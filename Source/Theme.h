// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Encore's look on the desktop: the same warm near-black surfaces, paper text
// and accent colours as encoremusic.studio (design-tokens.css), so moving
// between the website and Basement feels like one product.
#pragma once

namespace theme
{
    using juce::Colour;
    inline const Colour base     { 0xff0c0a09 }; // --bg-base
    inline const Colour subtle   { 0xff110e0d }; // --bg-subtle (timeline lanes)
    inline const Colour surface  { 0xff171412 }; // --bg-surface (headers, bars)
    inline const Colour raised   { 0xff221e1b }; // --bg-surface-hover (buttons, fields)
    inline const Colour strong   { 0xff2c2723 }; // --bg-surface-strong (dividers, tracks)
    inline const Colour paper    { 0xfff2efe8 }; // --text-primary
    inline const Colour text2    { 0xffbdb4a9 }; // --text-secondary
    inline const Colour muted    { 0xff91887d }; // --text-muted
    inline const Colour brand    { 0xffff3b00 }; // --brand-primary: actions only (Record, Export)
    inline const Colour accent   { 0xff22d3ee }; // --brand-accent (waveforms)
    inline const Colour accent2  { 0xffec4899 }; // --brand-accent-2
    inline const Colour solo     { 0xffeab308 };
    inline const Colour record   { 0xffef4444 };

    /** Track colours, picked in order as tracks are added */
    inline Colour trackColour (int i)
    {
        static const juce::uint32 c[] = { 0xff22d3ee, 0xffec4899, 0xffa3e635, 0xfff59e0b, 0xff818cf8, 0xff2dd4bf, 0xfffb7185, 0xffc084fc };
        return Colour (c[(size_t) std::abs (i) % (sizeof (c) / sizeof (c[0]))]);
    }
}

class BasementLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BasementLookAndFeel()
        : LookAndFeel_V4 (LookAndFeel_V4::ColourScheme (theme::base, theme::surface, theme::raised, theme::strong,
                                                        theme::paper, theme::raised, theme::paper, theme::brand, theme::paper))
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, theme::base);
        setColour (TextButton::buttonColourId, theme::raised);
        setColour (TextButton::buttonOnColourId, theme::paper);
        setColour (TextButton::textColourOffId, theme::paper);
        setColour (TextButton::textColourOnId, theme::base);
        setColour (Label::textColourId, theme::paper);
        setColour (Slider::thumbColourId, theme::paper);
        setColour (Slider::trackColourId, theme::text2);
        setColour (Slider::backgroundColourId, theme::strong);
        setColour (Slider::rotarySliderFillColourId, theme::text2);
        setColour (Slider::rotarySliderOutlineColourId, theme::strong);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (PopupMenu::backgroundColourId, theme::surface);
        setColour (PopupMenu::highlightedBackgroundColourId, theme::strong);
        setColour (PopupMenu::textColourId, theme::paper);
        setColour (PopupMenu::highlightedTextColourId, theme::paper);
        setColour (ComboBox::backgroundColourId, theme::raised);
        setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        setColour (TextEditor::backgroundColourId, theme::raised);
        setColour (TextEditor::outlineColourId, Colours::transparentBlack);
        setColour (TextEditor::focusedOutlineColourId, theme::text2);
        setColour (ScrollBar::thumbColourId, theme::strong);
        setColour (ListBox::backgroundColourId, theme::surface);
        setColour (AlertWindow::backgroundColourId, theme::surface);
        setColour (AlertWindow::textColourId, theme::paper);
        setColour (TooltipWindow::backgroundColourId, theme::strong);
        setColour (TooltipWindow::textColourId, theme::paper);
    }

    // Pill buttons, like the website's
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);
        else if (down) c = c.brighter (0.15f);
        else if (over) c = c.brighter (0.08f);
        g.setColour (c);
        g.fillRoundedRectangle (r, juce::jmin (r.getHeight() * 0.5f, 999.0f));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::FontOptions (juce::jmin (14.0f, (float) h * 0.5f), juce::Font::bold);
    }
};
