#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"
#include "ui/Widgets.h"

#include <iostream>

// Renders the editor in several states to PNG files.
//   DucklingDev snapshot <outDir>
// Most states push a hand-built DisplayState through DucklingEditor::setDisplayOverride so every mode / status can
// be checked without a host. The last one uses the real processor after it processed some audio.

namespace juce::detail
{
// Defined in juce_events (juce_Messaging_windows.cpp); lets this console tool pump the message queue.
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}

namespace
{
void pumpMessages (int milliseconds)
{
   #if JUCE_WINDOWS || JUCE_LINUX
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;

    while (juce::Time::getMillisecondCounter() < end)
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (2);
   #else
    juce::ignoreUnused (milliseconds);
   #endif
}

bool writePng (juce::Component& c, const juce::File& file)
{
    file.deleteFile();
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << " (" << image.getWidth() << "x"
              << image.getHeight() << ")\n";
    return ok;
}

void setParam (dk::DucklingProcessor& p, const char* id, float plain)
{
    auto* param = p.getAPVTS().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

dk::DucklingProcessor::DisplayState playing (float phase, float gain)
{
    dk::DucklingProcessor::DisplayState s;
    s.phase = phase;
    s.gain = gain;
    s.hostHasPosition = true;
    s.hostPlaying = true;
    s.bpm = 128.0;
    s.processing = true;
    s.sidechainChannels = 2;
    s.triggerCount = 5;
    return s;
}

struct NullPlayHead final : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (126.0);
        info.setIsPlaying (true);
        info.setPpqPosition (playedSamples / 48000.0 * 126.0 / 60.0);
        return info;
    }

    double playedSamples = 0.0;
};
} // namespace

int runSnapshot (const juce::File& outputDir)
{
    outputDir.createDirectory();
    dk::ui::freezeAnimationsForSnapshots (true);
    bool ok = true;

    dk::DucklingProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editorHolder (proc.createEditorAndMakeActive());
    auto* editor = dynamic_cast<dk::ui::DucklingEditor*> (editorHolder.get());

    if (editor == nullptr)
    {
        std::cout << "FAILED to create the editor\n";
        return 1;
    }

    auto shot = [&] (const juce::String& name, int width = 720) {
        editor->setSize (width, width * 480 / 720);
        pumpMessages (30);
        editor->refreshNow();
        ok &= writePng (*editor, outputDir.getChildFile (name + "_" + juce::String (width) + ".png"));
    };

    // 1. Tempo, playing, Classic at full depth.
    {
        const float phase = 0.22f;
        editor->setDisplayOverride (playing (phase, 1.0f - dk::duckAmount (dk::CurveShape::Classic, phase)), 0.6f);
        shot ("01_tempo_playing");
        shot ("01_tempo_playing", 1080);
    }

    // 2. Tempo, stopped.
    {
        auto s = playing (-1.0f, 1.0f);
        s.hostPlaying = false;
        editor->setDisplayOverride (s);
        shot ("02_tempo_stopped");
    }

    // 3. Pump at 70 %, Lows only, 1/8.
    {
        setParam (proc, dk::ParamIDs::curve, (float) dk::CurveShape::Pump);
        setParam (proc, dk::ParamIDs::depth, 70.0f);
        setParam (proc, dk::ParamIDs::band, (float) dk::DuckBand::Lows);
        setParam (proc, dk::ParamIDs::rate, (float) dk::RateDivision::Eighth);
        const float phase = 0.55f;
        editor->setDisplayOverride (playing (phase, 1.0f - 0.7f * dk::duckAmount (dk::CurveShape::Pump, phase)));
        shot ("03_pump_lows");
    }

    // 4. Audio mode, listening, just triggered.
    {
        setParam (proc, dk::ParamIDs::trigger, (float) dk::TriggerMode::Audio);
        setParam (proc, dk::ParamIDs::curve, (float) dk::CurveShape::Punchy);
        setParam (proc, dk::ParamIDs::depth, 100.0f);
        setParam (proc, dk::ParamIDs::band, (float) dk::DuckBand::Full);
        setParam (proc, dk::ParamIDs::rate, (float) dk::RateDivision::Quarter);
        auto s = playing (0.08f, 1.0f - dk::duckAmount (dk::CurveShape::Punchy, 0.08f));
        s.sidechainDb = -9.0f;
        s.sidechainHeard = true;
        editor->setDisplayOverride (s, 0.9f);
        shot ("04_audio_listening");
    }

    // 5. Audio mode, nothing routed.
    {
        auto s = playing (-1.0f, 1.0f);
        s.sidechainChannels = 2;
        s.sidechainHeard = false;
        s.sidechainDb = -100.0f;
        editor->setDisplayOverride (s);
        shot ("05_audio_no_signal");
    }

    // 6. MIDI mode with notes.
    {
        setParam (proc, dk::ParamIDs::trigger, (float) dk::TriggerMode::Midi);
        setParam (proc, dk::ParamIDs::curve, (float) dk::CurveShape::Gate);
        auto s = playing (0.3f, 0.0f);
        s.midiNoteCount = 13;
        editor->setDisplayOverride (s, 0.7f);
        shot ("06_midi_notes");

        s.midiNoteCount = 0;
        s.phase = -1.0f;
        s.gain = 1.0f;
        editor->setDisplayOverride (s);
        shot ("06b_midi_waiting");
    }

    // 7. Help overlay.
    {
        setParam (proc, dk::ParamIDs::trigger, (float) dk::TriggerMode::Audio);
        editor->setDisplayOverride (playing (-1.0f, 1.0f));
        editor->setHelpVisible (true);
        shot ("07_help");
        editor->setHelpVisible (false);
    }

    // 8. The real processor after some audio in Tempo mode.
    {
        setParam (proc, dk::ParamIDs::trigger, (float) dk::TriggerMode::Tempo);
        setParam (proc, dk::ParamIDs::curve, (float) dk::CurveShape::Classic);
        NullPlayHead head;
        proc.setPlayHead (&head);
        juce::AudioBuffer<float> buffer (4, 512);
        juce::MidiBuffer midi;

        for (int i = 0; i < 40; ++i)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
            head.playedSamples += 512;
        }

        editor->setDisplayOverride (std::nullopt);
        shot ("08_real_processor");
        proc.setPlayHead (nullptr);
    }

    proc.editorBeingDeleted (editorHolder.get());
    editorHolder.reset();
    return ok ? 0 : 1;
}
