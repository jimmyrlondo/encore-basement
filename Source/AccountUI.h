// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The first screen (who you're making music as) and Account > Edit Profile.
// The details here fill in every new song (artist, genre) and what goes out
// with a release (who owns the recordings, the songwriter's PRO and IPI).
#pragma once

//==============================================================================
/** One of a label's artists: name, genres, and a button to take it off the list */
class ArtistRow : public juce::Component
{
public:
    std::function<void()> onRemove;

    ArtistRow (const account::Artist& a) : artist (a)
    {
        hubui::styleEditor (name, "Artist or band name");
        name.setText (a.name, false);
        hubui::styleEditor (genre.getEditor());
        genre.setText (a.genre);
        remove.setTooltip ("Take this artist off the list (their folder and songs stay)");
        remove.onClick = [this] { if (onRemove) onRemove(); };
        for (auto* c : std::initializer_list<juce::Component*> { &name, &genre, &remove }) addAndMakeVisible (c);
    }

    account::Artist get() const
    {
        auto a = artist;   // keeps any details only the artist's own profile shows
        a.name = name.getText().trim();
        a.genre = genre.getText();
        return a;
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedBottom (6);
        remove.setBounds (r.removeFromRight (32));
        r.removeFromRight (6);
        genre.setBounds (r.removeFromRight (r.getWidth() / 2));
        r.removeFromRight (6);
        name.setBounds (r);
    }

    juce::TextEditor name;

private:
    account::Artist artist;
    genres::Field genre { true };
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
        view.setViewedComponent (&form, false);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (10);
        addAndMakeVisible (view);

        for (auto* l : { &nameL, &emailL, &kindL, &artistL, &genreL, &labelL, &artistsL, &whereL, &wherePath, &note,
                         &hometownL, &membersL, &websiteL, &copyrightL, &proL, &ipiL, &moreNote })
        {
            hubui::styleLabel (*l);
            form.addAndMakeVisible (l);
        }
        hubui::styleLabel (wherePath, 13.0f, theme::paper);
        hubui::styleLabel (note, 12.0f, theme::muted);
        hubui::styleLabel (moreNote, 12.0f, theme::muted);
        hubui::styleLabel (problem, 13.0f, theme::brand, true);
        addAndMakeVisible (problem);
        note.setJustificationType (juce::Justification::topLeft);
        moreNote.setJustificationType (juce::Justification::topLeft);
        note.setText ("Encore accounts are coming soon. Until then this profile lives on this computer, "
                      "and it links to your Encore account when sign-in arrives.", juce::dontSendNotification);
        moreNote.setText ("Optional. These go into your songs' details and the notes that go out with a release.", juce::dontSendNotification);

        hubui::styleEditor (name, "Your name");
        hubui::styleEditor (email, "Optional, for linking your Encore account later");
        hubui::styleEditor (artistName, "Your artist or band name");
        hubui::styleEditor (labelName, "Your label's name");
        hubui::styleEditor (genre.getEditor());
        hubui::styleEditor (hometown, "City, state or country");
        hubui::styleEditor (members, "One per line: name and what they play (Jimmy: guitar, vocals)");
        hubui::styleEditor (website, "Your site, Encore page or socials");
        hubui::styleEditor (copyright, "Who owns the recordings (you, your band or your label)");
        hubui::styleEditor (ipi, "The songwriter's IPI/CAE number, if you have one");
        members.setMultiLine (true, true);
        members.setReturnKeyStartsNewLine (true);
        name.setText (p.name, false);
        email.setText (p.email, false);
        labelName.setText (p.label, false);
        pro.addItemList ({ "None / not registered", "ASCAP", "BMI", "SESAC", "GMR", "SOCAN", "PRS for Music", "APRA AMCOS", "GEMA", "SACEM",
                           "SIAE", "SGAE", "JASRAC", "KODA", "STIM", "BUMA/STEMRA", "IMRO", "Other" }, 1);
        pro.setSelectedId (1, juce::dontSendNotification);
        hubui::styleCombo (pro);
        if (! p.isLabel() && p.artist() != nullptr)
        {
            const auto& a = *p.artist();
            artistName.setText (a.name, false);
            genre.setText (a.genre);
            hometown.setText (a.hometown, false);
            members.setText (a.members, false);
            website.setText (a.website, false);
            copyright.setText (a.copyright, false);
            ipi.setText (a.ipi, false);
            if (a.pro.isNotEmpty())
            {
                pro.setText (a.pro, juce::dontSendNotification);
                if (pro.getSelectedId() == 0) pro.setSelectedId (18, juce::dontSendNotification);
            }
        }
        else if (p.isLabel() && ! p.artists.isEmpty())
        {
            copyright.setText (p.artists[0].copyright.isNotEmpty() ? p.artists[0].copyright : p.label, false);
            website.setText (p.artists[0].website, false);
        }

        for (auto* b : { &asArtist, &asLabel })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7101);
            b->setColour (juce::TextButton::buttonOnColourId, theme::brand);
            form.addAndMakeVisible (b);
        }
        (p.isLabel() ? asLabel : asArtist).setToggleState (true, juce::dontSendNotification);
        asArtist.onClick = asLabel.onClick = [this] { updateKind(); };

        for (auto* c : std::initializer_list<juce::Component*> { &name, &email, &artistName, &genre, &labelName, &addArtist, &change,
                                                                &hometown, &members, &website, &copyright, &pro, &ipi })
            form.addAndMakeVisible (c);
        for (auto* c : { &start, &skip }) addAndMakeVisible (c);
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
        setSize (700, 720);
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
        g.setColour (theme::strong);
        g.fillRect (content().removeFromBottom (54).removeFromTop (1));
    }

    void resized() override
    {
        auto r = content();
        r.removeFromTop (56 + 10);
        auto buttons = r.removeFromBottom (42);
        start.setBounds (buttons.removeFromRight (200));
        buttons.removeFromRight (10);
        skip.setBounds (buttons.removeFromRight (140));
        problem.setBounds (buttons);
        r.removeFromBottom (12);
        view.setBounds (r);
        layoutForm (r.getWidth() - 14);
    }

private:
    const bool firstRun;
    juce::Viewport view;
    struct Form : public juce::Component
    {
        juce::Array<juce::Rectangle<int>> headings;
        juce::StringArray text;
        void paint (juce::Graphics& g) override { for (int i = 0; i < headings.size(); ++i) hubui::drawHeading (g, text[i], headings[i]); }
    } form;
    juce::TextEditor name, email, artistName, labelName, hometown, members, website, copyright, ipi;
    genres::Field genre { true };
    juce::ComboBox pro;
    juce::TextButton asArtist { "An artist or band" }, asLabel { "A record label" }, addArtist { "+ Add an artist" },
                     change { "Change..." }, start, skip;
    juce::Label nameL { {}, "Your name" }, emailL { {}, "Email" }, kindL { {}, "I'm" }, artistL { {}, "Artist / band" }, genreL { {}, "Genres" },
                labelL { {}, "Label" }, artistsL { {}, "Artists" }, whereL { {}, "Keep music in" }, wherePath, note, problem,
                hometownL { {}, "From" }, membersL { {}, "Members" }, websiteL { {}, "Website" }, copyrightL { {}, "Recordings owned by" },
                proL { {}, "Songwriter PRO" }, ipiL { {}, "IPI number" }, moreNote;
    juce::OwnedArray<ArtistRow> rows;
    juce::File library;
    static constexpr int rowH = 34, gap = 10, labelW = 150;

    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 720), juce::jmin (getHeight() - 40, 780)); }
    juce::Rectangle<int> content() const { return (firstRun ? card() : getLocalBounds()).reduced (28, 24); }

    void layoutForm (int w)
    {
        int y = 0;
        form.headings.clear();
        form.text.clear();
        auto line = [&] (juce::Label& l, int h = rowH) { auto x = juce::Rectangle<int> (0, y, w, h); y += h + gap; l.setBounds (x.removeFromLeft (labelW).withHeight (rowH)); return x; };
        auto heading = [&] (const juce::String& t) { form.headings.add ({ 0, y, w, 24 }); form.text.add (t); y += 32; };

        note.setBounds (0, y, w, 36); y += 44;
        name.setBounds (line (nameL));
        email.setBounds (line (emailL));
        auto k = line (kindL);
        asArtist.setBounds (k.removeFromLeft (180));
        k.removeFromLeft (8);
        asLabel.setBounds (k.removeFromLeft (180));
        y += 6;

        const bool label = asLabel.getToggleState();
        if (! label)
        {
            heading ("The artist");
            artistName.setBounds (line (artistL));
            genre.setBounds (line (genreL));
            hometown.setBounds (line (hometownL));
            members.setBounds (line (membersL, 72));
            website.setBounds (line (websiteL));
        }
        else
        {
            heading ("The label");
            labelName.setBounds (line (labelL));
            website.setBounds (line (websiteL));
            artistsL.setBounds (0, y, labelW, rowH);
            for (auto* x : rows) { x->setBounds (labelW, y, w - labelW, rowH + 6); y += rowH + 6; }
            addArtist.setBounds (labelW, y, 150, rowH);
            y += rowH + gap;
        }
        heading ("Releases");
        moreNote.setBounds (0, y, w, 20); y += 26;
        copyright.setBounds (line (copyrightL));
        for (auto* c : std::initializer_list<juce::Component*> { &pro, &ipi, &proL, &ipiL }) c->setVisible (! label);
        if (! label)
        {
            pro.setBounds (line (proL).removeFromLeft (240));
            ipi.setBounds (line (ipiL).removeFromLeft (300));
        }
        heading ("Your music library");
        auto wr = line (whereL);
        change.setBounds (wr.removeFromRight (100));
        wr.removeFromRight (8);
        wherePath.setBounds (wr);
        form.setSize (w, y + 8);
        form.repaint();
    }

    void addRow (const account::Artist& a)
    {
        auto* x = rows.add (new ArtistRow (a));
        x->onRemove = [this, x]
        {
            if (rows.size() <= 1) { x->name.clear(); return; }
            rows.removeObject (x);
            resized();
        };
        form.addAndMakeVisible (x);
        resized();
    }

    void updateKind()
    {
        const bool label = asLabel.getToggleState();
        for (auto* c : std::initializer_list<juce::Component*> { &artistName, &genre, &artistL, &genreL, &hometown, &hometownL, &members, &membersL })
            c->setVisible (! label);
        for (auto* c : std::initializer_list<juce::Component*> { &labelName, &labelL, &artistsL, &addArtist }) c->setVisible (label);
        for (auto* x : rows) x->setVisible (label);
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
            for (auto* x : rows)
                if (auto a = x->get(); a.name.isNotEmpty())
                {
                    if (a.copyright.isEmpty()) a.copyright = copyright.getText().trim();
                    if (a.website.isEmpty()) a.website = website.getText().trim();
                    next.artists.add (a);
                }
            if (next.artists.isEmpty()) { complain ("Add at least one of the label's artists."); return; }
        }
        else
        {
            account::Artist a = p.artist() != nullptr && ! p.isLabel() ? *p.artist() : account::Artist {};
            a.name = artistName.getText().trim();
            if (a.name.isEmpty()) a.name = next.name;
            if (a.name.isEmpty()) { complain ("What's your artist or band name?"); artistName.grabKeyboardFocus(); return; }
            a.genre = genre.getText();
            a.hometown = hometown.getText().trim();
            a.members = members.getText().trim();
            a.website = website.getText().trim();
            a.copyright = copyright.getText().trim();
            a.pro = pro.getSelectedId() > 1 ? pro.getText() : juce::String();
            a.ipi = ipi.getText().trim();
            next.artists.add (a);
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
