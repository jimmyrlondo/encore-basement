// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The Hub: the first thing you see. Jump back into a recent song, or set up a
// new one properly (title, artist, tempo, key, which interface you're recording
// from, how many tracks and which input feeds each one) so the song's details
// are right from the start.
#pragma once

#include "SongInfo.h"

namespace hubui
{
    inline juce::Font font (float h, bool bold = false) { return juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain); }

    inline void styleLabel (juce::Label& l, float h = 13.0f, juce::Colour c = theme::text2, bool bold = false)
    {
        l.setFont (font (h, bold));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::centredLeft);
        l.setInterceptsMouseClicks (false, false);
    }

    inline void styleEditor (juce::TextEditor& e, const juce::String& hint = {})
    {
        e.setFont (font (14.0f));
        e.setIndents (10, 7);
        e.setTextToShowWhenEmpty (hint, theme::muted);
        e.setColour (juce::TextEditor::textColourId, theme::paper);
        e.setColour (juce::CaretComponent::caretColourId, theme::paper);
        e.setColour (juce::TextEditor::highlightColourId, theme::brand.withAlpha (0.35f));
    }

    inline void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::textColourId, theme::paper);
        c.setColour (juce::ComboBox::arrowColourId, theme::text2);
    }

    /** A small uppercase heading over each part of the form */
    inline void drawHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r)
    {
        g.setColour (theme::muted);
        g.setFont (font (11.0f, true));
        g.drawText (text.toUpperCase(), r, juce::Justification::bottomLeft);
    }

    inline juce::String timeAgo (juce::Time t)
    {
        if (t.toMilliseconds() <= 0) return {};
        auto now = juce::Time::getCurrentTime();
        auto days = (int) ((now.toMilliseconds() - t.toMilliseconds()) / 86400000);
        if (t.getDayOfMonth() == now.getDayOfMonth() && days < 1) return "Today " + t.formatted ("%H:%M");
        if (days < 2) return "Yesterday " + t.formatted ("%H:%M");
        if (days < 7) return t.formatted ("%A");
        return t.formatted (t.getYear() == now.getYear() ? "%b %d" : "%b %d, %Y");
    }

    inline juce::String lengthText (double s)
    {
        if (s <= 0) return "empty";
        return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2);
    }

    inline juce::StringArray keys()
    {
        juce::StringArray k;
        for (auto n : { "C", "C#/Db", "D", "Eb", "E", "F", "F#/Gb", "G", "Ab", "A", "Bb", "B" })
            for (auto m : { " major", " minor" }) k.add (juce::String (n) + m);
        return k;
    }

    inline juce::StringArray genres()
    {
        return { "Rock", "Alternative", "Indie", "Punk", "Metal", "Pop", "Hip-Hop", "R&B", "Soul", "Funk", "Country", "Americana", "Folk",
                 "Blues", "Jazz", "Gospel", "Electronic", "Ambient", "Latin", "Reggae", "Polka", "Classical", "Soundtrack", "Spoken word", "Podcast" };
    }
}

//==============================================================================
/** Title, artist, album, genre, tempo, time signature, key and notes.
    Used by the New song form and by Project > Song Setup. */
class SongFields : public juce::Component
{
public:
    std::function<void()> onChange;

    SongFields()
    {
        for (auto* l : { &titleL, &artistL, &albumL, &genreL, &tempoL, &sigL, &keyL, &notesL, &bpmUnit })
        {
            hubui::styleLabel (*l);
            addAndMakeVisible (l);
        }
        hubui::styleEditor (title, "Untitled");
        hubui::styleEditor (artist, "Who it's by");
        hubui::styleEditor (album, "Optional");
        hubui::styleEditor (bpm);
        hubui::styleEditor (notes, "Lyrics, ideas, who played what...");
        title.setFont (hubui::font (16.0f, true));
        bpm.setInputRestrictions (6, "0123456789.");
        bpm.setSelectAllWhenFocused (true);
        bpm.onFocusLost = [this]
        {
            auto v = bpm.getText().getDoubleValue();
            SongInfo i;
            i.bpm = juce::jlimit (20.0, 400.0, v > 0 ? v : 120.0);
            if (bpm.getText() != i.bpmText()) { bpm.setText (i.bpmText(), false); if (onChange) onChange(); }
        };
        bpm.setTooltip ("20 to 400 BPM");
        notes.setMultiLine (true, true);
        notes.setReturnKeyStartsNewLine (true);
        for (auto* e : { &title, &artist, &album, &bpm, &notes })
        {
            addAndMakeVisible (e);
            e->onTextChange = [this] { if (onChange) onChange(); };
        }

        int id = 1;
        for (auto s : { "4/4", "3/4", "6/8", "2/4", "5/4", "7/8", "9/8", "12/8" }) sig.addItem (s, id++);
        key.addItem ("Not set", 1);
        key.addItemList (hubui::keys(), 2);
        for (auto* c : { &sig, &key })
        {
            hubui::styleCombo (*c);
            addAndMakeVisible (c);
            c->onChange = [this] { if (onChange) onChange(); };
        }
        hubui::styleEditor (genre.getEditor(), "Pick or type one");
        genre.onChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible (genre);
        set ({});
    }

    void set (const SongInfo& i)
    {
        title.setText (i.title, false);
        artist.setText (i.artist, false);
        album.setText (i.album, false);
        genre.setText (i.genre);
        bpm.setText (i.bpmText(), false);
        sig.setText (i.sigText(), juce::dontSendNotification);
        if (sig.getSelectedId() == 0) sig.setText (i.sigText(), juce::dontSendNotification);
        key.setSelectedId (juce::jmax (1, hubui::keys().indexOf (i.key) + 2), juce::dontSendNotification);
        notes.setText (i.notes, false);
    }

    /** Fills in the fields this form owns; everything else in base is kept */
    SongInfo get (SongInfo base = {}) const
    {
        base.title = title.getText().trim().isEmpty() ? juce::String ("Untitled") : title.getText().trim();
        base.artist = artist.getText().trim();
        base.album = album.getText().trim();
        base.genre = genre.getText().trim();
        base.notes = notes.getText();
        base.key = key.getSelectedId() > 1 ? key.getText() : juce::String();
        auto b = bpm.getText().getDoubleValue();
        base.bpm = b > 0 ? juce::jlimit (20.0, 400.0, b) : 120.0;
        auto s = juce::StringArray::fromTokens (sig.getText(), "/", {});
        if (s.size() == 2 && s[0].getIntValue() > 0 && s[1].getIntValue() > 0) { base.sigNum = s[0].getIntValue(); base.sigDen = s[1].getIntValue(); }
        return base;
    }

    static constexpr int rowH = 32, gap = 10, labelW = 112, notesH = 76;
    static int heightFor() { return 5 * (rowH + gap) + notesH; }

    void resized() override
    {
        auto r = getLocalBounds();
        auto row = [&] (juce::Label& l, int h = rowH) { auto x = r.removeFromTop (h); r.removeFromTop (gap); l.setBounds (x.removeFromLeft (labelW).withHeight (rowH)); return x; };
        title.setBounds (row (titleL));
        artist.setBounds (row (artistL));
        album.setBounds (row (albumL));
        genre.setBounds (row (genreL).removeFromLeft (340));
        auto t = row (tempoL);
        bpm.setBounds (t.removeFromLeft (70));
        bpmUnit.setBounds (t.removeFromLeft (44).withTrimmedLeft (8));
        sigL.setBounds (t.removeFromLeft (96).withTrimmedLeft (12));
        sig.setBounds (t.removeFromLeft (84));
        keyL.setBounds (t.removeFromLeft (52).withTrimmedLeft (16));
        key.setBounds (t.removeFromLeft (150));
        notes.setBounds (row (notesL, notesH));
    }

    juce::TextEditor title, artist, album, bpm, notes;

private:
    genres::Field genre { false };
    juce::ComboBox sig, key;
public:
    /** The artist's own genres, at the top of the genre list */
    void setGenreSuggestions (const juce::StringArray& s) { genre.suggestions = s; genre.suggestions.trim(); genre.suggestions.removeEmptyStrings(); }
private:
    juce::Label titleL { {}, "Song title" }, artistL { {}, "Artist" }, albumL { {}, "Album / EP" }, genreL { {}, "Genre" },
                tempoL { {}, "Tempo" }, sigL { {}, "Time sig." }, keyL { {}, "Key" }, notesL { {}, "Notes" }, bpmUnit { {}, "BPM" };
};

//==============================================================================
/** The big tab buttons along the top of the Hub (like a DAW's start page) */
class HubTab : public juce::Button
{
public:
    enum class Icon { recent, add };
    HubTab (const juce::String& name, Icon i) : Button (name), icon (i) { setClickingTogglesState (false); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();
        g.setColour (on ? theme::paper : (over ? theme::raised.brighter (0.05f) : theme::raised));
        if (down) g.setColour (on ? theme::paper.darker (0.1f) : theme::strong);
        g.fillRoundedRectangle (r, 10.0f);
        auto ink = on ? theme::base : theme::paper;

        auto iconArea = r.removeFromTop (r.getHeight() * 0.62f).withSizeKeepingCentre (26.0f, 26.0f);
        g.setColour (ink);
        if (icon == Icon::recent)
        {
            g.drawEllipse (iconArea.reduced (1.5f), 2.0f);
            auto c = iconArea.getCentre();
            g.drawLine (c.x, c.y, c.x, c.y - 8.0f, 2.0f);
            g.drawLine (c.x, c.y, c.x + 6.0f, c.y + 3.0f, 2.0f);
        }
        else
        {
            g.drawRoundedRectangle (iconArea.reduced (1.5f), 5.0f, 2.0f);
            auto c = iconArea.getCentre();
            g.drawLine (c.x - 6.0f, c.y, c.x + 6.0f, c.y, 2.0f);
            g.drawLine (c.x, c.y - 6.0f, c.x, c.y + 6.0f, 2.0f);
        }
        g.setFont (hubui::font (13.0f, true));
        g.drawText (getName(), r.toNearestInt(), juce::Justification::centredTop);
    }

private:
    Icon icon;
};

//==============================================================================
/** Recent songs, newest first, with a search box */
class RecentPanel : public juce::Component,
                    private juce::ListBoxModel
{
public:
    std::function<void (juce::File)> onOpen;
    std::function<void()> onOpenOther, onNew;
    std::function<juce::PropertiesFile&()> props;

    RecentPanel()
    {
        hubui::styleEditor (search, "Search by title, artist or album");
        search.onTextChange = [this] { filter(); };
        addAndMakeVisible (search);
        list.setModel (this);
        list.setRowHeight (68);
        list.setColour (juce::ListBox::backgroundColourId, theme::surface);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);

        openButton.setColour (juce::TextButton::buttonColourId, theme::brand);
        for (auto* b : { &openButton, &otherButton, &revealButton, &removeButton })
            addAndMakeVisible (b);
        openButton.onClick = [this] { openSelected(); };
        otherButton.onClick = [this] { if (onOpenOther) onOpenOther(); };
        revealButton.onClick = [this] { if (auto* e = selected()) (e->file.existsAsFile() ? e->file : e->file.getParentDirectory()).revealToUser(); };
        removeButton.onClick = [this]
        {
            if (auto* e = selected()) { recent::forget (props(), e->file); refresh(); }
        };
        revealButton.setTooltip ("Show the song's folder in Explorer");
        removeButton.setTooltip ("Take it off this list (the song itself stays where it is)");

        newButton.setColour (juce::TextButton::buttonColourId, theme::brand);
        newButton.onClick = [this] { if (onNew) onNew(); };
        addChildComponent (newButton);
    }

    void refresh()
    {
        all = recent::load (props());
        filter();
    }

    int count() const { return all.size(); }

    void paint (juce::Graphics& g) override
    {
        if (all.isEmpty())
        {
            auto r = list.getBounds().withSizeKeepingCentre (420, 120);
            g.setColour (theme::paper);
            g.setFont (hubui::font (18.0f, true));
            g.drawText ("No songs yet", r.removeFromTop (28), juce::Justification::centred);
            g.setColour (theme::text2);
            g.setFont (hubui::font (14.0f));
            g.drawFittedText ("Start a new song, or open one you already have.", r.removeFromTop (40), juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        search.setBounds (r.removeFromTop (36));
        r.removeFromTop (12);
        auto bottom = r.removeFromBottom (36);
        r.removeFromBottom (12);
        list.setBounds (r);
        openButton.setBounds (bottom.removeFromRight (110));
        bottom.removeFromRight (8);
        otherButton.setBounds (bottom.removeFromRight (130));
        removeButton.setBounds (bottom.removeFromLeft (150));
        bottom.removeFromLeft (8);
        revealButton.setBounds (bottom.removeFromLeft (130));
        newButton.setBounds (list.getBounds().withSizeKeepingCentre (160, 38).translated (0, 70));
    }

private:
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton openButton { "Open" }, otherButton { "Open other..." }, revealButton { "Show in folder" },
                     removeButton { "Remove from list" }, newButton { "New song" };
    juce::Array<recent::Entry> all, shown;

    const recent::Entry* selected() const
    {
        auto row = list.getSelectedRow();
        return juce::isPositiveAndBelow (row, shown.size()) ? &shown.getReference (row) : nullptr;
    }

    void openSelected()
    {
        if (auto* e = selected())
        {
            if (e->file.existsAsFile()) { if (onOpen) onOpen (e->file); }
            else
            {
                auto f = e->file;
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Song not found",
                                                    f.getFullPathName() + "\n\nIt may have been moved, renamed or be on a drive that isn't plugged in. Take it off the list?",
                                                    "Remove from list", "Keep it", this,
                                                    juce::ModalCallbackFunction::create ([this, f] (int r) { if (r == 1) { recent::forget (props(), f); refresh(); } }));
            }
        }
    }

    void filter()
    {
        auto q = search.getText().trim();
        shown.clear();
        for (auto& e : all)
            if (q.isEmpty() || e.title.containsIgnoreCase (q) || e.artist.containsIgnoreCase (q) || e.album.containsIgnoreCase (q))
                shown.add (e);
        list.updateContent();
        list.repaint();
        if (! shown.isEmpty() && list.getSelectedRow() < 0) list.selectRow (0);
        updateButtons();
        newButton.setVisible (all.isEmpty());
        search.setVisible (! all.isEmpty());
        repaint();
    }

    void updateButtons()
    {
        const bool any = selected() != nullptr;
        for (auto* b : { &openButton, &revealButton, &removeButton }) b->setEnabled (any);
    }

    int getNumRows() override { return shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected) override
    {
        if (! juce::isPositiveAndBelow (row, shown.size())) return;
        auto& e = shown.getReference (row);
        const bool missing = ! e.file.existsAsFile();
        auto r = juce::Rectangle<int> (0, 0, w, h).reduced (4, 3);
        if (isSelected) { g.setColour (theme::strong); g.fillRoundedRectangle (r.toFloat(), 8.0f); }
        r.reduce (12, 8);

        auto right = r.removeFromRight (juce::jmin (260, w / 3));
        g.setColour (theme::text2);
        g.setFont (hubui::font (13.0f));
        g.drawText (missing ? juce::String ("Missing") : hubui::timeAgo (e.saved), right.removeFromTop (18), juce::Justification::topRight);
        juce::StringArray facts;
        facts.add (juce::String (e.tracks) + (e.tracks == 1 ? " track" : " tracks"));
        facts.add (hubui::lengthText (e.lengthSeconds));
        if (e.bpm > 0) facts.add (juce::String (e.bpm, 0) + " BPM");
        if (e.sig.isNotEmpty()) facts.add (e.sig);
        if (e.key.isNotEmpty()) facts.add (e.key);
        g.setColour (theme::muted);
        g.setFont (hubui::font (12.0f));
        g.drawText (facts.joinIntoString ("  ·  "), right.removeFromTop (18), juce::Justification::topRight);
        if (e.format.isNotEmpty()) g.drawText (e.format, right.removeFromTop (16), juce::Justification::topRight);

        r.removeFromRight (16);
        g.setColour (missing ? theme::muted : theme::paper);
        g.setFont (hubui::font (16.0f, true));
        g.drawText (e.title, r.removeFromTop (20), juce::Justification::topLeft, true);
        juce::StringArray who;
        if (e.artist.isNotEmpty()) who.add (e.artist);
        if (e.album.isNotEmpty()) who.add (e.album);
        g.setColour (theme::text2);
        g.setFont (hubui::font (13.0f));
        g.drawText (who.isEmpty() ? juce::String ("No artist yet") : who.joinIntoString ("  ·  "), r.removeFromTop (18), juce::Justification::topLeft, true);
        g.setColour (theme::muted);
        g.setFont (hubui::font (11.5f));
        g.drawText (e.file.getFullPathName(), r.removeFromTop (16), juce::Justification::topLeft, true);
    }

    void selectedRowsChanged (int) override { updateButtons(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { openSelected(); }
    void returnKeyPressed (int) override { openSelected(); }
};

//==============================================================================
/** One track in the New song form: its name and the input that feeds it */
class TrackPlanRow : public juce::Component
{
public:
    TrackPlanRow (int index) : i (index)
    {
        hubui::styleEditor (name);
        hubui::styleCombo (input);
        addAndMakeVisible (name);
        addAndMakeVisible (input);
    }

    void setInputs (const juce::StringArray& inputs, const juce::String& want)
    {
        input.clear (juce::dontSendNotification);
        input.addItem ("No input", 1);
        input.addItemList (inputs, 2);
        auto idx = inputs.indexOf (want);
        input.setSelectedId (idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
    }

    juce::String chosenInput() const { return input.getSelectedId() > 1 ? input.getText() : juce::String(); }

    juce::String group;   // the group track this one plays into (from the template), or empty

    void setGroup (const juce::String& g)
    {
        group = g;
        name.setTooltip (g.isEmpty() ? juce::String() : "Plays into the group \"" + g + "\"");
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (theme::muted);
        g.setFont (hubui::font (12.0f, true));
        g.drawText (juce::String (i + 1), r.removeFromLeft (26), juce::Justification::centredRight);
        g.setColour (theme::trackColour (i));
        g.fillEllipse (r.removeFromLeft (30).toFloat().withSizeKeepingCentre (10.0f, 10.0f));
    }

    /** The group's name, tucked inside the right end of the name box */
    void paintOverChildren (juce::Graphics& g) override
    {
        if (group.isEmpty()) return;
        auto tag = name.getBounds().removeFromRight (juce::jmin (110, name.getWidth() / 3)).reduced (4, 6);
        g.setColour (theme::strong);
        g.fillRoundedRectangle (tag.toFloat(), 4.0f);
        g.setColour (theme::text2);
        g.setFont (hubui::font (11.0f, true));
        g.drawText (group, tag, juce::Justification::centred, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (56);
        input.setBounds (r.removeFromRight (juce::jmin (260, r.getWidth() / 2)));
        r.removeFromRight (8);
        name.setBounds (r);
    }

    juce::TextEditor name;
    juce::ComboBox input;

private:
    int i;
};

//==============================================================================
/** Starting points: a tile per template */
class TemplateTile : public juce::Button
{
public:
    TemplateTile (const templates::Template& tt) : Button (tt.name), t (tt) { setClickingTogglesState (false); }

    int inputsAvailable = 0;   // on the chosen interface, for "fits your interface"


    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (getToggleState() ? theme::strong : (over ? theme::raised.brighter (0.05f) : theme::raised));
        g.fillRoundedRectangle (r, 8.0f);
        if (getToggleState())
        {
            g.setColour (theme::brand);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.5f);
        }
        auto tr = getLocalBounds().reduced (12, 8);
        g.setColour (theme::paper);
        g.setFont (hubui::font (14.0f, true));
        g.drawText (t.name, tr.removeFromTop (20), juce::Justification::centredLeft, true);
        if (t.needsInputs > 0 && inputsAvailable > 0)
        {
            const bool fits = inputsAvailable >= t.needsInputs;
            g.setColour (fits ? theme::accent : theme::solo);
            g.setFont (hubui::font (11.0f, true));
            g.drawText (fits ? "Fits your " + juce::String (inputsAvailable) + " inputs"
                             : "Needs " + juce::String (t.needsInputs) + " inputs, yours has " + juce::String (inputsAvailable),
                        tr.removeFromBottom (14), juce::Justification::bottomLeft, true);
        }
        g.setColour (theme::muted);
        g.setFont (hubui::font (12.0f));
        g.drawFittedText (t.description, tr, juce::Justification::topLeft, 2);
    }

    templates::Template t;
};

//==============================================================================
class NewSongPanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    std::function<void (SongPlan)> onCreate;
    std::function<void()> onAudioSetup;
    std::function<juce::PropertiesFile&()> props;

    NewSongPanel (te::Engine& e) : engine (e)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (10);
        addAndMakeVisible (viewport);

        content.addAndMakeVisible (fields);
        fields.onChange = [this] { updateSummary(); };

        for (auto* l : { &interfaceL, &formatL, &tracksL, &inputsNote, &whereL, &wherePath })
        {
            hubui::styleLabel (*l);
            content.addAndMakeVisible (l);
        }
        hubui::styleLabel (wherePath, 13.0f, theme::paper);
        hubui::styleLabel (inputsNote, 12.0f, theme::muted);
        for (auto* c : { &interfaceBox, &rateBox, &bitsBox })
        {
            hubui::styleCombo (*c);
            content.addAndMakeVisible (c);
        }
        bitsBox.addItem ("16-bit", 16);
        bitsBox.addItem ("24-bit (recommended)", 24);
        bitsBox.addItem ("32-bit float", 32);
        bitsBox.setSelectedId (24, juce::dontSendNotification);
        bitsBox.onChange = [this] { updateSummary(); };
        interfaceBox.onChange = [this] { applyInterface(); };
        rateBox.onChange = [this] { applyRate(); };

        hubui::styleEditor (countBox);
        countBox.setJustification (juce::Justification::centred);
        countBox.setInputRestrictions (2, "0123456789");
        countBox.setSelectAllWhenFocused (true);
        countBox.onTextChange = [this] { setTrackCount (countBox.getText().getIntValue(), false); };
        for (auto* b : { &minus, &plus, &audioSetup, &changeWhere })
            content.addAndMakeVisible (b);
        content.addAndMakeVisible (countBox);
        minus.onClick = [this] { setTrackCount (rows.size() - 1); };
        plus.onClick = [this] { setTrackCount (rows.size() + 1); };
        audioSetup.onClick = [this] { if (onAudioSetup) onAudioSetup(); };
        audioSetup.setTooltip ("Outputs, buffer size and the rest of your interface's settings");
        changeWhere.onClick = [this] { chooseWhere(); };

        create.setColour (juce::TextButton::buttonColourId, theme::brand);
        create.onClick = [this] { createSong(); };
        addAndMakeVisible (create);
        hubui::styleLabel (summary, 13.0f, theme::text2);
        addAndMakeVisible (summary);

        engine.getDeviceManager().addChangeListener (this);
    }

    ~NewSongPanel() override { engine.getDeviceManager().removeChangeListener (this); }

    /** Fresh form each time the tab opens */
    void reset()
    {
        SongInfo blank;
        blank.title = {};
        if (auto* a = account::get().artist()) { blank.artist = a->name; blank.genre = a->mainGenre(); }
        fields.setGenreSuggestions (account::get().artist() != nullptr ? juce::StringArray::fromTokens (account::get().artist()->genre, ",", {}) : juce::StringArray());
        changeWhere.setVisible (! account::get().signedIn());
        if (auto* d = engine.getDeviceManager().deviceManager.getCurrentAudioDevice()) blank.sampleRate = d->getCurrentSampleRate();
        fields.set (blank);
        buildTemplates();
        scanInterfaces();
        refreshDevice();
        pickTemplate (0);
        viewport.setViewPosition (0, 0);
        fields.title.grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::strong);
        g.fillRect (getLocalBounds().removeFromBottom (footerH).removeFromTop (1));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto foot = r.removeFromBottom (footerH).withTrimmedTop (12);
        create.setBounds (foot.removeFromRight (150).withSizeKeepingCentre (150, 38));
        foot.removeFromRight (16);
        summary.setBounds (foot);
        viewport.setBounds (r);
        layoutContent();
    }

private:
    static constexpr int footerH = 62, headingH = 34, rowH = 32, gap = 10;
    te::Engine& engine;
    juce::Viewport viewport;

    struct Content : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    } content;

    SongFields fields;
    juce::OwnedArray<TemplateTile> tiles;
    juce::OwnedArray<TrackPlanRow> rows;
    int templateIndex = 0;

    juce::ComboBox interfaceBox, rateBox, bitsBox;
    juce::TextEditor countBox;
    juce::TextButton minus { "-" }, plus { "+" }, audioSetup { "More audio settings..." }, changeWhere { "Change..." }, create { "Create song" };
    juce::Label interfaceL { {}, "Record from" }, formatL { {}, "Format" }, tracksL { {}, "Tracks" }, inputsNote, whereL { {}, "Folder" }, wherePath, summary;

    struct DeviceChoice { juce::String type, name; bool inputSide; };
    juce::Array<DeviceChoice> choices;
    juce::Array<juce::Rectangle<int>> headings;
    juce::StringArray headingText;

    //==============================================================================
    void layoutContent()
    {
        const int w = juce::jmax (420, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
        int y = 0;
        headings.clear(); headingText.clear();
        auto heading = [&] (const juce::String& t) { headings.add ({ 0, y, w, headingH - 10 }); headingText.add (t); y += headingH; };
        auto line = [&] (int h = rowH) { auto r = juce::Rectangle<int> (0, y, w, h); y += h + gap; return r; };

        // the song first, then where it starts from, with the tracks that gives right under it
        heading ("Song");
        fields.setBounds (0, y, w, SongFields::heightFor());
        y += SongFields::heightFor() + 8;

        heading ("Start from");
        const int cols = w > 760 ? 3 : 2, tileW = (w - (cols - 1) * 10) / cols, tileH = 62;
        for (int i = 0; i < tiles.size(); ++i)
            tiles[i]->setBounds ((i % cols) * (tileW + 10), y + (i / cols) * (tileH + 10), tileW, tileH);
        y += ((tiles.size() + cols - 1) / cols) * (tileH + 10) + 8;

        heading ("Recording");
        auto lw = SongFields::labelW;
        {
            auto r = line();
            interfaceL.setBounds (r.removeFromLeft (lw));
            audioSetup.setBounds (r.removeFromRight (170));
            r.removeFromRight (8);
            interfaceBox.setBounds (r);
        }
        {
            auto r = line();
            formatL.setBounds (r.removeFromLeft (lw));
            rateBox.setBounds (r.removeFromLeft (140));
            r.removeFromLeft (8);
            bitsBox.setBounds (r.removeFromLeft (190));
        }
        {
            auto r = line();
            tracksL.setBounds (r.removeFromLeft (lw));
            minus.setBounds (r.removeFromLeft (32));
            r.removeFromLeft (4);
            countBox.setBounds (r.removeFromLeft (52));
            r.removeFromLeft (4);
            plus.setBounds (r.removeFromLeft (32));
            r.removeFromLeft (14);
            inputsNote.setBounds (r);
        }
        for (auto* row : rows)
            row->setBounds (line (30).withTrimmedLeft (lw - 56));
        y += 8;

        heading ("Saved in");
        {
            auto r = line();
            whereL.setBounds (r.removeFromLeft (lw));
            changeWhere.setBounds (r.removeFromRight (100));
            r.removeFromRight (8);
            wherePath.setBounds (r);
        }
        y += 16;
        content.setSize (w, y);
        content.painter = [this] (juce::Graphics& g)
        {
            for (int i = 0; i < headings.size(); ++i) hubui::drawHeading (g, headingText[i], headings[i]);
        };
        content.repaint();
    }

    //==============================================================================
    // Templates
    void buildTemplates()
    {
        tiles.clear();
        for (auto& t : templates::all())
        {
            auto* tile = tiles.add (new TemplateTile (t));
            const int index = tiles.size() - 1;
            tile->onClick = [this, index] { pickTemplate (index); };
            content.addAndMakeVisible (tile);
        }
    }

    juce::StringArray inputNames() const
    {
        juce::StringArray names;
        auto& dm = engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto w = dm.getWaveInDevice (i))
                if (! w->isTrackDevice()) names.add (w->getName());
        return names;
    }

    void pickTemplate (int index)
    {
        if (! juce::isPositiveAndBelow (index, tiles.size())) return;
        templateIndex = index;
        for (int i = 0; i < tiles.size(); ++i) tiles[i]->setToggleState (i == index, juce::dontSendNotification);
        auto& t = tiles[index]->t;
        auto inputs = inputNames();
        juce::StringArray names = t.trackNames;
        if (t.allInputs) names = inputs.isEmpty() ? juce::StringArray { "Input 1", "Input 2" } : inputs;
        rows.clear();
        juce::StringArray used;
        for (int i = 0; i < names.size(); ++i)
        {
            auto in = t.allInputs ? inputs[i] : t.inputFor (i, inputs, used);
            used.add (in);
            addRow (names[i], in);
            rows.getLast()->setGroup (t.groups[i]);
        }
        const bool fixed = t.file.existsAsFile();
        minus.setEnabled (! fixed); plus.setEnabled (! fixed); countBox.setEnabled (! fixed);
        countBox.setText (juce::String (rows.size()), false);
        updateInputsNote();
        layoutContent();
        updateSummary();
    }

    /** Each row's input again (a new interface): the template's picks, else in order */
    void reapplyInputs()
    {
        auto inputs = inputNames();
        const templates::Template* t = juce::isPositiveAndBelow (templateIndex, tiles.size()) ? &tiles[templateIndex]->t : nullptr;
        juce::StringArray used;
        for (int r = 0; r < rows.size(); ++r)
        {
            auto in = t != nullptr && r < t->trackNames.size() && ! t->allInputs ? t->inputFor (r, inputs, used)
                                                                                  : (r < inputs.size() ? inputs[r] : juce::String());
            used.add (in);
            rows[r]->setInputs (inputs, in);
        }
    }

    void addRow (const juce::String& name, const juce::String& in)
    {
        auto* row = rows.add (new TrackPlanRow (rows.size()));
        row->name.setText (name, false);
        row->setInputs (inputNames(), in);
        row->input.onChange = [this] { updateSummary(); };
        content.addAndMakeVisible (row);
    }

    void setTrackCount (int n, bool updateBox = true)
    {
        n = juce::jlimit (0, 64, n);
        auto inputs = inputNames();
        // a new track gets the next input nobody else is using yet
        while (rows.size() < n)
        {
            juce::StringArray used;
            for (auto* r : rows) used.add (r->chosenInput());
            juce::String next;
            for (auto& in : inputs) if (! used.contains (in)) { next = in; break; }
            addRow ("Track " + juce::String (rows.size() + 1), next);
        }
        while (rows.size() > n) rows.removeLast();
        if (updateBox) countBox.setText (juce::String (rows.size()), false);
        updateInputsNote();
        layoutContent();
        updateSummary();
    }

    void updateInputsNote()
    {
        auto n = inputNames().size();
        inputsNote.setText (n == 0 ? "No inputs found: check Record from, or pick inputs later on each track"
                                   : juce::String (n) + (n == 1 ? " input" : " inputs") + " on this interface. Pick which one feeds each track:",
                            juce::dontSendNotification);
    }

    //==============================================================================
    // The interface you record from
    void scanInterfaces()
    {
        auto& dm = engine.getDeviceManager().deviceManager;
        choices.clear();
        interfaceBox.clear (juce::dontSendNotification);
        for (auto* type : dm.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            const bool separate = type->hasSeparateInputsAndOutputs();
            for (auto& n : type->getDeviceNames (separate))
            {
                choices.add ({ type->getTypeName(), n, separate });
                interfaceBox.addItem (n + "   (" + type->getTypeName() + ")", choices.size());
            }
        }
    }

    void refreshDevice()
    {
        auto& dm = engine.getDeviceManager().deviceManager;
        auto setup = dm.getAudioDeviceSetup();
        auto type = dm.getCurrentAudioDeviceType();
        for (int i = 0; i < choices.size(); ++i)
        {
            auto& c = choices.getReference (i);
            if (c.type == type && c.name == (c.inputSide ? setup.inputDeviceName : setup.outputDeviceName))
                interfaceBox.setSelectedId (i + 1, juce::dontSendNotification);
        }
        rateBox.clear (juce::dontSendNotification);
        if (auto* d = dm.getCurrentAudioDevice())
        {
            for (auto sr : d->getAvailableSampleRates())
            {
                auto khz = juce::String (sr / 1000.0, 1);
                if (khz.endsWith (".0")) khz = khz.dropLastCharacters (2);
                rateBox.addItem (khz + " kHz", juce::roundToInt (sr));
            }
            rateBox.setSelectedId (juce::roundToInt (d->getCurrentSampleRate()), juce::dontSendNotification);
        }
        else rateBox.setTextWhenNothingSelected ("No interface");

        // inputs may have changed: keep each row's choice if it still exists
        auto inputs = inputNames();
        for (auto* row : rows) row->setInputs (inputs, row->chosenInput().isNotEmpty() ? row->chosenInput() : juce::String());
        for (auto* tile : tiles) { tile->inputsAvailable = inputs.size(); tile->repaint(); }
        updateInputsNote();
        updateSummary();
    }

    void applyInterface()
    {
        auto i = interfaceBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (i, choices.size())) return;
        auto c = choices[i];
        auto& dm = engine.getDeviceManager().deviceManager;
        if (dm.getCurrentAudioDeviceType() != c.type) dm.setCurrentAudioDeviceType (c.type, true);
        auto setup = dm.getAudioDeviceSetup();
        if (c.inputSide) setup.inputDeviceName = c.name;
        else setup.inputDeviceName = setup.outputDeviceName = c.name;
        setup.useDefaultInputChannels = true;
        auto err = dm.setAudioDeviceSetup (setup, true);
        studioLog ("hub: record from " + c.name + " (" + c.type + ")" + (err.isEmpty() ? "" : " error: " + err));
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open " + c.name, err);
        // a fresh interface: give the tracks its inputs (the template's way, if it has one)
        reapplyInputs();
        refreshDevice();
    }

    void applyRate()
    {
        auto& dm = engine.getDeviceManager().deviceManager;
        auto setup = dm.getAudioDeviceSetup();
        setup.sampleRate = rateBox.getSelectedId();
        auto err = dm.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sample rate", err);
        updateSummary();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (isShowing()) refreshDevice();
    }

    //==============================================================================
    juce::File projectsFolder() { return library::songsFolderOr (props().getValue ("projectsFolder")); }

public:
    /** Another artist was picked: their name and genre go in, the rest of the form stays */
    void accountChanged()
    {
        if (auto* a = account::get().artist())
        {
            auto i = fields.get();
            i.title = fields.title.getText();
            i.artist = a->name;
            if (a->genre.isNotEmpty()) i.genre = a->mainGenre();
            fields.set (i);
        }
        changeWhere.setVisible (! account::get().signedIn());
        updateSummary();
    }
private:

    void chooseWhere()
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Where should Encore Studio keep your songs? (Pick a roomy drive)", projectsFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser&)
                              {
                                  auto dir = chooser->getResult();
                                  if (dir == juce::File()) return;
                                  if (dir.getFileName() != "Encore Studio" && ! dir.getChildFile ("Encore Studio").exists()) dir = dir.getChildFile ("Encore Studio");
                                  props().setValue ("projectsFolder", dir.getFullPathName());
                                  updateSummary();
                              });
    }

    SongPlan plan()
    {
        SongPlan p;
        p.info = fields.get();
        if (auto* d = engine.getDeviceManager().deviceManager.getCurrentAudioDevice()) p.info.sampleRate = d->getCurrentSampleRate();
        p.info.bitDepth = bitsBox.getSelectedId() > 0 ? bitsBox.getSelectedId() : 24;
        auto name = juce::File::createLegalFileName (p.info.title);
        p.folder = projectsFolder().getChildFile (name);
        for (int n = 2; p.folder.exists(); ++n) p.folder = projectsFolder().getChildFile (name + " " + juce::String (n)); // "Song 2", like a DAW would
        for (auto* r : rows) p.tracks.add ({ r->name.getText().trim(), r->chosenInput(), r->group });
        if (juce::isPositiveAndBelow (templateIndex, tiles.size()))
        {
            p.templateFile = tiles[templateIndex]->t.file;
            p.importAfterCreate = tiles[templateIndex]->t.importStems;
        }
        return p;
    }

    void updateSummary()
    {
        auto p = plan();
        wherePath.setText (p.folder.getFullPathName(), juce::dontSendNotification);
        int fed = 0;
        for (auto& t : p.tracks) if (t.input.isNotEmpty()) ++fed;
        juce::String s = juce::String (p.tracks.size()) + (p.tracks.size() == 1 ? " track" : " tracks");
        if (fed > 0) s << " (" << fed << " ready to record)";
        juce::StringArray groupNames;
        for (auto& t : p.tracks) if (t.group.isNotEmpty()) groupNames.addIfNotAlreadyThere (t.group);
        if (! groupNames.isEmpty()) s << ", grouped as " << groupNames.joinIntoString (", ");
        s << ",  " << p.info.formatText() << ",  " << p.info.bpmText() << " BPM " << p.info.sigText();
        if (p.info.key.isNotEmpty()) s << ",  " << p.info.key;
        summary.setText (s, juce::dontSendNotification);
    }

    void createSong()
    {
        auto p = plan();
        auto& acc = account::get();
        if (auto* a = acc.artist())
        {
            if (p.info.album.isNotEmpty()) library::releaseFolder (library::artistFolder (acc.library, *a), p.info.album, p.info.title).createDirectory();
        }
        else props().setValue ("projectsFolder", projectsFolder().getFullPathName());
        if (onCreate) onCreate (p);
    }
};

//==============================================================================
class HubComponent : public juce::Component,
                     private juce::ChangeListener
{
public:
    std::function<void (juce::File)> onOpen;
    std::function<void()> onOpenOther, onBack, onAudioSetup;
    std::function<void (SongPlan)> onCreate;
    std::function<void()> onSignIn, onAccountMenu, onAddArtist, onAccountChanged;

    HubComponent (te::Engine& e, juce::PropertiesFile& p) : engine (e), props (p), newPanel (e)
    {
        auto getProps = [this]() -> juce::PropertiesFile& { return props; };
        recentPanel.props = getProps;
        newPanel.props = getProps;
        recentPanel.onOpen = [this] (juce::File f) { if (onOpen) onOpen (f); };
        recentPanel.onOpenOther = [this] { if (onOpenOther) onOpenOther(); };
        recentPanel.onNew = [this] { showTab (1); };
        newPanel.onCreate = [this] (SongPlan plan) { if (onCreate) onCreate (plan); };
        newPanel.onAudioSetup = [this] { if (onAudioSetup) onAudioSetup(); };

        addAndMakeVisible (recentTab);
        addAndMakeVisible (newTab);
        recentTab.onClick = [this] { showTab (0); };
        newTab.onClick = [this] { showTab (1); };
        addChildComponent (recentPanel);
        addChildComponent (newPanel);

        newButton.setColour (juce::TextButton::buttonColourId, theme::brand);
        audioButton.setColour (juce::TextButton::buttonColourId, theme::strong);
        newButton.onClick = [this] { showTab (1); };
        openButton.onClick = [this] { if (onOpenOther) onOpenOther(); };
        audioButton.onClick = [this] { if (onAudioSetup) onAudioSetup(); };
        backButton.onClick = [this] { if (onBack) onBack(); };
        for (auto* b : { &newButton, &openButton, &audioButton }) addAndMakeVisible (b);
        addChildComponent (backButton);

        auto link = [this] (juce::HyperlinkButton& l, const juce::String& url)
        {
            l.setURL (juce::URL (url));
            l.setFont (hubui::font (13.0f), false, juce::Justification::centredLeft);
            l.setColour (juce::HyperlinkButton::textColourId, theme::text2);
            addAndMakeVisible (l);
        };
        hubui::styleCombo (artistBox);
        artistBox.setTooltip ("Who new songs are for: they go in this artist's folder, with their name filled in");
        artistBox.onChange = [this] { pickArtist(); };
        accountButton.onClick = [this] { if (onAccountMenu) onAccountMenu(); };
        signInButton.onClick = [this] { if (onSignIn) onSignIn(); };
        signInButton.setColour (juce::TextButton::buttonColourId, theme::strong);
        for (auto* c : std::initializer_list<juce::Component*> { &artistBox, &accountButton, &signInButton }) addChildComponent (c);
        refreshAccount();

        link (studioLink, "https://encoremusic.studio");
        link (guideLink, "https://github.com/jimmyrlondo/encore-studio#readme");
        link (issueLink, "https://github.com/jimmyrlondo/encore-studio/issues/new");
        link (updatesLink, "https://github.com/jimmyrlondo/encore-studio/releases");

        engine.getDeviceManager().addChangeListener (this);
    }

    ~HubComponent() override { engine.getDeviceManager().removeChangeListener (this); }

    void refresh (const juce::String& openSongName)
    {
        backButton.setButtonText ("Back to " + openSongName);
        backButton.setVisible (openSongName.isNotEmpty());
        recentPanel.refresh();
        resized();
        repaint();
    }

    void showTab (int t)
    {
        tab = t;
        recentTab.setToggleState (t == 0, juce::dontSendNotification);
        newTab.setToggleState (t == 1, juce::dontSendNotification);
        recentPanel.setVisible (t == 0);
        newPanel.setVisible (t == 1);
        if (t == 1) newPanel.reset();
        if (t == 0) recentPanel.refresh();
    }

    int recentCount() const { return recentPanel.count(); }

    /** Shows who's signed in (and their artists) after the profile changes */
    void refreshAccount()
    {
        auto& p = account::get();
        artistBox.clear (juce::dontSendNotification);
        for (int i = 0; i < p.artists.size(); ++i) artistBox.addItem (p.artists[i].name, i + 1);
        artistBox.addSeparator();
        artistBox.addItem ("Add an artist...", addArtistId);
        artistBox.setSelectedId (p.current + 1, juce::dontSendNotification);
        artistBox.setVisible (p.signedIn());
        accountButton.setVisible (p.signedIn());
        signInButton.setVisible (! p.signedIn());
        newPanel.accountChanged();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::base);
        auto left = leftArea();
        g.setColour (theme::surface);
        g.fillRect (left);
        g.setColour (theme::strong);
        g.fillRect (left.removeFromRight (1));

        auto r = leftArea().reduced (28, 30);
        // wordmark
        auto mark = r.removeFromTop (64);
        drawEncoreIcon (g, mark.removeFromLeft (60).toFloat());
        mark.removeFromLeft (14);
        g.setColour (theme::text2);
        g.setFont (hubui::font (14.0f, true));
        g.drawText ("ENCORE:", mark.removeFromTop (26), juce::Justification::bottomLeft);
        g.setColour (theme::paper);
        g.setFont (hubui::font (30.0f, true));
        g.drawText ("STUDIO", mark.withTrimmedLeft (-2), juce::Justification::topLeft);
        r.removeFromTop (6);
        g.setColour (theme::text2);
        g.setFont (hubui::font (14.0f));
        g.drawFittedText ("Record, edit and mix your songs. Free and open source.", r.removeFromTop (40), juce::Justification::topLeft, 2);
        r.removeFromTop (14);
        auto& acc = account::get();
        hubui::drawHeading (g, acc.isLabel() ? acc.label + ": making music as" : juce::String ("Making music as"), r.removeFromTop (16));

        // audio card
        auto card = audioCard();
        g.setColour (theme::raised);
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        auto c = card.reduced (16, 14);
        hubui::drawHeading (g, "Audio interface", c.removeFromTop (14));
        c.removeFromTop (8);
        auto& dm = engine.getDeviceManager().deviceManager;
        g.setColour (theme::paper);
        g.setFont (hubui::font (15.0f, true));
        if (auto* d = dm.getCurrentAudioDevice())
        {
            auto setup = dm.getAudioDeviceSetup();
            auto name = setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : d->getName();
            g.drawText (name, c.removeFromTop (22), juce::Justification::centredLeft, true);
            g.setColour (theme::text2);
            g.setFont (hubui::font (13.0f));
            const auto ms = d->getCurrentBufferSizeSamples() * 1000.0 / juce::jmax (1.0, d->getCurrentSampleRate());
            g.drawText (dm.getCurrentAudioDeviceType() + ",  " + juce::String (d->getCurrentSampleRate() / 1000.0, 1) + " kHz,  "
                          + juce::String (d->getCurrentBufferSizeSamples()) + " samples (" + juce::String (ms, 1) + " ms)",
                        c.removeFromTop (20), juce::Justification::centredLeft, true);
            int ins = 0;
            auto& tdm = engine.getDeviceManager();
            for (int i = 0; i < tdm.getNumWaveInDevices(); i++) if (auto w = tdm.getWaveInDevice (i)) if (! w->isTrackDevice()) ++ins;
            g.drawText (juce::String (ins) + (ins == 1 ? " input, " : " inputs, ") + juce::String (d->getActiveOutputChannels().countNumberOfSetBits()) + " outputs",
                        c.removeFromTop (20), juce::Justification::centredLeft, true);
        }
        else
        {
            g.drawText ("No interface open", c.removeFromTop (22), juce::Justification::centredLeft);
            g.setColour (theme::text2);
            g.setFont (hubui::font (13.0f));
            g.drawText ("Pick one in Audio setup to record.", c.removeFromTop (20), juce::Justification::centredLeft);
        }

        // links heading + version
        hubui::drawHeading (g, "Encore", linksArea().removeFromTop (16));
        g.setColour (theme::muted);
        g.setFont (hubui::font (12.0f));
        g.drawText ("Version " + juce::String (ProjectInfo::versionString) + "  ·  Free software (AGPLv3)",
                    leftArea().reduced (28, 22).removeFromBottom (16), juce::Justification::bottomLeft);
    }

    void resized() override
    {
        auto r = leftArea().reduced (28, 30);
        r.removeFromTop (64 + 6 + 40 + 14);
        {
            auto acc = r.removeFromTop (accountH).withTrimmedTop (22).removeFromTop (34);
            signInButton.setBounds (acc);
            accountButton.setBounds (acc.removeFromRight (84));
            acc.removeFromRight (8);
            artistBox.setBounds (acc);
        }
        newButton.setBounds (r.removeFromTop (42));
        r.removeFromTop (10);
        openButton.setBounds (r.removeFromTop (38));
        auto card = audioCard();
        audioButton.setBounds (card.reduced (16, 14).removeFromBottom (32).removeFromLeft (130));
        auto links = linksArea();
        links.removeFromTop (24);
        for (auto* l : { &studioLink, &guideLink, &issueLink, &updatesLink })
            l->setBounds (links.removeFromTop (26).removeFromLeft (220));

        auto right = getLocalBounds().withTrimmedLeft (leftArea().getWidth()).reduced (32, 26);
        auto tabs = right.removeFromTop (92);
        recentTab.setBounds (tabs.removeFromLeft (120));
        tabs.removeFromLeft (10);
        newTab.setBounds (tabs.removeFromLeft (120));
        backButton.setBounds (tabs.removeFromRight (220).withSizeKeepingCentre (220, 38));
        right.removeFromTop (22);
        recentPanel.setBounds (right);
        newPanel.setBounds (right);
    }

private:
    te::Engine& engine;
    juce::PropertiesFile& props;
    HubTab recentTab { "Recent", HubTab::Icon::recent }, newTab { "New song", HubTab::Icon::add };
    RecentPanel recentPanel;
    NewSongPanel newPanel;
    int tab = 0;
    juce::TextButton newButton { "New song" }, openButton { "Open a song..." }, audioButton { "Audio setup" }, backButton;
    juce::ComboBox artistBox;
    juce::TextButton accountButton { "Account" }, signInButton { "Set up your profile" };
    static constexpr int accountH = 70, addArtistId = 9000;
    juce::HyperlinkButton studioLink { "Encore", {} }, guideLink { "User guide", {} }, issueLink { "Report a problem", {} }, updatesLink { "What's new / updates", {} };

    juce::Rectangle<int> leftArea() const { return getLocalBounds().removeFromLeft (juce::jlimit (300, 380, getWidth() * 3 / 10)); }
    juce::Rectangle<int> audioCard() const
    {
        auto r = leftArea().reduced (28, 30);
        r.removeFromTop (64 + 6 + 40 + 14 + accountH + 42 + 10 + 38 + 28);
        return r.removeFromTop (160);
    }
    juce::Rectangle<int> linksArea() const { return audioCard().translated (0, 160 + 28).withHeight (24 + 4 * 26); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    void pickArtist()
    {
        const auto id = artistBox.getSelectedId();
        if (id == addArtistId)
        {
            artistBox.setSelectedId (account::get().current + 1, juce::dontSendNotification);
            if (onAddArtist) onAddArtist();
            return;
        }
        if (id <= 0) return;
        account::get().current = id - 1;
        account::save();
        refreshAccount();
        if (onAccountChanged) onAccountChanged();
    }
};
