#pragma once

// Song versions: "Song.encore", "Song v2.encore", "Song v3.encore"...
// side by side in the song's folder (File > Save New Version makes the next),
// each with an optional note ("chorus louder") kept in versions.xml there.
// File > Versions lists them to open one, change its note, or hear it
// against the open one.

namespace versions
{
    struct Version
    {
        juce::File file;
        int number = 1;            // the plain "Song.encore" is version 1
        juce::String note;
        juce::Time modified;
    };

    /** "Song v3" -> ("Song", 3); "Song" -> ("Song", 1) */
    inline std::pair<juce::String, int> split (const juce::String& name)
    {
        if (auto at = name.lastIndexOf (" v"); at > 0 && name.substring (at + 2).isNotEmpty() && name.substring (at + 2).containsOnly ("0123456789"))
            return { name.substring (0, at), name.substring (at + 2).getIntValue() };
        return { name, 1 };
    }

    inline juce::File notesFile (const juce::File& songFile) { return songFile.getSiblingFile ("versions.xml"); }

    inline juce::String noteFor (const juce::File& songFile)
    {
        if (auto xml = juce::parseXML (notesFile (songFile)))
            for (auto* v : xml->getChildWithTagNameIterator ("version"))
                if (v->getStringAttribute ("file") == songFile.getFileName()) return v->getStringAttribute ("note");
        return {};
    }

    inline void setNote (const juce::File& songFile, const juce::String& note)
    {
        auto xml = juce::parseXML (notesFile (songFile));
        if (xml == nullptr || ! xml->hasTagName ("versions")) xml = std::make_unique<juce::XmlElement> ("versions");
        juce::XmlElement* entry = nullptr;
        for (auto* v : xml->getChildWithTagNameIterator ("version"))
            if (v->getStringAttribute ("file") == songFile.getFileName()) entry = v;
        if (entry == nullptr) { entry = xml->createNewChildElement ("version"); entry->setAttribute ("file", songFile.getFileName()); }
        entry->setAttribute ("note", note.trim());
        xml->writeTo (notesFile (songFile));
    }

    /** Every version of this song in its folder, oldest first */
    inline juce::Array<Version> list (const juce::File& songFile)
    {
        juce::Array<Version> out;
        const auto base = split (songFile.getFileNameWithoutExtension()).first;
        for (auto& f : songFile.getParentDirectory().findChildFiles (juce::File::findFiles, false, songfile::patterns))
        {
            auto [b, n] = split (f.getFileNameWithoutExtension());
            if (b != base) continue;
            out.add ({ f, n, noteFor (f), f.getLastModificationTime() });
        }
        std::sort (out.begin(), out.end(), [] (const Version& x, const Version& y) { return x.number < y.number; });
        return out;
    }

    /** The next "Song vN.encore" that doesn't exist yet */
    inline juce::File nextFile (const juce::File& songFile)
    {
        int top = 1;
        for (auto& v : list (songFile)) top = juce::jmax (top, v.number);
        const auto base = split (songFile.getFileNameWithoutExtension()).first;
        auto next = songFile.getSiblingFile (base + " v" + juce::String (top + 1) + songFile.getFileExtension());
        while (next.exists()) next = songFile.getSiblingFile (base + " v" + juce::String (++top + 1) + songFile.getFileExtension());
        return next;
    }

    inline juce::String when (juce::Time t)
    {
        const auto now = juce::Time::getCurrentTime();
        if (t.getYear() == now.getYear() && t.getDayOfYear() == now.getDayOfYear()) return "Today " + t.formatted ("%H:%M");
        if (now - t < juce::RelativeTime::days (6)) return t.formatted ("%A %H:%M");
        return t.formatted ("%d %b %Y");
    }

    //==============================================================================
    /** File > Versions */
    class VersionsComponent : public juce::Component, private juce::ListBoxModel
    {
    public:
        std::function<void (juce::File)> onOpen;
        std::function<void()> onSaveNew;
        std::function<void (juce::File other)> onCompare;   // render both, play the open one
        std::function<void (bool other)> onHearOther;
        std::function<void()> onStopComparing;

        explicit VersionsComponent (juce::File current) : open (std::move (current))
        {
            hubui::styleLabel (heading, 15.0f, theme::paper);
            heading.setText ("Versions of " + split (open.getFileNameWithoutExtension()).first, juce::dontSendNotification);
            addAndMakeVisible (heading);
            hubui::styleLabel (info, 12.0f, theme::muted);
            info.setText ("Each version is its own file in the song folder (they share the recordings). Double-click one to open it.",
                          juce::dontSendNotification);
            addAndMakeVisible (info);
            listBox.setModel (this);
            listBox.setRowHeight (44);
            listBox.setColour (juce::ListBox::backgroundColourId, theme::subtle);
            listBox.setColour (juce::ListBox::outlineColourId, theme::strong);
            addAndMakeVisible (listBox);
            for (auto* b : { &openB, &noteB, &compareB, &saveB, &closeB }) addAndMakeVisible (b);
            hubui::styleLabel (compareNote, 12.0f, theme::text2);
            addAndMakeVisible (compareNote);
            other.setColour (juce::ToggleButton::textColourId, theme::paper);
            other.setColour (juce::ToggleButton::tickColourId, theme::accent);
            addChildComponent (other);
            other.onClick = [this] { if (onHearOther) onHearOther (other.getToggleState()); updateCompareText(); };
            saveB.setColour (juce::TextButton::buttonColourId, theme::brand);
            openB.onClick = [this] { if (auto* v = selected(); v != nullptr && v->file != open && onOpen) { stopComparing(); onOpen (v->file); close(); } };
            noteB.onClick = [this] { editNote(); };
            compareB.onClick = [this]
            {
                if (comparing) { stopComparing(); return; }
                if (auto* v = selected(); v != nullptr && v->file != open && onCompare)
                {
                    comparing = true;
                    comparedNumber = v->number;
                    compareB.setButtonText ("Stop");
                    other.setButtonText ("Hear v" + juce::String (v->number) + " instead");
                    other.setToggleState (false, juce::dontSendNotification);
                    other.setVisible (true);
                    compareNote.setText ("Getting 30 seconds of both ready...", juce::dontSendNotification);
                    onCompare (v->file);
                }
            };
            saveB.onClick = [this] { stopComparing(); if (onSaveNew) onSaveNew(); close(); };
            closeB.onClick = [this] { close(); };
            refresh();
            setSize (620, 480);
        }

        ~VersionsComponent() override { if (comparing && onStopComparing) onStopComparing(); }

        void comparisonReady (const juce::String& text) { compareNote.setText (text, juce::dontSendNotification); ready = true; }

        void refresh()
        {
            all = list (open);
            listBox.updateContent();
            for (int i = 0; i < all.size(); ++i)
                if (all[i].file == open) listBox.selectRow (i);
            updateButtons();
            repaint();
        }

        void paint (juce::Graphics& g) override { g.fillAll (theme::surface); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (20, 16);
            heading.setBounds (r.removeFromTop (24));
            info.setBounds (r.removeFromTop (34));
            r.removeFromTop (6);
            auto bottom = r.removeFromBottom (36);
            closeB.setBounds (bottom.removeFromRight (90));
            bottom.removeFromRight (8);
            saveB.setBounds (bottom.removeFromRight (170));
            r.removeFromBottom (10);
            auto compareRow = r.removeFromBottom (26);
            other.setBounds (compareRow.removeFromLeft (200));
            compareNote.setBounds (compareRow);
            r.removeFromBottom (8);
            auto buttons = r.removeFromBottom (32);
            openB.setBounds (buttons.removeFromLeft (90));
            buttons.removeFromLeft (8);
            noteB.setBounds (buttons.removeFromLeft (110));
            buttons.removeFromLeft (8);
            compareB.setBounds (buttons.removeFromLeft (180));
            r.removeFromBottom (10);
            listBox.setBounds (r);
        }

    private:
        juce::File open;
        juce::Array<Version> all;
        juce::Label heading, info, compareNote;
        juce::ListBox listBox;
        juce::TextButton openB { "Open" }, noteB { "Change note..." }, compareB { "Compare by ear" },
                         saveB { "Save a new version..." }, closeB { "Close" };
        juce::ToggleButton other;
        bool comparing = false, ready = false;
        int comparedNumber = 0;

        const Version* selected() const
        {
            const int row = listBox.getSelectedRow();
            return row >= 0 && row < all.size() ? &all.getReference (row) : nullptr;
        }

        void updateButtons()
        {
            const auto* v = selected();
            const bool another = v != nullptr && v->file != open;
            openB.setEnabled (another);
            compareB.setEnabled (another || comparing);
            noteB.setEnabled (v != nullptr);
        }

        void updateCompareText()
        {
            if (! ready) return;
            compareNote.setText (other.getToggleState() ? "Hearing v" + juce::String (comparedNumber) + "." : "Hearing the open version.",
                                 juce::dontSendNotification);
        }

        void stopComparing()
        {
            if (! comparing) return;
            comparing = ready = false;
            compareB.setButtonText ("Compare by ear");
            other.setVisible (false);
            compareNote.setText ({}, juce::dontSendNotification);
            if (onStopComparing) onStopComparing();
            updateButtons();
        }

        void editNote()
        {
            auto* v = selected();
            if (v == nullptr) return;
            auto file = v->file;
            auto* w = new juce::AlertWindow ("Version note", "What's different in v" + juce::String (v->number) + "?", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("note", v->note);
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<VersionsComponent> safe (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, file] (int result)
            {
                if (result == 1) setNote (file, w->getTextEditorContents ("note"));
                if (safe) safe->refresh();
            }), true);
        }

        void close() { stopComparing(); if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (0); }

        int getNumRows() override { return all.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected) override
        {
            if (row < 0 || row >= all.size()) return;
            const auto& v = all.getReference (row);
            if (isSelected) { g.setColour (theme::raised); g.fillRect (0, 0, w, h); }
            g.setColour (theme::strong);
            g.fillRect (0, h - 1, w, 1);
            const bool isOpen = v.file == open;
            g.setColour (isOpen ? theme::accent : theme::paper);
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText ("v" + juce::String (v.number), 14, 0, 50, h, juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (14.0f));
            g.setColour (theme::paper);
            g.drawText (v.note.isNotEmpty() ? v.note : (v.number == 1 ? juce::String ("The first one") : juce::String ("(no note)")),
                        64, 4, w - 260, h / 2, juce::Justification::bottomLeft, true);
            g.setColour (theme::muted);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (v.file.getFileName(), 64, h / 2, w - 260, h / 2 - 4, juce::Justification::topLeft, true);
            g.drawText (when (v.modified), w - 190, 0, 120, h, juce::Justification::centredRight);
            if (isOpen)
            {
                g.setColour (theme::accent);
                g.drawText ("open", w - 60, 0, 48, h, juce::Justification::centredRight);
            }
        }
        void selectedRowsChanged (int) override { if (comparing) stopComparing(); updateButtons(); }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            if (row >= 0 && row < all.size() && all[row].file != open && onOpen) { auto f = all[row].file; stopComparing(); onOpen (f); close(); }
        }
    };
}
