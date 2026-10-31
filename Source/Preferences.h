// Encore: Basement Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Studio > Preferences: a normal settings window. Pages down the left (General,
// Audio, Recording, Plug-ins, Keyboard Shortcuts), the page on the right in
// labelled groups, and OK / Cancel / Apply at the bottom. Cancel puts back
// everything changed since the window opened (or since the last Apply),
// including the audio interface and the shortcuts.
#pragma once

namespace prefs
{
    /** Dialogs look like a desktop program's: square-ish buttons, regular-weight text, outlined fields */
    class DialogLookAndFeel : public StudioLookAndFeel
    {
    public:
        DialogLookAndFeel()
        {
            using namespace juce;
            setColour (ComboBox::outlineColourId, theme::strong.brighter (0.25f));
            setColour (TextEditor::outlineColourId, theme::strong.brighter (0.25f));
            setColour (GroupComponent::outlineColourId, theme::strong.brighter (0.2f));
            setColour (GroupComponent::textColourId, theme::text2);
            setColour (ToggleButton::textColourId, theme::paper);
            setColour (ToggleButton::tickColourId, theme::accent);
            setColour (ToggleButton::tickDisabledColourId, theme::muted);
            setColour (ListBox::outlineColourId, theme::strong.brighter (0.25f));
            setColour (KeyMappingEditorComponent::backgroundColourId, theme::subtle);
            setColour (KeyMappingEditorComponent::textColourId, theme::paper);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
            if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);
            else if (down) c = c.brighter (0.15f);
            else if (over) c = c.brighter (0.08f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (b.hasKeyboardFocus (false) ? theme::accent : theme::strong.brighter (0.3f));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
        }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (13.5f); }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.5f); }
        juce::Font getLabelFont (juce::Label& l) override { return l.getFont().withHeight (juce::jmin (l.getFont().getHeight(), 14.0f)); }
        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
            g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (box.hasKeyboardFocus (true) ? theme::accent : box.findColour (juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            juce::Path arrow;
            const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
            arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
            g.setColour (theme::text2.withAlpha (box.isEnabled() ? 1.0f : 0.4f));
            g.fillPath (arrow);
        }
    };

    /** A label in the left column of a settings row */
    inline void styleRowLabel (juce::Label& l)
    {
        l.setFont (juce::FontOptions (13.5f));
        l.setColour (juce::Label::textColourId, theme::paper);
        l.setJustificationType (juce::Justification::centredLeft);
    }
    /** Grey help text under a setting */
    inline void styleNote (juce::Label& l)
    {
        l.setFont (juce::FontOptions (12.5f));
        l.setColour (juce::Label::textColourId, theme::muted);
        l.setJustificationType (juce::Justification::topLeft);
    }

    /** A page: groups stacked down the page, each with rows of label + control */
    class Page : public juce::Component
    {
    public:
        static constexpr int rowH = 26, gap = 8, labelW = 190;
    protected:
        juce::OwnedArray<juce::GroupComponent> groups;
        juce::GroupComponent& group (const juce::String& title)
        {
            auto* g = groups.add (new juce::GroupComponent ({}, title));
            g->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (g);
            g->toBack();   // the box is drawn behind the settings in it
            return *g;
        }
        /** Lays a group out from the top of r: returns the area inside it */
        static juce::Rectangle<int> placeGroup (juce::GroupComponent& g, juce::Rectangle<int>& r, int innerHeight)
        {
            auto b = r.removeFromTop (innerHeight + 30);
            r.removeFromTop (12);
            g.setBounds (b);
            return b.reduced (14, 0).withTrimmedTop (22).withTrimmedBottom (8);
        }
        static juce::Rectangle<int> row (juce::Rectangle<int>& inner, juce::Label& label, int h = rowH)
        {
            auto r = inner.removeFromTop (h);
            inner.removeFromTop (gap);
            label.setBounds (r.removeFromLeft (labelW));
            return r;
        }
    };

    //==============================================================================
    class GeneralPage : public Page
    {
    public:
        GeneralPage (juce::PropertiesFile& p) : props (p)
        {
            for (auto* l : { &startL, &updatesL, &recentL, &folderL }) { styleRowLabel (*l); addAndMakeVisible (l); }
            styleNote (folderNote);
            addAndMakeVisible (folderNote);
            startWith.addItem ("The Hub (recent songs and New Song)", 1);
            startWith.addItem ("The last song I had open", 2);
            startWith.onChange = [this] { props.setValue ("startWith", startWith.getSelectedId() == 2 ? "last" : "hub"); };
            addAndMakeVisible (startWith);
            updates.setButtonText ("Check for a newer Encore Basement Studio once a day");
            updates.onClick = [this] { props.setValue ("checkUpdates", updates.getToggleState()); };
            addAndMakeVisible (updates);
            clearRecent.onClick = [this] { recent::save (props, {}); refresh(); };
            addAndMakeVisible (clearRecent);
            folderPath.setReadOnly (true);
            addAndMakeVisible (folderPath);
            changeFolder.onClick = [this]
            {
                auto chooser = std::make_shared<juce::FileChooser> ("Where should Encore Basement Studio keep your songs?", juce::File (props.getValue ("projectsFolder")));
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser&)
                {
                    if (chooser->getResult() == juce::File()) return;
                    props.setValue ("projectsFolder", chooser->getResult().getFullPathName());
                    refresh();
                });
            };
            addAndMakeVisible (changeFolder);
            startGroup = &group ("Starting up");
            songsGroup = &group ("Songs");
            refresh();
        }
        void refresh()
        {
            startWith.setSelectedId (props.getValue ("startWith", "hub") == "last" ? 2 : 1, juce::dontSendNotification);
            updates.setToggleState (props.getBoolValue ("checkUpdates", true), juce::dontSendNotification);
            clearRecent.setButtonText ("Clear the recent list (" + juce::String (recent::load (props).size()) + ")");
            const auto& me = account::get();
            if (me.signedIn())
            {
                folderPath.setText (library::songsFolder().getFullPathName(), false);
                folderNote.setText ("New songs go in the current artist's Songs folder in your library. Account > Edit Profile changes where the library is.",
                                    juce::dontSendNotification);
                changeFolder.setEnabled (false);
            }
            else
            {
                folderPath.setText (props.getValue ("projectsFolder", "(not chosen yet)"), false);
                folderNote.setText ("Each song gets its own folder in here, with its recordings and imported audio.", juce::dontSendNotification);
                changeFolder.setEnabled (true);
            }
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto a = placeGroup (*startGroup, r, rowH * 2 + gap);
            startWith.setBounds (row (a, startL).removeFromLeft (320));
            updates.setBounds (row (a, updatesL));
            auto b = placeGroup (*songsGroup, r, rowH * 2 + gap * 2 + 34);
            auto f = row (b, folderL);
            changeFolder.setBounds (f.removeFromRight (100));
            f.removeFromRight (8);
            folderPath.setBounds (f);
            folderNote.setBounds (b.removeFromTop (34).withTrimmedLeft (labelW));
            b.removeFromTop (gap);
            clearRecent.setBounds (row (b, recentL).removeFromLeft (230));
        }
    private:
        juce::PropertiesFile& props;
        juce::GroupComponent* startGroup = nullptr;
        juce::GroupComponent* songsGroup = nullptr;
        juce::Label startL { {}, "When Encore Basement Studio opens" }, updatesL { {}, "Updates" }, recentL { {}, "Recent songs" }, folderL { {}, "Songs folder" }, folderNote;
        juce::ComboBox startWith;
        juce::ToggleButton updates;
        juce::TextButton clearRecent, changeFolder { "Change..." };
        juce::TextEditor folderPath;
    };

    //==============================================================================
    class AudioPage : public Page
    {
    public:
        AudioPage (te::Engine& e)
            : selector (e.getDeviceManager().deviceManager, 0, 512, 1, 512, false, false, true, false)
        {
            box = &group ("Audio interface");
            addAndMakeVisible (selector);
            styleNote (note);
            note.setText ("Pick your interface (its own ASIO driver is best when it has one). Every input becomes its own mono input on tracks "
                          "(Input 1, Input 2...). If the interface is unplugged, Studio says so and switches back when it returns.",
                          juce::dontSendNotification);
            addAndMakeVisible (note);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto inner = placeGroup (*box, r, getHeight() - 30 - 12 - 48);
            selector.setBounds (inner);
            note.setBounds (r.removeFromTop (44));
        }
    private:
        juce::GroupComponent* box = nullptr;
        juce::AudioDeviceSelectorComponent selector;
        juce::Label note;
    };

    //==============================================================================
    class RecordingPage : public Page
    {
    public:
        RecordingPage (juce::PropertiesFile& p, te::Engine& e) : props (p), engine (e)
        {
            for (auto* l : { &offsetL, &drumsL, &countL }) { styleRowLabel (*l); addAndMakeVisible (l); }
            for (auto* l : { &offsetNote }) { styleNote (*l); addAndMakeVisible (l); }
            offset.setSliderStyle (juce::Slider::LinearHorizontal);
            offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
            offset.setRange (-100.0, 100.0, 0.5);
            offset.setTextValueSuffix (" ms");
            offset.setValue (props.getDoubleValue ("recordOffsetMs", 0.0), juce::dontSendNotification);
            offset.onValueChange = [this] { props.setValue ("recordOffsetMs", offset.getValue()); if (StudioHooks::applyRecordOffset) StudioHooks::applyRecordOffset(); };
            offset.setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (offset);
            offsetNote.setText ("If recordings land a little late or early against what you played along to, move them by this much. "
                                "Studio already allows for what your interface reports, so most people leave this at 0.", juce::dontSendNotification);
            drums.setButtonText ("Snap drum hits played from pads or a keyboard to the grid (1/16)");
            drums.setToggleState (props.getBoolValue ("drumRecordSnap", true), juce::dontSendNotification);
            drums.onClick = [this] { props.setValue ("drumRecordSnap", drums.getToggleState()); };
            addAndMakeVisible (drums);
            count.addItemList ({ "No count-in", "1 beat", "2 beats", "1 bar", "2 bars" }, 1);
            // the engine keeps the count-in (one setting for every song)
            static const int modes[] = { 0, 4, 3, 1, 2 };   // the list's order, as the engine numbers them (none, 1 bar, 2 bars, 2 beats, 1 beat)
            const int now = (int) engine.getPropertyStorage().getProperty (te::SettingID::countInMode);
            for (int i = 0; i < 5; ++i) if (modes[i] == now) count.setSelectedId (i + 1, juce::dontSendNotification);
            count.onChange = [this] { engine.getPropertyStorage().setProperty (te::SettingID::countInMode, modes[juce::jlimit (0, 4, count.getSelectedId() - 1)]); };
            addAndMakeVisible (count);
            timing = &group ("Timing");
            midi = &group ("Drums and MIDI");
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto a = placeGroup (*timing, r, rowH * 2 + gap * 2 + 34);
            offset.setBounds (row (a, offsetL).removeFromLeft (360));
            offsetNote.setBounds (a.removeFromTop (34).withTrimmedLeft (labelW));
            a.removeFromTop (gap);
            count.setBounds (row (a, countL).removeFromLeft (180));
            auto b = placeGroup (*midi, r, rowH);
            drums.setBounds (row (b, drumsL));
        }
    private:
        juce::PropertiesFile& props;
        te::Engine& engine;
        juce::GroupComponent* timing = nullptr;
        juce::GroupComponent* midi = nullptr;
        juce::Label offsetL { {}, "Recording offset" }, drumsL { {}, "Drum recording" }, countL { {}, "Count-in before recording" }, offsetNote;
        juce::Slider offset;
        juce::ToggleButton drums;
        juce::ComboBox count;
    };

    //==============================================================================
    class PluginsPage : public Page, private juce::ListBoxModel
    {
    public:
        PluginsPage (juce::PropertiesFile& p) : props (p)
        {
            folders = &group ("Plug-in folders");
            scanning = &group ("Finding plug-ins");
            list.setModel (this);
            list.setRowHeight (22);
            addAndMakeVisible (list);
            styleNote (note);
            note.setText ("Always searched: the standard VST3 and VST2 folders, and the 32-bit ones (run through the 32-bit bridge). "
                          "Add any other folder you keep plug-ins in.", juce::dontSendNotification);
            addAndMakeVisible (note);
            addFolder.onClick = [this]
            {
                auto chooser = std::make_shared<juce::FileChooser> ("A folder with plug-ins in it (VST3 or VST2)", juce::File());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser&)
                {
                    if (chooser->getResult() == juce::File()) return;
                    auto f = added();
                    f.addIfNotAlreadyThere (chooser->getResult().getFullPathName());
                    props.setValue ("pluginFolders", f.joinIntoString ("\n"));
                    list.updateContent(); list.repaint();
                    if (StudioHooks::findPlugins) StudioHooks::findPlugins();
                });
            };
            removeFolder.onClick = [this]
            {
                auto f = added();
                f.remove (list.getSelectedRow());
                props.setValue ("pluginFolders", f.joinIntoString ("\n"));
                list.updateContent(); list.repaint();
            };
            findNow.onClick = [] { if (StudioHooks::findPlugins) StudioHooks::findPlugins(); };
            manager.onClick = [] { if (StudioHooks::showPluginManager) StudioHooks::showPluginManager(); };
            for (auto* b : { &addFolder, &removeFolder, &findNow, &manager }) addAndMakeVisible (b);
            styleNote (scanNote);
            scanNote.setText ("Studio looks for new plug-ins each time it starts, each one checked in a separate helper so a crashing plug-in can't take Studio down. "
                              "Plug-ins that show a window while being checked (an activation prompt) are set aside: Plugin Manager lists them.", juce::dontSendNotification);
            addAndMakeVisible (scanNote);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto a = placeGroup (*folders, r, 150 + gap + 26 + 40);
            auto buttons = a.removeFromRight (110);
            list.setBounds (a.removeFromTop (150));
            addFolder.setBounds (buttons.removeFromTop (26)); buttons.removeFromTop (6);
            removeFolder.setBounds (buttons.removeFromTop (26));
            a.removeFromTop (gap);
            note.setBounds (a.removeFromTop (40));
            auto b = placeGroup (*scanning, r, 26 + gap + 48);
            auto bb = b.removeFromTop (26);
            findNow.setBounds (bb.removeFromLeft (170)); bb.removeFromLeft (8);
            manager.setBounds (bb.removeFromLeft (170));
            b.removeFromTop (gap);
            scanNote.setBounds (b);
        }
    private:
        juce::PropertiesFile& props;
        juce::GroupComponent* folders = nullptr;
        juce::GroupComponent* scanning = nullptr;
        juce::ListBox list;
        juce::Label note, scanNote;
        juce::TextButton addFolder { "Add..." }, removeFolder { "Remove" }, findNow { "Find New Plug-ins Now" }, manager { "Plugin Manager..." };
        juce::StringArray added() const { auto f = juce::StringArray::fromLines (props.getValue ("pluginFolders")); f.removeEmptyStrings(); return f; }
        int getNumRows() override { return added().size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (selected) g.fillAll (theme::accent.withAlpha (0.3f));
            g.setColour (theme::paper);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (added()[row], 6, 0, w - 12, h, juce::Justification::centredLeft);
        }
    };

    //==============================================================================
    class ShortcutsPage : public Page
    {
    public:
        ShortcutsPage (juce::ApplicationCommandManager& cm, std::function<juce::String (const juce::KeyPress&)> keyName)
            : commands (cm), editor (*cm.getKeyMappings(), true), keyText (std::move (keyName))
        {
            box = &group ("Keyboard shortcuts");
            addAndMakeVisible (editor);
            styleNote (note);
            note.setText ("Click + next to an action to give it a key, or click a key to change or remove it. "
                          "\"Reset to Defaults\" puts every shortcut back. Changes count once you press OK or Apply.", juce::dontSendNotification);
            addAndMakeVisible (note);
            saveList.onClick = [this] { saveAsText(); };
            addAndMakeVisible (saveList);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (32);
            saveList.setBounds (bottom.removeFromLeft (200).withSizeKeepingCentre (200, 26));
            r.removeFromBottom (6);
            auto inner = placeGroup (*box, r, r.getHeight() - 30 - 12 - 40);
            note.setBounds (inner.removeFromTop (36));
            editor.setBounds (inner);
        }
        /** Every action with its keys, by menu, as text */
        juce::String listAsText()
        {
            juce::String out ("Encore: Basement Studio keyboard shortcuts\n\n");
            for (auto& cat : commands.getCommandCategories())
            {
                out << cat << "\n";
                for (auto id : commands.getCommandsInCategory (cat))
                {
                    auto* info = commands.getCommandForID (id);
                    if (info == nullptr) continue;
                    juce::StringArray keys;
                    for (auto& k : commands.getKeyMappings()->getKeyPressesAssignedToCommand (id)) keys.add (keyText (k));
                    out << "  " << info->shortName.paddedRight (' ', 44) << (keys.isEmpty() ? juce::String ("-") : keys.joinIntoString (", ")) << "\n";
                }
                out << "\n";
            }
            return out;
        }
    private:
        juce::ApplicationCommandManager& commands;
        juce::KeyMappingEditorComponent editor;
        std::function<juce::String (const juce::KeyPress&)> keyText;
        juce::GroupComponent* box = nullptr;
        juce::Label note;
        juce::TextButton saveList { "Save the List as Text..." };
        void saveAsText()
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Save the shortcut list",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Encore Basement Studio shortcuts.txt"), "*.txt");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, chooser] (const juce::FileChooser&)
            {
                auto f = chooser->getResult();
                if (f == juce::File()) return;
                f.replaceWithText (listAsText());
                f.revealToUser();
            });
        }
    };

    //==============================================================================
    /** The page names down the left */
    class PageList : public juce::Component
    {
    public:
        juce::StringArray names;
        int selected = 0;
        std::function<void (int)> onSelect;
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::subtle);
            g.setColour (theme::strong);
            g.fillRect (getLocalBounds().removeFromRight (1));
            for (int i = 0; i < names.size(); ++i)
            {
                auto r = rowBounds (i);
                if (i == selected)
                {
                    g.setColour (theme::accent.withAlpha (0.22f));
                    g.fillRect (r);
                    g.setColour (theme::accent);
                    g.fillRect (r.removeFromLeft (3));
                }
                g.setColour (i == selected ? theme::paper : theme::text2);
                g.setFont (juce::FontOptions (14.0f));
                g.drawText (names[i], rowBounds (i).withTrimmedLeft (16), juce::Justification::centredLeft);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < names.size(); ++i)
                if (rowBounds (i).contains (e.getPosition()) && onSelect) onSelect (i);
        }
    private:
        juce::Rectangle<int> rowBounds (int i) const { return { 0, 10 + i * 32, getWidth() - 1, 32 }; }
    };

    //==============================================================================
    class PreferencesComponent : public juce::Component
    {
    public:
        std::function<void()> onClosed;

        PreferencesComponent (juce::PropertiesFile& p, te::Engine& e, juce::ApplicationCommandManager& cm,
                              std::function<juce::String (const juce::KeyPress&)> keyName, const juce::String& startPage)
            : props (p), engine (e), commands (cm)
        {
            setLookAndFeel (&look.get());
            pages.add (new GeneralPage (props));
            pages.add (new AudioPage (engine));
            pages.add (new RecordingPage (props, engine));
            pages.add (new PluginsPage (props));
            pages.add (new ShortcutsPage (commands, std::move (keyName)));
            list.names = { "General", "Audio", "Recording", "Plug-ins", "Keyboard Shortcuts" };
            list.onSelect = [this] (int i) { showPage (i); };
            addAndMakeVisible (list);
            for (auto* pg : pages) addChildComponent (pg);
            ok.onClick = [this] { apply(); close(); };
            cancel.onClick = [this] { revert(); close(); };
            applyButton.onClick = [this] { apply(); };
            for (auto* b : { &ok, &cancel, &applyButton }) addAndMakeVisible (b);
            ok.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.35f));
            snapshot();
            showPage (juce::jmax (0, list.names.indexOf (startPage)));
            setSize (860, 640);
        }
        ~PreferencesComponent() override
        {
            if (! closed) revert();   // closing the window with its X is Cancel, as in any Windows program
            if (onClosed) onClosed();
            setLookAndFeel (nullptr);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::surface);
            g.setColour (theme::strong);
            g.fillRect (getLocalBounds().removeFromBottom (52).removeFromTop (1));
            g.setColour (theme::paper);
            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.drawText (list.names[list.selected], titleArea, juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (52).reduced (16, 12);
            applyButton.setBounds (bottom.removeFromRight (90)); bottom.removeFromRight (8);
            cancel.setBounds (bottom.removeFromRight (90)); bottom.removeFromRight (8);
            ok.setBounds (bottom.removeFromRight (90));
            list.setBounds (r.removeFromLeft (190));
            auto content = r.reduced (20, 14);
            titleArea = content.removeFromTop (30);
            content.removeFromTop (8);
            for (auto* pg : pages) pg->setBounds (content);
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::escapeKey) { revert(); close(); return true; }
            return false;
        }

    private:
        juce::PropertiesFile& props;
        te::Engine& engine;
        juce::ApplicationCommandManager& commands;
        juce::SharedResourcePointer<DialogLookAndFeel> look;
        juce::OwnedArray<juce::Component> pages;
        PageList list;
        juce::Rectangle<int> titleArea;
        juce::TextButton ok { "OK" }, cancel { "Cancel" }, applyButton { "Apply" };
        bool closed = false;

        // what to go back to on Cancel
        juce::StringPairArray savedProps;
        std::unique_ptr<juce::XmlElement> savedKeys, savedAudio;

        void showPage (int i)
        {
            list.selected = i;
            for (int p = 0; p < pages.size(); ++p) pages[p]->setVisible (p == i);
            list.repaint();
            repaint();
        }
        void snapshot()
        {
            savedProps = props.getAllProperties();
            savedKeys = commands.getKeyMappings()->createXml (false);
            savedAudio = engine.getDeviceManager().deviceManager.createStateXml();
        }
        void apply()
        {
            props.setValue ("keyMappings", commands.getKeyMappings()->createXml (true)->toString());
            props.saveIfNeeded();
            commands.commandStatusChanged();
            snapshot();
        }
        void revert()
        {
            for (auto& k : props.getAllProperties().getAllKeys())
                if (! savedProps.containsKey (k) && k != "keyMappings") props.removeValue (k);
            for (auto& k : savedProps.getAllKeys())
                if (props.getValue (k) != savedProps[k]) props.setValue (k, savedProps[k]);
            if (savedKeys != nullptr) commands.getKeyMappings()->restoreFromXml (*savedKeys);
            auto& dm = engine.getDeviceManager().deviceManager;
            if (auto now = dm.createStateXml(); savedAudio != nullptr && (now == nullptr || ! now->isEquivalentTo (savedAudio.get(), true)))
                dm.initialise (0, 512, savedAudio.get(), true);
            if (StudioHooks::applyRecordOffset) StudioHooks::applyRecordOffset();
            commands.commandStatusChanged();
        }
        void close()
        {
            closed = true;
            if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (0);
        }
    };
}
