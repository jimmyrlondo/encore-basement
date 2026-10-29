// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The first screen (who you're making music as) and Account > Edit Profile.
#pragma once

//==============================================================================
/** One of a label's artists: name, genre, and a button to take it off the list */
class ArtistRow : public juce::Component
{
public:
    std::function<void()> onRemove;

    ArtistRow (const account::Artist& a)
    {
        hubui::styleEditor (name, "Artist or band name");
        name.setText (a.name, false);
        genre.setEditableText (true);
        genre.addItemList (hubui::genres(), 1);
        genre.setTextWhenNothingSelected ("Genre");
        genre.setText (a.genre, juce::dontSendNotification);
        hubui::styleCombo (genre);
        remove.setTooltip ("Take this artist off the list (their folder and songs stay)");
        remove.onClick = [this] { if (onRemove) onRemove(); };
        for (auto* c : std::initializer_list<juce::Component*> { &name, &genre, &remove }) addAndMakeVisible (c);
    }

    account::Artist get() const { return { name.getText().trim(), genre.getText().trim() }; }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedBottom (6);
        remove.setBounds (r.removeFromRight (32));
        r.removeFromRight (6);
        genre.setBounds (r.removeFromRight (150));
        r.removeFromRight (6);
        name.setBounds (r);
    }

    juce::TextEditor name;

private:
    juce::ComboBox genre;
    juce::TextButton remove { "x" };
};

//==============================================================================
/** Who you're making music as: an artist (or band), or a record label with its artists.
    firstRun = the welcome screen (it covers the window, with Skip for now);
    otherwise it's Account > Edit Profile in a dialog. */
class AccountComponent : public juce::Component
{
public:
    std::function<void (bool signedIn)> onDone;

    AccountComponent (bool isFirstRun) : firstRun (isFirstRun)
    {
        const auto& p = account::get();
        for (auto* l : { &nameL, &emailL, &kindL, &artistL, &genreL, &labelL, &artistsL, &whereL, &wherePath, &note, &problem })
        {
            hubui::styleLabel (*l);
            addAndMakeVisible (l);
        }
        hubui::styleLabel (wherePath, 13.0f, theme::paper);
        hubui::styleLabel (note, 12.0f, theme::muted);
        hubui::styleLabel (problem, 13.0f, theme::brand, true);
        note.setJustificationType (juce::Justification::topLeft);
        note.setText ("Encore accounts are coming soon. Until then this profile lives on this computer, "
                      "and it links to your Encore account when sign-in arrives.", juce::dontSendNotification);

        hubui::styleEditor (name, "Your name");
        hubui::styleEditor (email, "Optional, for linking your Encore account later");
        hubui::styleEditor (artistName, "Your artist or band name");
        hubui::styleEditor (labelName, "Your label's name");
        name.setText (p.name, false);
        email.setText (p.email, false);
        labelName.setText (p.label, false);
        genre.setEditableText (true);
        genre.addItemList (hubui::genres(), 1);
        genre.setTextWhenNothingSelected ("Pick or type one");
        hubui::styleCombo (genre);
        if (! p.isLabel() && p.artist() != nullptr)
        {
            artistName.setText (p.artist()->name, false);
            genre.setText (p.artist()->genre, juce::dontSendNotification);
        }

        for (auto* b : { &asArtist, &asLabel })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7101);
            b->setColour (juce::TextButton::buttonOnColourId, theme::brand);
            addAndMakeVisible (b);
        }
        (p.isLabel() ? asLabel : asArtist).setToggleState (true, juce::dontSendNotification);
        asArtist.onClick = asLabel.onClick = [this] { updateKind(); };

        for (auto* c : std::initializer_list<juce::Component*> { &name, &email, &artistName, &genre, &labelName, &addArtist, &change, &start, &skip })
            addAndMakeVisible (c);
        rowsView.setViewedComponent (&rowsHolder, false);
        rowsView.setScrollBarsShown (true, false);
        addAndMakeVisible (rowsView);
        if (p.isLabel()) for (auto& a : p.artists) addRow (a);
        if (rows.isEmpty()) addRow ({});
        addArtist.onClick = [this] { addRow ({}); rows.getLast()->name.grabKeyboardFocus(); };

        library = p.library != juce::File() ? p.library : account::defaultLibrary();
        change.onClick = [this] { chooseLibrary(); };
        start.setButtonText (firstRun ? "Start making music" : "Save");
        start.setColour (juce::TextButton::buttonColourId, theme::brand);
        start.onClick = [this] { finish(); };
        skip.setButtonText (firstRun ? "Skip for now" : "Cancel");
        skip.onClick = [this]
        {
            if (firstRun) { account::get().skipped = true; account::save(); }
            close (false);
        };
        updateKind();
        setSize (620, 640);
    }

    void paint (juce::Graphics& g) override
    {
        if (firstRun)
        {
            g.fillAll (theme::base);
            g.setColour (theme::surface);
            g.fillRoundedRectangle (card().toFloat(), 14.0f);
        }
        else g.fillAll (theme::surface);
        auto r = content();
        auto mark = r.removeFromTop (56);
        drawEncoreIcon (g, mark.removeFromLeft (52).toFloat());
        mark.removeFromLeft (14);
        g.setColour (theme::paper);
        g.setFont (hubui::font (24.0f, true));
        g.drawText (firstRun ? "Welcome to Encore: Studio" : "Your profile", mark.removeFromTop (32), juce::Justification::bottomLeft);
        g.setColour (theme::text2);
        g.setFont (hubui::font (14.0f));
        g.drawText ("Who are you making music as?", mark, juce::Justification::topLeft);
    }

    void resized() override
    {
        auto r = content();
        r.removeFromTop (56 + 8);
        note.setBounds (r.removeFromTop (36));
        r.removeFromTop (10);
        auto row = [&] (juce::Label& l) { auto x = r.removeFromTop (rowH); r.removeFromTop (gap); l.setBounds (x.removeFromLeft (labelW)); return x; };
        name.setBounds (row (nameL));
        email.setBounds (row (emailL));
        auto k = row (kindL);
        asArtist.setBounds (k.removeFromLeft (180));
        k.removeFromLeft (8);
        asLabel.setBounds (k.removeFromLeft (180));
        r.removeFromTop (4);

        const bool label = asLabel.getToggleState();
        if (! label)
        {
            artistName.setBounds (row (artistL));
            genre.setBounds (row (genreL).removeFromLeft (260));
        }
        else
        {
            labelName.setBounds (row (labelL));
            auto list = r.removeFromTop (4 * rowH + 4 * 6);
            artistsL.setBounds (list.removeFromLeft (labelW).removeFromTop (rowH));
            auto addRowArea = list.removeFromBottom (rowH);
            addArtist.setBounds (addRowArea.removeFromLeft (150));
            list.removeFromBottom (6);
            rowsView.setBounds (list);
            rowsHolder.setSize (list.getWidth() - (rows.size() * (rowH + 6) > list.getHeight() ? 12 : 0), rows.size() * (rowH + 6));
            int y = 0;
            for (auto* x : rows) { x->setBounds (0, y, rowsHolder.getWidth(), rowH + 6); y += rowH + 6; }
            r.removeFromTop (gap);
        }
        auto w = row (whereL);
        change.setBounds (w.removeFromRight (100));
        w.removeFromRight (8);
        wherePath.setBounds (w);
        problem.setBounds (r.removeFromTop (22));

        auto buttons = content().removeFromBottom (42);
        start.setBounds (buttons.removeFromRight (200));
        buttons.removeFromRight (10);
        skip.setBounds (buttons.removeFromRight (140));
    }

private:
    const bool firstRun;
    juce::TextEditor name, email, artistName, labelName;
    juce::ComboBox genre;
    juce::TextButton asArtist { "An artist or band" }, asLabel { "A record label" }, addArtist { "+ Add an artist" },
                     change { "Change..." }, start, skip;
    juce::Label nameL { {}, "Your name" }, emailL { {}, "Email" }, kindL { {}, "I'm" }, artistL { {}, "Artist / band" }, genreL { {}, "Genre" },
                labelL { {}, "Label" }, artistsL { {}, "Artists" }, whereL { {}, "Keep music in" }, wherePath, note, problem;
    juce::Viewport rowsView;
    juce::Component rowsHolder;
    juce::OwnedArray<ArtistRow> rows;
    juce::File library;
    static constexpr int rowH = 34, gap = 10, labelW = 120;

    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 640), juce::jmin (getHeight() - 40, 640)); }
    juce::Rectangle<int> content() const { return (firstRun ? card() : getLocalBounds()).reduced (28, 24); }

    void addRow (const account::Artist& a)
    {
        auto* x = rows.add (new ArtistRow (a));
        x->onRemove = [this, x]
        {
            if (rows.size() <= 1) { x->name.clear(); return; }
            rows.removeObject (x);
            resized();
        };
        rowsHolder.addAndMakeVisible (x);
        resized();
    }

    void updateKind()
    {
        const bool label = asLabel.getToggleState();
        for (auto* c : std::initializer_list<juce::Component*> { &artistName, &genre, &artistL, &genreL }) c->setVisible (! label);
        for (auto* c : std::initializer_list<juce::Component*> { &labelName, &labelL, &artistsL, &rowsView, &addArtist }) c->setVisible (label);
        wherePath.setText (library.getFullPathName() + (label ? "   (a folder for each artist)" : "   (a folder for you)"), juce::dontSendNotification);
        problem.setText ({}, juce::dontSendNotification);
        resized();
        repaint();
    }

    void chooseLibrary()
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Where should Encore Studio keep your music? (Pick a roomy drive)", library);
        juce::Component::SafePointer<AccountComponent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe, chooser] (const juce::FileChooser&)
        {
            auto dir = chooser->getResult();
            if (safe == nullptr || dir == juce::File()) return;
            if (dir.getFileName() != "Encore Studio" && ! dir.getChildFile ("Encore Studio").exists()) dir = dir.getChildFile ("Encore Studio");
            safe->library = dir;
            safe->updateKind();
        });
    }

    void finish()
    {
        auto& p = account::get();
        const auto previous = p.artist() != nullptr ? p.artist()->name : juce::String();
        account::Profile next;
        next.name = name.getText().trim();
        next.email = email.getText().trim();
        next.library = library;
        if (asLabel.getToggleState())
        {
            next.label = labelName.getText().trim();
            if (next.label.isEmpty()) { complain ("What's the label called?"); labelName.grabKeyboardFocus(); return; }
            for (auto* x : rows) if (x->get().name.isNotEmpty()) next.artists.add (x->get());
            if (next.artists.isEmpty()) { complain ("Add at least one of the label's artists."); return; }
        }
        else
        {
            auto a = artistName.getText().trim();
            if (a.isEmpty()) a = next.name;
            if (a.isEmpty()) { complain ("What's your artist or band name?"); artistName.grabKeyboardFocus(); return; }
            next.artists.add ({ a, genre.getText().trim() });
            if (! p.isLabel()) for (int i = 1; i < p.artists.size(); ++i) next.artists.add (p.artists[i]);   // side projects added from the Hub
        }
        for (int i = 0; i < next.artists.size(); ++i) if (next.artists[i].name == previous) next.current = i;
        if (! library.createDirectory()) { complain ("Couldn't make " + library.getFullPathName() + ". Pick another place."); return; }
        for (auto& a : next.artists) library::ensure (library, a);
        p = next;
        account::save();
        close (true);
    }

    void complain (const juce::String& s) { problem.setText (s, juce::dontSendNotification); }

    void close (bool signedIn)
    {
        auto done = onDone;
        if (! firstRun) if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (0);
        if (done) done (signedIn);   // may delete this (the first screen goes away)
    }
};
