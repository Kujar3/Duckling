#include "ui/Theme.h"

#include "DucklingBinary.h"

namespace dk::ui
{
namespace
{
//==============================================================================
// Inter typefaces, created once from the embedded TTFs. Released by JUCE's shutdown (DeletedAtShutdown) so the
// native font objects never outlive the platform font machinery.
struct InterFonts final : public juce::DeletedAtShutdown
{
    InterFonts()
    {
        regular  = load ("Inter400_ttf");
        semibold = load ("Inter600_ttf");
        bold     = load ("Inter700_ttf");

        if (semibold == nullptr) semibold = bold != nullptr ? bold : regular;
        if (bold == nullptr)     bold = semibold;
    }

    ~InterFonts() override { clearSingletonInstance(); }

    static juce::Typeface::Ptr load (const char* resourceName)
    {
        int size = 0;

        if (auto* data = DucklingBinary::getNamedResource (resourceName, size))
            if (size > 0)
                return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);

        return nullptr;
    }

    juce::Typeface::Ptr regular, semibold, bold;

    JUCE_DECLARE_SINGLETON_INLINE (InterFonts, false)
};

const juce::Colour tooltipFill { 0xff13171c };

juce::TextLayout layoutTooltipText (const juce::String& text, juce::Colour colour)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (text, font (12.5f), colour);

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 280.0f);
    return layout;
}

juce::Path roundedRectPath (juce::Rectangle<float> b, float radius, const juce::Button& button)
{
    const bool left = button.isConnectedOnLeft(), right = button.isConnectedOnRight();
    const bool top = button.isConnectedOnTop(), bottom = button.isConnectedOnBottom();

    juce::Path p;
    p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), radius, radius,
                           ! (left || top), ! (right || top), ! (left || bottom), ! (right || bottom));
    return p;
}

bool hasFlag (const juce::Component& c, const char* flag)
{
    return (bool) c.getProperties().getWithDefault (flag, false);
}
} // namespace

//==============================================================================
juce::Font font (float height, int weight)
{
    auto& fonts = *InterFonts::getInstance();
    auto typeface = weight >= 650 ? fonts.bold : (weight >= 500 ? fonts.semibold : fonts.regular);

    if (typeface == nullptr)
        typeface = fonts.regular;

    if (typeface != nullptr)
        return juce::Font (juce::FontOptions (typeface).withHeight (height));

    return juce::Font (juce::FontOptions (height, weight >= 500 ? juce::Font::bold : juce::Font::plain));
}

float textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, float radius)
{
    // Same card style as the palette / lane / export panels (which paint their own surfaces).
    const auto b = bounds.reduced (0.5f);
    g.setGradientFill (juce::ColourGradient::vertical (Colours::panel.brighter (0.035f), b.getY(), Colours::panel, b.getBottom()));
    g.fillRoundedRectangle (b, radius);
    g.setColour (Colours::outline.withAlpha (0.55f));
    g.drawRoundedRectangle (b, radius, 1.0f);
}

void neverTakeKeyboardFocus (juce::Component& root)
{
    // A text editor (and its inner text holder) must be able to take focus when clicked.
    if (dynamic_cast<juce::TextEditor*> (&root) != nullptr)
        return;

    root.setWantsKeyboardFocus (false);
    root.setMouseClickGrabsKeyboardFocus (false);

    for (auto* child : root.getChildren())
        if (child != nullptr)
            neverTakeKeyboardFocus (*child);
}

void drawSectionTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                       juce::Justification justification)
{
    g.setFont (font (10.5f, 600).withExtraKerningFactor (0.09f));
    g.setColour (Colours::textDim);   // full alpha: >= 5:1 contrast at 10.5 px
    g.drawText (text.toUpperCase(), area, justification, true);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    using juce::Colour;
    namespace jc = juce::Colours;

    setColourScheme ({ Colours::background, Colours::panelRaised, Colours::panel, Colours::outline, Colours::text,
                       Colours::accent, Colours::background, Colours::panelHover, Colours::text });

    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::DocumentWindow::textColourId, Colours::text);

    setColour (juce::ComboBox::backgroundColourId, Colours::panelRaised);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::buttonColourId, Colours::panelRaised);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, Colours::accent);

    // A not-quite-opaque background makes JUCE create transparent menu windows, so the rounded corners work.
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel.withAlpha (0.995f));
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::headerTextColourId, Colours::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::panelHover);
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);

    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Label::backgroundColourId, jc::transparentBlack);
    setColour (juce::Label::outlineColourId, jc::transparentBlack);
    setColour (juce::Label::textWhenEditingColourId, Colours::text);
    setColour (juce::Label::backgroundWhenEditingColourId, Colours::panelRaised);
    setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);

    setColour (juce::TextButton::buttonColourId, Colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::accent);

    setColour (juce::ToggleButton::textColourId, Colours::text);
    setColour (juce::ToggleButton::tickColourId, Colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours::textFaint);

    setColour (juce::Slider::backgroundColourId, Colours::outline);
    setColour (juce::Slider::trackColourId, Colours::accent);
    setColour (juce::Slider::thumbColourId, Colours::text);
    setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Colours::outline);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, Colours::panelRaised);
    setColour (juce::Slider::textBoxOutlineColourId, jc::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Colours::accent.withAlpha (0.3f));

    setColour (juce::BubbleComponent::backgroundColourId, tooltipFill);
    setColour (juce::BubbleComponent::outlineColourId, Colours::outline);
    setColour (juce::TooltipWindow::backgroundColourId, tooltipFill);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::outlineColourId, Colours::outline);

    setColour (juce::ScrollBar::thumbColourId, Colours::textFaint);
    setColour (juce::ScrollBar::trackColourId, jc::transparentBlack);
    setColour (juce::ScrollBar::backgroundColourId, jc::transparentBlack);

    setColour (juce::TextEditor::backgroundColourId, Colours::panelRaised);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, Colours::text);
    setColour (juce::CaretComponent::caretColourId, Colours::accent);

    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::outline);

    setColour (juce::ListBox::backgroundColourId, Colours::panel);
    setColour (juce::ListBox::textColourId, Colours::text);
    setColour (juce::ListBox::outlineColourId, Colours::outline);

    setColour (juce::GroupComponent::outlineColourId, Colours::outline);
    setColour (juce::GroupComponent::textColourId, Colours::textDim);

    setColour (juce::HyperlinkButton::textColourId, Colours::accent);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, Colours::panelHover);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, Colours::text);
}

LookAndFeel::~LookAndFeel() = default;

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
    {
        auto& fonts = *InterFonts::getInstance();

        if (auto tf = f.isBold() ? fonts.semibold : fonts.regular)
            return tf;
    }

    return LookAndFeel_V4::getTypefaceForFont (f);
}

//==============================================================================
// Combo boxes
void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int,
                                juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const float radius = juce::jmin (6.0f, b.getHeight() * 0.5f);
    const bool enabled = box.isEnabled();
    const bool hover = enabled && box.isMouseOver (true);
    const bool open = box.isPopupActive() || isButtonDown;
    const float alpha = enabled ? 1.0f : 0.45f;

    auto fill = hover || open ? Colours::panelHover : Colours::panelRaised;
    g.setGradientFill (juce::ColourGradient::vertical (fill.brighter (0.04f).withMultipliedAlpha (alpha), b.getY(),
                                                       fill.darker (0.06f).withMultipliedAlpha (alpha), b.getBottom()));
    g.fillRoundedRectangle (b, radius);

    g.setColour ((open ? Colours::accent.withAlpha (0.75f)
                       : Colours::outline.brighter (hover ? 0.18f : 0.0f)).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (b, radius, 1.0f);

    // chevron
    const float cx = b.getRight() - 13.0f;
    const float cy = b.getCentreY();
    const float s = hasFlag (box, kPropProminent) ? 4.0f : 3.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - s, cy - s * 0.45f);
    chevron.lineTo (cx, cy + s * 0.55f);
    chevron.lineTo (cx + s, cy - s * 0.45f);
    g.setColour ((hover || open ? Colours::text : Colours::textDim).withMultipliedAlpha (alpha));
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    if (hasFlag (box, kPropProminent))
        return font (juce::jmin (15.5f, (float) box.getHeight() * 0.52f), 600);

    return font (juce::jmin (13.0f, (float) box.getHeight() * 0.5f), 400);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, juce::jmax (0, box.getWidth() - 26), box.getHeight() - 2);
    label.setBorderSize ({ 0, hasFlag (box, kPropProminent) ? 11 : 9, 0, 2 });
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.85f);
}

void LookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
{
    g.setColour (Colours::textFaint);
    g.setFont (getComboBoxFont (box));
    g.drawFittedText (box.getTextWhenNothingSelected(),
                      label.getBounds().withTrimmedLeft (label.getBorderSize().getLeft()),
                      label.getJustificationType(), 1, 0.9f);
}

//==============================================================================
// Popup menus
void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);

    if (! juce::Desktop::canUseSemiTransparentWindows())
        g.fillAll (Colours::panel);

    const auto b = bounds.reduced (0.5f);
    g.setGradientFill (juce::ColourGradient::vertical (Colours::panel.brighter (0.05f), b.getY(),
                                                       Colours::panel.darker (0.02f), b.getBottom()));
    g.fillRoundedRectangle (b, kCornerRadius);
    g.setColour (Colours::outline.brighter (0.1f));
    g.drawRoundedRectangle (b, kCornerRadius, 1.0f);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                     bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                     const juce::String& text, const juce::String& shortcutKeyText,
                                     const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        auto r = area.toFloat().reduced (10.0f, 0.0f);
        g.setColour (Colours::outline);
        g.fillRect (r.withHeight (1.0f).withCentre (r.getCentre()));
        return;
    }

    auto r = area.toFloat().reduced (4.0f, 1.0f);

    if (isHighlighted && isActive)
    {
        g.setColour (Colours::panelHover);
        g.fillRoundedRectangle (r, 5.0f);
    }

    auto colour = textColour != nullptr ? *textColour : Colours::text;

    if (! isActive)
        colour = Colours::textFaint;
    else if (! isHighlighted && ! isTicked && textColour == nullptr)
        colour = Colours::text.withAlpha (0.92f);

    auto tickArea = r.removeFromLeft (22.0f);

    if (isTicked)
    {
        const auto c = tickArea.getCentre().translated (2.0f, 0.0f);
        juce::Path tick;
        tick.startNewSubPath (c.x - 3.5f, c.y + 0.2f);
        tick.lineTo (c.x - 1.0f, c.y + 2.8f);
        tick.lineTo (c.x + 3.8f, c.y - 2.8f);
        g.setColour (Colours::accent);
        g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        colour = isActive ? Colours::accent.brighter (0.25f) : colour;
    }

    if (icon != nullptr)
    {
        auto iconArea = r.removeFromLeft (r.getHeight()).reduced (4.0f);
        icon->drawWithin (g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
        r.removeFromLeft (4.0f);
    }

    if (hasSubMenu)
    {
        auto arrowArea = r.removeFromRight (18.0f);
        const auto c = arrowArea.getCentre();
        juce::Path arrow;
        arrow.startNewSubPath (c.x - 1.5f, c.y - 3.5f);
        arrow.lineTo (c.x + 2.0f, c.y);
        arrow.lineTo (c.x - 1.5f, c.y + 3.5f);
        g.setColour (colour.withMultipliedAlpha (0.8f));
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    r.removeFromRight (10.0f);
    auto f = getPopupMenuFont();
    g.setFont (f);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (isActive ? Colours::textSubtle : Colours::textFaint);   // readable hint ("since reset")
        g.setFont (font (12.0f));
        g.drawText (shortcutKeyText, r, juce::Justification::centredRight, true);
        g.setFont (f);
    }

    g.setColour (colour);
    g.drawFittedText (text, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
}

void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                              const juce::String& sectionName)
{
    drawSectionTitle (g, sectionName, area.withTrimmedLeft (12).withTrimmedTop (4));
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth,
                                             int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 40;
        idealHeight = 9;
        return;
    }

    idealHeight = 28;
    idealWidth = (int) std::ceil (textWidth (getPopupMenuFont(), text.upToFirstOccurrenceOf ("\t", false, false)))
                 + 4 + 22 + 10 + 22;
}

void LookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int, int& idealWidth,
                                                                 int& idealHeight, const juce::PopupMenu::Options&)
{
    idealHeight = 26;
    idealWidth = (int) std::ceil (textWidth (font (10.5f, 600).withExtraKerningFactor (0.09f), text.toUpperCase())) + 32;
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (13.5f); }
int LookAndFeel::getPopupMenuBorderSize() { return 5; }
int LookAndFeel::getMenuWindowFlags() { return 0; }   // no rectangular native shadow around the rounded menu

//==============================================================================
// Buttons
juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (hasFlag (button, kPropChip))
        return font (juce::jmin (11.5f, (float) buttonHeight * 0.55f), 600);

    return font (juce::jmin (13.0f, (float) buttonHeight * 0.5f), 600);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                        bool highlighted, bool down)
{
    const auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool chip = hasFlag (button, kPropChip);
    const bool ghost = hasFlag (button, kPropGhost);
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();
    const float alpha = enabled ? 1.0f : 0.45f;
    const float radius = chip ? b.getHeight() * 0.5f : juce::jmin (6.0f, b.getHeight() * 0.5f);
    const auto path = roundedRectPath (b, radius, button);

    highlighted = highlighted && enabled;
    down = down && enabled;

    juce::Colour fill;

    if (on)
        fill = Colours::panelRaised.interpolatedWith (Colours::accent, down ? 0.30f : (highlighted ? 0.24f : 0.17f));
    else if (ghost)
        fill = (down || highlighted) ? Colours::panelHover.withAlpha (down ? 1.0f : 0.75f) : juce::Colours::transparentBlack;
    else
        fill = backgroundColour.brighter (highlighted ? 0.07f : 0.0f).darker (down ? 0.12f : 0.0f);

    if (! fill.isTransparent())
    {
        g.setGradientFill (juce::ColourGradient::vertical (fill.brighter (0.04f).withMultipliedAlpha (alpha), b.getY(),
                                                           fill.darker (0.05f).withMultipliedAlpha (alpha), b.getBottom()));
        g.fillPath (path);
    }

    juce::Colour line;

    if (on)
        line = Colours::accent.withAlpha (highlighted ? 0.7f : 0.5f);
    else if (ghost)
        line = juce::Colours::transparentBlack;
    else
        line = Colours::outline.brighter (highlighted ? 0.18f : 0.0f);

    if (! line.isTransparent())
    {
        g.setColour (line.withMultipliedAlpha (alpha));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool)
{
    const bool on = button.getToggleState();
    auto colour = button.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.4f);
    else if (highlighted && ! on)
        colour = colour.brighter (0.2f);

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (colour);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.85f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto b = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();
    const float alpha = enabled ? 1.0f : 0.4f;
    highlighted = highlighted && enabled;

    const float trackH = juce::jlimit (10.0f, 16.0f, b.getHeight() - 4.0f);
    const float trackW = std::round (trackH * 1.8f);
    const auto track = juce::Rectangle<float> (b.getX() + 1.0f, b.getCentreY() - trackH * 0.5f, trackW, trackH);
    const float r = trackH * 0.5f;

    if (on)
    {
        g.setGradientFill (juce::ColourGradient::vertical (Colours::accent.brighter (highlighted ? 0.15f : 0.05f).withMultipliedAlpha (alpha),
                                                           track.getY(),
                                                           Colours::accent.darker (0.12f).withMultipliedAlpha (alpha),
                                                           track.getBottom()));
        g.fillRoundedRectangle (track, r);
    }
    else
    {
        g.setColour (Colours::background.brighter (0.06f).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (track, r);
        g.setColour (Colours::outline.brighter (highlighted ? 0.25f : 0.08f).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (track.reduced (0.5f), r - 0.5f, 1.0f);
    }

    const float knobD = trackH - 4.0f;
    const float knobX = on ? track.getRight() - 2.0f - knobD : track.getX() + 2.0f;
    const auto knob = juce::Rectangle<float> (knobX, track.getCentreY() - knobD * 0.5f, knobD, knobD);

    if (on)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f * alpha));
        g.fillEllipse (knob.translated (0.0f, 0.7f));
    }

    g.setColour ((on ? juce::Colour (0xfff6fafb) : Colours::textDim.brighter (highlighted ? 0.2f : 0.0f)).withMultipliedAlpha (alpha));
    g.fillEllipse (knob);

    const auto text = button.getButtonText();

    if (text.isNotEmpty())
    {
        auto textArea = b.withTrimmedLeft (track.getRight() + 8.0f - b.getX());
        g.setFont (font (juce::jmin (12.5f, b.getHeight() * 0.6f)));
        g.setColour ((on || highlighted ? Colours::text : Colours::textDim).withMultipliedAlpha (alpha));
        g.drawFittedText (text, textArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    }
}

void LookAndFeel::changeToggleButtonWidthToFitText (juce::ToggleButton& button)
{
    const float h = (float) button.getHeight();
    const float trackH = juce::jlimit (10.0f, 16.0f, h - 4.0f);
    const float w = std::round (trackH * 1.8f) + 1.0f + 8.0f
                    + textWidth (font (juce::jmin (12.5f, h * 0.6f)), button.getButtonText()) + 4.0f;
    button.setSize ((int) std::ceil (w), button.getHeight());
}

//==============================================================================
// Sliders
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area = bounds.withSizeKeepingCentre (size, size).reduced (1.5f);
    const auto centre = area.getCentre();
    const float radius = area.getWidth() * 0.5f;
    const bool enabled = slider.isEnabled();
    const bool hover = enabled && slider.isMouseOverOrDragging();
    const float alpha = enabled ? 1.0f : 0.45f;

    const float lineW = juce::jlimit (2.0f, 3.5f, radius * 0.13f);
    const float arcR = radius - lineW * 0.5f;
    const float toAngle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::outline.brighter (hover ? 0.15f : 0.0f).withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, toAngle, true);
        g.setColour (Colours::accent.brighter (hover ? 0.12f : 0.0f).withMultipliedAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float bodyR = arcR - lineW * 0.5f - juce::jmax (2.5f, radius * 0.12f);

    if (bodyR > 3.0f)
    {
        const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.25f * alpha));
        g.fillEllipse (body.translated (0.0f, 1.0f));
        const auto top = (hover ? Colours::panelHover.brighter (0.08f) : Colours::panelHover);
        g.setGradientFill (juce::ColourGradient::vertical (top.withMultipliedAlpha (alpha), body.getY(),
                                                           Colours::panelRaised.darker (0.1f).withMultipliedAlpha (alpha),
                                                           body.getBottom()));
        g.fillEllipse (body);
        g.setColour (juce::Colours::white.withAlpha (0.06f * alpha));
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        const float dotR = juce::jlimit (1.4f, 2.6f, bodyR * 0.14f);
        const auto dot = centre.getPointOnCircumference (bodyR - dotR - juce::jmax (2.0f, bodyR * 0.16f), toAngle);
        g.setColour ((hover ? Colours::accent.brighter (0.3f) : Colours::text).withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (dot));
    }
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                    juce::Slider& slider)
{
    if (slider.isBar() || slider.isTwoValue() || slider.isThreeValue())
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const bool horizontal = slider.isHorizontal();
    const bool enabled = slider.isEnabled();
    const bool hover = enabled && slider.isMouseOverOrDragging();
    const float alpha = enabled ? 1.0f : 0.45f;
    const float trackW = 4.0f;

    const juce::Point<float> start (horizontal ? (float) x : (float) x + (float) width * 0.5f,
                                    horizontal ? (float) y + (float) height * 0.5f : (float) (y + height));
    const juce::Point<float> end (horizontal ? (float) (x + width) : start.x,
                                  horizontal ? start.y : (float) y);
    const juce::Point<float> thumb (horizontal ? sliderPos : start.x, horizontal ? start.y : sliderPos);

    juce::Path track;
    track.startNewSubPath (start);
    track.lineTo (end);
    g.setColour (Colours::outline.brighter (hover ? 0.12f : 0.0f).withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.startNewSubPath (start);
    value.lineTo (thumb);
    g.setColour (Colours::accent.withMultipliedAlpha (alpha));
    g.strokePath (value, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float d = (float) getSliderThumbRadius (slider) * 2.0f + (hover ? 1.0f : 0.0f);
    const auto thumbRect = juce::Rectangle<float> (d, d).withCentre (thumb);
    g.setColour (juce::Colours::black.withAlpha (0.3f * alpha));
    g.fillEllipse (thumbRect.translated (0.0f, 1.0f));
    g.setColour (juce::Colour (0xfff2f6f8).withMultipliedAlpha (alpha));
    g.fillEllipse (thumbRect);
    g.setColour (Colours::accent.withMultipliedAlpha (alpha * (hover ? 1.0f : 0.0f)));
    g.drawEllipse (thumbRect.reduced (0.5f), 1.0f);
}

int LookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.isRotary() ? 0 : 6;
}

juce::Font LookAndFeel::getSliderPopupFont (juce::Slider&) { return font (12.0f, 600); }
int LookAndFeel::getSliderPopupPlacement (juce::Slider&) { return juce::BubbleComponent::below; }

void LookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                              const juce::Rectangle<float>& body)
{
    g.setColour (tooltipFill);
    g.fillRoundedRectangle (body, 6.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (body.reduced (0.5f), 6.0f, 1.0f);
}

//==============================================================================
juce::Font LookAndFeel::getLabelFont (juce::Label& label) { return label.getFont(); }

//==============================================================================
// Tooltips
juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                    juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltipText (tipText, Colours::text);
    const int w = (int) std::ceil (layout.getWidth()) + 22;
    const int h = (int) std::ceil (layout.getHeight()) + 14;

    const int x = screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 10) : screenPos.x + 14;
    const int y = screenPos.y > parentArea.getBottom() - (h + 40) ? screenPos.y - (h + 10) : screenPos.y + 20;

    return juce::Rectangle<int> (x, y, w, h).constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto b = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (tooltipFill.withAlpha (0.97f));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (Colours::outline.brighter (0.1f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

    layoutTooltipText (text, Colours::text).draw (g, b.reduced (11.0f, 7.0f));
}

//==============================================================================
int LookAndFeel::getDefaultScrollbarWidth() { return 10; }

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                 bool vertical, int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    juce::Rectangle<float> thumb;

    if (vertical)
        thumb = { (float) x + (float) width * 0.5f - 2.5f, (float) thumbStart + 2.0f, 5.0f, (float) thumbSize - 4.0f };
    else
        thumb = { (float) thumbStart + 2.0f, (float) y + (float) height * 0.5f - 2.5f, (float) thumbSize - 4.0f, 5.0f };

    g.setColour (Colours::textDim.withAlpha (isMouseDown ? 0.7f : (isMouseOver ? 0.55f : 0.32f)));
    g.fillRoundedRectangle (thumb, 2.5f);
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour ((isMouseOver || isMouseDragging ? Colours::textDim : Colours::textFaint).withAlpha (0.9f));

    for (int i = 0; i < 3; ++i)
        for (int j = 0; j <= i; ++j)
        {
            const float px = (float) w - 5.0f - (float) (i - j) * 4.0f;
            const float py = (float) h - 5.0f - (float) j * 4.0f;
            g.fillEllipse (px - 1.1f, py - 1.1f, 2.2f, 2.2f);
        }
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), 5.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), 5.0f, 1.0f);
}

} // namespace dk::ui
