#include "plugin/PluginProcessor.h"

#include "ui/PluginEditor.h"

namespace dk
{
namespace
{
juce::StringArray curveNames()
{
    juce::StringArray names;
    for (int i = 0; i < numCurveShapes; ++i)
        names.add (curveName ((CurveShape) i));
    return names;
}

juce::StringArray rateNames()
{
    juce::StringArray names;
    for (int i = 0; i < numRateDivisions; ++i)
        names.add (rateName ((RateDivision) i));
    return names;
}

juce::StringArray modeNames()
{
    juce::StringArray names;
    for (int i = 0; i < numTriggerModes; ++i)
        names.add (triggerModeName ((TriggerMode) i));
    return names;
}

int choiceIndex (const std::atomic<float>* p, int count) noexcept
{
    return juce::jlimit (0, count - 1, juce::roundToInt (p->load()));
}

constexpr float kSidechainHeardDb = -70.0f;
} // namespace

const char* duckBandName (DuckBand band)
{
    switch (band)
    {
        case DuckBand::Full:  return "Full";
        case DuckBand::Lows:  return "Lows";
        case DuckBand::Highs: return "Highs";
        case DuckBand::NumBands: break;
    }

    return "Full";
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DucklingProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::trigger, 1 }, "Trigger", modeNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::rate, 1 }, "Rate", rateNames(),
                                                        (int) RateDivision::Quarter));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::curve, 1 }, "Curve", curveNames(), 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::depth, 1 }, "Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)) + " %"; })));

    StringArray bands;
    for (int i = 0; i < (int) DuckBand::NumBands; ++i)
        bands.add (duckBandName ((DuckBand) i));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::band, 1 }, "Band", bands, 0));

    NormalisableRange<float> xoverRange (40.0f, 1000.0f, 1.0f);
    xoverRange.setSkewForCentre (180.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::crossover, 1 }, "Crossover", xoverRange, 150.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::threshold, 1 }, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));

    return layout;
}

DucklingProcessor::DucklingProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DucklingState", createParameterLayout())
{
    triggerParam = apvts.getRawParameterValue (ParamIDs::trigger);
    rateParam = apvts.getRawParameterValue (ParamIDs::rate);
    curveParam = apvts.getRawParameterValue (ParamIDs::curve);
    depthParam = apvts.getRawParameterValue (ParamIDs::depth);
    bandParam = apvts.getRawParameterValue (ParamIDs::band);
    crossoverParam = apvts.getRawParameterValue (ParamIDs::crossover);
    thresholdParam = apvts.getRawParameterValue (ParamIDs::threshold);

    crossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
}

DucklingProcessor::~DucklingProcessor() = default;

//==============================================================================
void DucklingProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, juce::jmin (samplesPerBlock, kChunk));
    spec.numChannels = (juce::uint32) juce::jmax (1, getMainBusNumInputChannels());
    crossover.prepare (spec);
    currentCrossoverHz = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), crossoverParam->load());
    crossover.setCutoffFrequency (currentCrossoverHz);
    crossover.reset();

    samplesSinceSidechainSignal = 1 << 30;
}

void DucklingProcessor::releaseResources() {}

bool DucklingProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);

        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void DucklingProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    auto mainBuffer = getBusBuffer (buffer, false, 0);
    const int mainChannels = juce::jmin (mainBuffer.getNumChannels(), getMainBusNumInputChannels());

    for (int ch = mainChannels; ch < mainBuffer.getNumChannels(); ++ch)
        mainBuffer.clear (ch, 0, numSamples);

    // Sidechain channels (read straight from the host buffer; no AudioBuffer copies on the audio thread).
    std::array<const float*, 2> sidechain {};
    int sidechainChannels = 0;

    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
            for (int ch = 0; ch < juce::jmin (2, bus->getNumberOfChannels()); ++ch)
            {
                const int index = getChannelIndexInProcessBlockBuffer (true, 1, ch);

                if (index >= 0 && index < buffer.getNumChannels())
                    sidechain[(size_t) sidechainChannels++] = buffer.getReadPointer (index);
            }

    // Transport
    TransportInfo transport;
    double beatsPerBar = 4.0;

    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm())
                transport.bpm = *bpm;

            if (const auto ppq = position->getPpqPosition())
            {
                transport.hasPosition = true;
                transport.ppq = *ppq;
            }

            transport.isPlaying = position->getIsPlaying();

            if (const auto sig = position->getTimeSignature(); sig.hasValue() && sig->numerator > 0 && sig->denominator > 0)
                beatsPerBar = (double) sig->numerator * 4.0 / (double) sig->denominator;
        }
    }

    if (! std::isfinite (transport.bpm) || transport.bpm < 1.0)
        transport.bpm = 120.0;

    // Parameters (read once per block)
    DuckSettings settings;
    settings.mode = (TriggerMode) choiceIndex (triggerParam, numTriggerModes);
    settings.curve = (CurveShape) choiceIndex (curveParam, numCurveShapes);
    settings.rateBeats = rateInBeats ((RateDivision) choiceIndex (rateParam, numRateDivisions), beatsPerBar);
    settings.depth = juce::jlimit (0.0f, 1.0f, depthParam->load() * 0.01f);
    settings.thresholdDb = thresholdParam->load();
    const auto band = (DuckBand) choiceIndex (bandParam, (int) DuckBand::NumBands);

    const float wantedXover = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), crossoverParam->load());

    if (std::abs (wantedXover - currentCrossoverHz) > 0.01f)
    {
        currentCrossoverHz = wantedXover;
        crossover.setCutoffFrequency (currentCrossoverHz);
    }

    const double beatsPerSample = transport.bpm / (60.0 * currentSampleRate);
    auto midiIterator = midi.cbegin();
    const auto midiEnd = midi.cend();

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int n = juce::jmin (kChunk, numSamples - start);

        // Sidechain detector input: the loudest channel per sample.
        for (int i = 0; i < n; ++i)
            detector[(size_t) i] = 0.0f;

        bool heard = false;
        const float heardLevel = juce::Decibels::decibelsToGain (kSidechainHeardDb);

        for (int ch = 0; ch < sidechainChannels; ++ch)
        {
            const float* in = sidechain[(size_t) ch] + start;

            for (int i = 0; i < n; ++i)
            {
                const float v = std::abs (in[i]);
                detector[(size_t) i] = juce::jmax (detector[(size_t) i], v);
                heard = heard || v > heardLevel;
            }
        }

        samplesSinceSidechainSignal = heard ? 0 : juce::jmin (1 << 30, samplesSinceSidechainSignal + n);

        // Note-ons inside this chunk
        int numMidi = 0;

        for (; midiIterator != midiEnd; ++midiIterator)
        {
            const auto metadata = *midiIterator;

            if (metadata.samplePosition >= start + n)
                break;

            if (metadata.getMessage().isNoteOn())
            {
                midiNotes.fetch_add (1, std::memory_order_relaxed);

                if (numMidi < kMaxMidiPerChunk)
                    midiOffsets[(size_t) numMidi++] = juce::jlimit (0, n - 1, metadata.samplePosition - start);
            }
        }

        TransportInfo chunkTransport = transport;

        if (transport.hasPosition && transport.isPlaying)
            chunkTransport.ppq = transport.ppq + (double) start * beatsPerSample;

        engine.process (settings, chunkTransport, sidechainChannels > 0 ? detector.data() : nullptr,
                        midiOffsets.data(), numMidi, gains.data(), n);

        // Apply
        for (int ch = 0; ch < mainChannels; ++ch)
        {
            float* data = mainBuffer.getWritePointer (ch, start);

            if (band == DuckBand::Full)
            {
                for (int i = 0; i < n; ++i)
                {
                    float low = 0.0f, high = 0.0f;
                    crossover.processSample (ch, data[i], low, high);   // keeps the filter warm for a band switch
                    data[i] *= gains[(size_t) i];
                }
            }
            else
            {
                const bool duckLows = band == DuckBand::Lows;

                for (int i = 0; i < n; ++i)
                {
                    float low = 0.0f, high = 0.0f;
                    crossover.processSample (ch, data[i], low, high);
                    data[i] = duckLows ? low * gains[(size_t) i] + high : low + high * gains[(size_t) i];
                }
            }
        }
    }

    crossover.snapToZero();

    // The input MIDI is consumed (Duckling produces no MIDI).
    midi.clear();

    displayPhase.store (engine.getPhase(), std::memory_order_relaxed);
    displayGain.store (engine.getGain(), std::memory_order_relaxed);
    displaySidechainDb.store (engine.getDetectorLevelDb(), std::memory_order_relaxed);
    displayTriggers.store (engine.getTriggerCount(), std::memory_order_relaxed);
    displayHasPosition.store (transport.hasPosition, std::memory_order_relaxed);
    displayPlaying.store (transport.isPlaying, std::memory_order_relaxed);
    displayBpm.store (transport.bpm, std::memory_order_relaxed);
    displayBeatsPerBar.store (beatsPerBar, std::memory_order_relaxed);
    displaySidechainChannels.store (sidechainChannels, std::memory_order_relaxed);
    displaySidechainHeard.store (samplesSinceSidechainSignal < (int) (currentSampleRate * 2.0), std::memory_order_relaxed);
    lastProcessMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
}

DucklingProcessor::DisplayState DucklingProcessor::getDisplayState() const noexcept
{
    DisplayState s;
    s.phase = displayPhase.load (std::memory_order_relaxed);
    s.gain = displayGain.load (std::memory_order_relaxed);
    s.sidechainDb = displaySidechainDb.load (std::memory_order_relaxed);
    s.triggerCount = displayTriggers.load (std::memory_order_relaxed);
    s.midiNoteCount = midiNotes.load (std::memory_order_relaxed);
    s.hostHasPosition = displayHasPosition.load (std::memory_order_relaxed);
    s.hostPlaying = displayPlaying.load (std::memory_order_relaxed);
    s.bpm = displayBpm.load (std::memory_order_relaxed);
    s.beatsPerBar = displayBeatsPerBar.load (std::memory_order_relaxed);
    s.sidechainChannels = displaySidechainChannels.load (std::memory_order_relaxed);
    s.sidechainHeard = displaySidechainHeard.load (std::memory_order_relaxed);

    const auto last = lastProcessMs.load (std::memory_order_relaxed);
    s.processing = last != 0 && juce::Time::getMillisecondCounter() - last < 500;

    if (! s.processing)
    {
        // The host stopped calling us (FL's "smart disable", or nothing playing): show an idle display.
        s.phase = -1.0f;
        s.gain = 1.0f;
        s.sidechainDb = DuckEngine::kFloorDb;
    }

    return s;
}

//==============================================================================
juce::AudioProcessorEditor* DucklingProcessor::createEditor()
{
    return new ui::DucklingEditor (*this);
}

void DucklingProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorWidth", editorWidth.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DucklingProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            editorWidth.store ((int) state.getProperty ("editorWidth", kDefaultEditorWidth));
            state.removeProperty ("editorWidth", nullptr);
            apvts.replaceState (state);
        }
    }
}
} // namespace dk

#if ! DUCKLING_DEV_TOOL
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dk::DucklingProcessor();
}
#endif
