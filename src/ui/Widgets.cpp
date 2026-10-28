#include "ui/Widgets.h"

#include "DucklingBinary.h"

namespace dk::ui
{
namespace
{
//==============================================================================
// 2x2 box-filter downscale (premultiplied ARGB): a clean small logo from the 192 px PNG (same as Chordbloom).
juce::Image halveImage (const juce::Image& source)
{
    const auto src = source.convertedToFormat (juce::Image::ARGB);
    const int w = src.getWidth() / 2, h = src.getHeight() / 2;

    if (w < 1 || h < 1)
        return src;

    juce::Image dst (juce::Image::ARGB, w, h, true);
    const juce::Image::BitmapData s (src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData d (dst, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            juce::uint32 a = 0, r = 0, g = 0, b = 0;

            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    const auto* p = reinterpret_cast<const juce::PixelARGB*> (s.getPixelPointer (x * 2 + dx, y * 2 + dy));
                    a += p->getAlpha();
                    r += p->getRed();
                    g += p->getGreen();
                    b += p->getBlue();
                }

            auto* q = reinterpret_cast<juce::PixelARGB*> (d.getPixelPointer (x, y));
            q->setARGB ((juce::uint8) ((a + 2) / 4), (juce::uint8) ((r + 2) / 4),
                        (juce::uint8) ((g + 2) / 4), (juce::uint8) ((b + 2) / 4));
        }
    }

    return dst;
}

// The logo PNG at 96 and 48 px, decoded once (released at shutdown with the other JUCE singletons).
struct LogoImages final : public juce::DeletedAtShutdown
{
    LogoImages()
    {
        int size = 0;

        if (auto* data = DucklingBinary::getNamedResource ("logo_png", size); data != nullptr && size > 0)
        {
            const auto full = juce::ImageFileFormat::loadFrom (data, (size_t) size);

            if (full.isValid())
            {
                large = full.getWidth() > 120 ? halveImage (full) : full;
                small = large.getWidth() > 60 ? halveImage (large) : large;
            }
        }
    }

    ~LogoImages() override { clearSingletonInstance(); }

    juce::Image large, small;

    JUCE_DECLARE_SINGLETON_INLINE (LogoImages, false)
};

void drawVectorLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    // A mint body riding a lavender wave.
    const float s = area.getWidth();
    const auto body = juce::Rectangle<float> (s * 0.78f, s * 0.62f).withPosition (area.getX() + s * 0.12f, area.getY() + s * 0.12f);
    g.setGradientFill (juce::ColourGradient (Colours::curve.brighter (0.25f), body.getX(), body.getY(),
                                             Colours::curve.darker (0.15f), body.getRight(), body.getBottom(), false));
    g.fillEllipse (body);

    juce::Path wave;
    wave.startNewSubPath (area.getX() + s * 0.06f, area.getY() + s * 0.70f);
    wave.cubicTo (area.getX() + s * 0.35f, area.getY() + s * 0.52f, area.getX() + s * 0.55f, area.getY() + s * 0.95f,
                  area.getX() + s * 0.94f, area.getY() + s * 0.66f);
    g.setColour (Colours::trigger);
    g.strokePath (wave, juce::PathStrokeType (s * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

bool animationsFrozen = false;   // message thread only
} // namespace

//==============================================================================
namespace fmt
{
juce::String number (float value, int decimals)
{
    const float half = 0.5f * std::pow (10.0f, (float) -decimals);

    if (std::abs (value) < half)
        value = 0.0f;

    return decimals > 0 ? juce::String (value, decimals) : juce::String (juce::roundToInt (value));
}

juce::String gainDb (float linearGain)
{
    if (linearGain <= 0.0005f)
        return "-inf dB";

    return number (20.0f * std::log10 (linearGain), 1) + " dB";
}
} // namespace fmt

juce::AttributedString richText (const juce::String& markup, float size, juce::Colour normal, juce::Colour strong,
                                 float lineSpacing)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.setJustification (juce::Justification::topLeft);
    s.setLineSpacing (lineSpacing);

    int start = 0;
    bool bold = false;

    for (;;)
    {
        const int index = markup.indexOf (start, "**");
        const auto piece = index < 0 ? markup.substring (start) : markup.substring (start, index);

        if (piece.isNotEmpty())
            s.append (piece, font (size, bold ? 600 : 400), bold ? strong : normal);

        if (index < 0)
            break;

        start = index + 2;
        bold = ! bold;
    }

    return s;
}

float animationPhase (juce::uint32 periodMs)
{
    const auto now = animationsFrozen ? juce::uint32 { 250 } : juce::Time::getMillisecondCounter();
    return periodMs > 0 ? (float) (now % periodMs) / (float) periodMs : 0.0f;
}

void freezeAnimationsForSnapshots (bool shouldFreeze) { animationsFrozen = shouldFreeze; }
bool animationsAreFrozen()                           { return animationsFrozen; }

juce::Colour blend (juce::Colour a, juce::Colour b, float amountOfB)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, amountOfB));
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto& images = *LogoImages::getInstance();

    if (! images.small.isValid())
    {
        drawVectorLogo (g, area);
        return;
    }

    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto& image = (area.getWidth() * pixelScale > 48.0f && images.large.isValid()) ? images.large : images.small;
    juce::Graphics::ScopedSaveState state (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, area, juce::RectanglePlacement::centred);
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icon iconType) : juce::Button (name), icon (iconType)
{
    setTitle (name);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();

    if (highlighted || down || on)
    {
        g.setColour (Colours::panelHover.withAlpha (down ? 1.0f : 0.85f));

        if (roundBackground)
        {
            g.fillEllipse (b);
        }
        else
        {
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (on ? Colours::accent.withAlpha (0.45f) : Colours::outline.brighter (0.1f));
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
        }
    }

    g.setColour (on ? Colours::accent : (highlighted ? Colours::text : Colours::textDim));
    paintIcon (g, icon, b.withSizeKeepingCentre (16.0f, 16.0f));
}

void IconButton::paintIcon (juce::Graphics& g, Icon type, juce::Rectangle<float> box)
{
    switch (type)
    {
        case Icon::help:
        {
            const auto circle = box.reduced (0.5f);
            g.drawEllipse (circle, 1.4f);
            g.setFont (font (11.0f, 700));
            g.drawText ("?", circle.translated (0.0f, 0.2f), juce::Justification::centred, false);
            break;
        }

        case Icon::close:
        {
            const auto c = box.getCentre();
            const float s = 4.0f;
            juce::Path p;
            p.startNewSubPath (c.x - s, c.y - s);
            p.lineTo (c.x + s, c.y + s);
            p.startNewSubPath (c.x + s, c.y - s);
            p.lineTo (c.x - s, c.y + s);
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
    }
}

//==============================================================================
Segmented::Segmented (juce::StringArray itemNames, int numColumns)
    : items (std::move (itemNames)), columns (numColumns)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void Segmented::setSelected (int index)
{
    index = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);

    if (index == selected)
        return;

    selected = index;
    repaint();

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

juce::Rectangle<float> Segmented::segmentBounds (int index) const
{
    const auto inner = getLocalBounds().toFloat().reduced (3.0f);
    const int cols = columns > 0 ? columns : juce::jmax (1, items.size());
    const int rows = (items.size() + cols - 1) / cols;
    const float gap = 2.0f;
    const float w = (inner.getWidth() - gap * (float) (cols - 1)) / (float) cols;
    const float h = (inner.getHeight() - gap * (float) (rows - 1)) / (float) juce::jmax (1, rows);
    const int r = index / cols, c = index % cols;
    return { inner.getX() + (float) c * (w + gap), inner.getY() + (float) r * (h + gap), w, h };
}

int Segmented::indexAt (juce::Point<float> p) const
{
    for (int i = 0; i < items.size(); ++i)
        if (segmentBounds (i).expanded (1.0f).contains (p))
            return i;

    return -1;
}

void Segmented::paint (juce::Graphics& g)
{
    const auto body = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = 8.0f;
    const bool enabled = isEnabled();

    g.setGradientFill (juce::ColourGradient::vertical (Colours::background.darker (0.10f), body.getY(),
                                                       Colours::background.brighter (0.02f), body.getBottom()));
    g.fillRoundedRectangle (body, radius);
    g.setColour (Colours::outline.withAlpha (0.8f));
    g.drawRoundedRectangle (body, radius, 1.0f);

    for (int i = 0; i < items.size(); ++i)
    {
        const auto seg = segmentBounds (i);
        const bool isSelected = i == selected;
        const bool isHovered = i == hovered && enabled;

        if (isSelected)
        {
            const auto tint = enabled ? accent : Colours::textFaint;
            g.setGradientFill (juce::ColourGradient::vertical (blend (Colours::panelHover, tint, 0.22f), seg.getY(),
                                                               blend (Colours::panelRaised, tint, 0.12f), seg.getBottom()));
            g.fillRoundedRectangle (seg, radius - 2.5f);
            g.setColour (tint.withAlpha (enabled ? 0.75f : 0.4f));
            g.drawRoundedRectangle (seg.reduced (0.5f), radius - 2.5f, 1.0f);
        }
        else if (isHovered)
        {
            g.setColour (Colours::panelHover.withAlpha (0.7f));
            g.fillRoundedRectangle (seg, radius - 2.5f);
        }

        auto textColour = isSelected ? Colours::text : (isHovered ? Colours::text.withAlpha (0.9f) : Colours::textDim);

        if (! enabled)
            textColour = textColour.withMultipliedAlpha (0.5f);

        g.setColour (textColour);
        g.setFont (font (12.0f, isSelected ? 600 : 400));
        g.drawFittedText (items[i], seg.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (! isLeftClick (e) || ! isEnabled())
        return;

    const int index = indexAt (e.position);

    if (index >= 0 && index != selected)
    {
        setSelected (index);

        if (onChange != nullptr)
            onChange (index);
    }
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const int index = indexAt (e.position);

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

juce::String Segmented::getTooltip()
{
    return juce::isPositiveAndBelow (hovered, tooltips.size()) ? tooltips[hovered] : juce::String();
}

std::unique_ptr<juce::AccessibilityHandler> Segmented::createAccessibilityHandler()
{
    struct Value final : public juce::AccessibilityTextValueInterface
    {
        explicit Value (Segmented& s) : owner (s) {}
        bool isReadOnly() const override { return false; }
        juce::String getCurrentValueAsString() const override { return owner.items[owner.selected]; }

        void setValueAsString (const juce::String& v) override
        {
            const int index = owner.items.indexOf (v.trim(), true);

            if (index >= 0 && index != owner.selected)
            {
                owner.setSelected (index);

                if (owner.onChange != nullptr)
                    owner.onChange (index);
            }
        }

        Segmented& owner;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::comboBox,
                                                         juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
namespace
{
const char* const modeLabels[] = { "TEMPO", "AUDIO", "MIDI" };
const char* const modeSubLabels[] = { "follows FL", "sidechain", "notes" };
const char* const modeTips[] = {
    "Tempo: ducks in time with your project on every Rate step. Nothing to route - just press play.",
    "Audio: ducks whenever the sidechain signal (your kick) gets louder than the threshold. Needs routing - see ?.",
    "MIDI: ducks on every note Duckling receives, so you can draw your own duck pattern. Needs routing - see ?."
};
} // namespace

ModeSwitch::ModeSwitch()
{
    setTitle ("Trigger");
    setDescription ("What makes Duckling duck");
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::Colour ModeSwitch::modeColour (int index)
{
    switch (index)
    {
        case 1:  return Colours::trigger;   // lavender
        case 2:  return Colours::lows;      // sky
        default: return Colours::curve;     // mint
    }
}

void ModeSwitch::setSelected (int index)
{
    index = juce::jlimit (0, 2, index);

    if (index == selected)
        return;

    selected = index;

    if (animationsAreFrozen() || ! isShowing())
    {
        position = (float) selected;
        repaint();
    }
    else
    {
        startTimerHz (60);
    }

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void ModeSwitch::timerCallback()
{
    const float target = (float) selected;
    position += (target - position) * 0.3f;

    if (std::abs (target - position) < 0.01f)
    {
        position = target;
        stopTimer();
    }

    repaint();
}

int ModeSwitch::indexAt (float x) const
{
    const auto inner = getLocalBounds().toFloat().reduced (7.0f);
    return juce::jlimit (0, 2, (int) ((x - inner.getX()) / (inner.getWidth() / 3.0f)));
}

juce::String ModeSwitch::getTooltip()
{
    return modeTips[indexAt ((float) getMouseXYRelative().x)];
}

void ModeSwitch::paint (juce::Graphics& g)
{
    const auto body = getLocalBounds().toFloat().reduced (4.0f);
    const float radius = juce::jmin (10.0f, body.getHeight() * 0.5f);

    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (body.translated (0.0f, 1.0f), radius);
    g.setGradientFill (juce::ColourGradient::vertical (Colours::background.darker (0.12f), body.getY(),
                                                       Colours::background.brighter (0.02f), body.getBottom()));
    g.fillRoundedRectangle (body, radius);
    g.setColour (Colours::outline.withAlpha (0.9f));
    g.drawRoundedRectangle (body.reduced (0.5f), radius, 1.0f);

    const auto inner = body.reduced (3.0f);
    const float segW = inner.getWidth() / 3.0f;
    const float segRadius = radius - 2.5f;

    // Highlight colour blends between the neighbouring modes while it slides.
    const int lower = juce::jlimit (0, 2, (int) std::floor (position));
    const int upper = juce::jlimit (0, 2, lower + 1);
    const auto colour = blend (modeColour (lower), modeColour (upper), position - (float) lower);
    const auto highlight = juce::Rectangle<float> (inner.getX() + segW * position, inner.getY(), segW, inner.getHeight());

    {
        juce::Path shape;
        shape.addRoundedRectangle (highlight, segRadius);
        juce::DropShadow (colour.withAlpha (0.42f), 14, {}).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient::vertical (blend (Colours::panelRaised, colour, 0.34f), highlight.getY(),
                                                           blend (Colours::panel, colour, 0.20f), highlight.getBottom()));
        g.fillPath (shape);
        g.setColour (colour.withAlpha (0.85f));
        g.drawRoundedRectangle (highlight.reduced (0.5f), segRadius, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (highlight.reduced (segRadius, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
    }

    const int hover = isMouseOver() ? indexAt ((float) getMouseXYRelative().x) : -1;

    for (int i = 0; i < 3; ++i)
    {
        const auto seg = juce::Rectangle<float> (inner.getX() + segW * (float) i, inner.getY(), segW, inner.getHeight());
        const float active = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (position - (float) i));
        const auto role = modeColour (i);

        if (hover == i && active < 0.5f)
        {
            g.setColour (Colours::panelHover.withAlpha (0.6f));
            g.fillRoundedRectangle (seg.reduced (1.0f), segRadius);
            g.setColour (role.withAlpha (0.3f));
            g.drawRoundedRectangle (seg.reduced (1.5f), segRadius, 1.0f);
        }

        const auto labelFont = font (juce::jmin (13.0f, seg.getHeight() * 0.38f), 700).withExtraKerningFactor (0.08f);
        const juce::String label (modeLabels[i]);
        const float dotD = 6.0f;
        const float labelW = textWidth (labelFont, label);
        const float startX = std::round (seg.getCentreX() - (labelW + dotD + 6.0f) * 0.5f);
        const float labelY = seg.getY() + seg.getHeight() * 0.12f;
        const float labelH = seg.getHeight() * 0.46f;

        g.setColour (role.withAlpha (0.45f + 0.55f * active));
        g.fillEllipse (juce::Rectangle<float> (dotD, dotD).withCentre ({ startX + dotD * 0.5f, labelY + labelH * 0.5f }));

        g.setFont (labelFont);
        g.setColour (blend (hover == i ? Colours::text : Colours::textDim, role.brighter (0.35f), active));
        g.drawText (label, juce::Rectangle<float> (startX + dotD + 6.0f, labelY, labelW + 4.0f, labelH),
                    juce::Justification::centredLeft, false);

        g.setFont (font (juce::jmin (11.0f, seg.getHeight() * 0.30f), 600));
        g.setColour (blend (Colours::textSubtle, role.brighter (0.1f), active));
        g.drawText (modeSubLabels[i], seg.withTrimmedTop (seg.getHeight() * 0.55f).withHeight (seg.getHeight() * 0.36f),
                    juce::Justification::centred, false);
    }
}

void ModeSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (! isLeftClick (e))
        return;

    const int index = indexAt (e.position.x);

    if (index != selected)
    {
        setSelected (index);

        if (onChange != nullptr)
            onChange (index);
    }
}

std::unique_ptr<juce::AccessibilityHandler> ModeSwitch::createAccessibilityHandler()
{
    struct Value final : public juce::AccessibilityTextValueInterface
    {
        explicit Value (ModeSwitch& s) : owner (s) {}
        bool isReadOnly() const override { return false; }
        juce::String getCurrentValueAsString() const override { return modeLabels[owner.selected]; }

        void setValueAsString (const juce::String& v) override
        {
            for (int i = 0; i < 3; ++i)
                if (v.trim().equalsIgnoreCase (modeLabels[i]) && i != owner.selected)
                {
                    owner.setSelected (i);

                    if (owner.onChange != nullptr)
                        owner.onChange (i);
                }
        }

        ModeSwitch& owner;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::comboBox,
                                                         juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

} // namespace dk::ui
