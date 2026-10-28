// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The main window: a song (a Tracktion "Edit") with its tracks, the transport,
// and the things you do to a song (record, import, add effects, export).
// Every song lives in its own folder inside your projects folder:
//
//   <Projects>/<Song>/<Song>.basement   the song itself (XML)
//   <Projects>/<Song>/Recordings/       everything recorded into it
//   <Projects>/<Song>/Audio/            copies of imported files
//   <Projects>/<Song>/<Song> mix.wav    exports (wherever you choose)
//
// so a song can be zipped, backed up or moved to another drive in one piece.
#pragma once

#include "Theme.h"


//==============================================================================
/** Engine hooks: recordings go into the song's own folder */
class BasementBehaviour : public te::EngineBehaviour
{
public:
    BasementBehaviour (bool openAudioLater = false) : deferDevices (openAudioLater) {}
    std::function<juce::File()> songFolder;

    /** The app opens the audio interface itself once its window is on screen
        (it's the slowest part of starting up), so the window appears at once */
    bool autoInitialiseDeviceManager() override { return ! deferDevices; }
    const bool deferDevices;

    juce::File getFileForNewAudioRecording (te::Track& t, const juce::String& ext) override
    {
        auto dir = songFolder ? songFolder() : juce::File();
        if (dir == juce::File()) return {};
        auto rec = dir.getChildFile ("Recordings");
        rec.createDirectory();
        auto base = juce::File::createLegalFileName (t.getName().trim().isEmpty() ? juce::String ("Track") : t.getName().trim());
        for (int take = 1;; ++take)
        {
            auto f = rec.getChildFile (base + " take " + juce::String (take) + ext);
            if (! f.exists()) return f;
        }
    }
};

//==============================================================================
/** Opening and creating songs.
    Tracktion stores a clip's file relative to the song file, written with
    getRelativePathFrom (songFile) (which treats the song file as a folder), but
    its default resolver reads it back with getSiblingFile (relative to the song's
    folder): one ".." off, so clips go missing after a song is reopened. Basement
    reads paths back the way they were written (and accepts the other way too). */
namespace songs
{
    inline te::Edit::FilePathResolver resolverFor (const juce::File& songFile)
    {
        return [songFile] (const juce::String& path) -> juce::File
        {
            if (juce::File::isAbsolutePath (path)) return path;
            auto asWritten = songFile.getChildFile (path);
            if (asWritten.exists()) return asWritten;
            auto sibling = songFile.getSiblingFile (path);
            return sibling.exists() ? sibling : asWritten;
        };
    }

    inline std::unique_ptr<te::Edit> open (te::Engine& engine, const juce::File& songFile, te::Edit::EditRole role = te::Edit::forEditing)
    {
        auto state = te::loadEditFromFile (engine, songFile, te::ProjectItemRef{});
        if (! state.isValid()) return {};
        auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);
        if (! id.isValid()) id = te::ProjectItemID::createNewID (te::ProjectID{});
        return te::Edit::createEdit (te::Edit::Options { engine, state, id, role, nullptr, te::Edit::getDefaultNumUndoLevels(),
                                                          [songFile] { return songFile; }, resolverFor (songFile) });
    }

    inline std::unique_ptr<te::Edit> create (te::Engine& engine, const juce::File& songFile)
    {
        auto id = te::ProjectItemID::createNewID (te::ProjectID{});
        return te::Edit::createEdit (te::Edit::Options { engine, te::loadEditFromFile (engine, juce::File(), id), id, te::Edit::forEditing, nullptr,
                                                          te::Edit::getDefaultNumUndoLevels(), [songFile] { return songFile; }, resolverFor (songFile) });
    }
}

//==============================================================================
/** Mixes a saved song down to a 24-bit stereo WAV on a background thread.
    The song is reopened in Tracktion's render mode (as its own render tests
    do), so exporting never touches the song you're working on.
    done(file, error) is called on the message thread. */
struct SongExport
{
    std::unique_ptr<te::Edit> edit;
    std::shared_ptr<te::EditRenderer::Handle> handle;
    float progress() const { return handle ? handle->getProgress() : 0.0f; }
};

inline std::shared_ptr<SongExport> exportSong (te::Engine& engine, const juce::File& songFile, const juce::File& dest, double sampleRate,
                                               std::function<void (juce::File, juce::String)> done)
{
    auto job = std::make_shared<SongExport>();
    job->edit = songs::open (engine, songFile, te::Edit::EditRole::forRendering);
    if (job->edit == nullptr || job->edit->getLength() <= 0s) { done ({}, "There's nothing in the song to export yet."); return {}; }

    te::Renderer::Parameters p (*job->edit);
    p.destFile = dest;
    p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 24;
    p.sampleRateForAudio = sampleRate;
    p.time = { 0_tp, job->edit->getLength() };
    p.tracksToDo = te::toBitSet (te::getAllTracks (*job->edit));
    p.usePlugins = true;
    p.useMasterPlugins = true;
    p.canRenderInMono = false;
    p.ditheringEnabled = true;
    dest.deleteFile();
    std::weak_ptr<SongExport> weak = job;
    job->handle = te::EditRenderer::render (p, [weak, done] (tl::expected<juce::File, std::string> res)
    {
        juce::MessageManager::callAsync ([weak, done, res]
        {
            done (res ? *res : juce::File(), res ? juce::String() : juce::String (res.error()));
            if (auto j = weak.lock()) { j->handle = nullptr; }
        });
    });
    return job;
}

//==============================================================================
/** Long jobs (exporting a mix, analysing tempo...). Tracktion leaves running
    them to the app: with a window we show a progress bar, without one
    (the self-test) we just run them. */
struct HeadlessUIBehaviour : public te::UIBehaviour
{
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& t) override
    {
        while (t.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain) {}
        if (auto* r = dynamic_cast<te::Renderer::RenderTask*> (&t); r != nullptr && r->errorMessage.isNotEmpty())
            warnings.add ("render: " + r->errorMessage);
    }
    void showWarningMessage (const juce::String& m) override { warnings.add (m); }
    juce::StringArray warnings;
};

class BasementUIBehaviour : public ExtendedUIBehaviour
{
public:
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& t) override
    {
        struct Window : public juce::ThreadWithProgressWindow
        {
            Window (te::ThreadPoolJobWithProgress& j) : ThreadWithProgressWindow (j.getJobName(), true, false), job (j) {}
            void run() override
            {
                while (! threadShouldExit())
                {
                    if (job.runJob() != juce::ThreadPoolJob::jobNeedsRunningAgain) break;
                    setProgress (job.getCurrentTaskProgress());
                }
            }
            te::ThreadPoolJobWithProgress& job;
        };
        Window w (t);
        w.runThread();
    }

    void showWarningMessage (const juce::String& message) override
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Basement", message);
    }
};


#include "Hub.h"

//==============================================================================
/** Small dialogs: Song Setup and Preferences */
class SongSetupComponent : public juce::Component
{
public:
    std::function<void (SongInfo)> onSave;

    SongSetupComponent (const SongInfo& i) : info (i)
    {
        fields.set (i);
        addAndMakeVisible (fields);
        hubui::styleLabel (format, 13.0f, theme::muted);
        format.setText ("Recorded at " + i.formatText() + ".  Started " + i.created.formatted ("%b %d, %Y"), juce::dontSendNotification);
        addAndMakeVisible (format);
        saveButton.setColour (juce::TextButton::buttonColourId, theme::brand);
        saveButton.onClick = [this] { if (onSave) onSave (fields.get (info)); close(); };
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (saveButton);
        addAndMakeVisible (cancelButton);
        setSize (640, SongFields::heightFor() + 120);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::surface); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (24);
        fields.setBounds (r.removeFromTop (SongFields::heightFor()));
        r.removeFromTop (10);
        format.setBounds (r.removeFromTop (20));
        auto b = r.removeFromBottom (36);
        saveButton.setBounds (b.removeFromRight (110));
        b.removeFromRight (8);
        cancelButton.setBounds (b.removeFromRight (90));
    }

private:
    SongInfo info;
    SongFields fields;
    juce::Label format;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };
    void close() { if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (0); }
};

class PreferencesComponent : public juce::Component
{
public:
    PreferencesComponent (juce::PropertiesFile& p) : props (p)
    {
        for (auto* l : { &startL, &folderL, &folderPath, &recentL })
        {
            hubui::styleLabel (*l);
            addAndMakeVisible (l);
        }
        hubui::styleLabel (folderPath, 13.0f, theme::paper);
        startWith.addItem ("The Hub (recent songs and New song)", 1);
        startWith.addItem ("The last song I had open", 2);
        startWith.setSelectedId (props.getValue ("startWith", "hub") == "last" ? 2 : 1, juce::dontSendNotification);
        startWith.onChange = [this] { props.setValue ("startWith", startWith.getSelectedId() == 2 ? "last" : "hub"); };
        hubui::styleCombo (startWith);
        addAndMakeVisible (startWith);
        changeFolder.onClick = [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Where should Basement keep your songs?", juce::File (props.getValue ("projectsFolder")));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser&)
            {
                if (chooser->getResult() == juce::File()) return;
                props.setValue ("projectsFolder", chooser->getResult().getFullPathName());
                update();
            });
        };
        clearRecent.onClick = [this] { recent::save (props, {}); update(); };
        addPluginFolder.onClick = [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("A folder with plugins in it (VST3)", juce::File());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser&)
            {
                if (chooser->getResult() == juce::File()) return;
                auto folders = juce::StringArray::fromLines (props.getValue ("pluginFolders"));
                folders.removeEmptyStrings();
                folders.addIfNotAlreadyThere (chooser->getResult().getFullPathName());
                props.setValue ("pluginFolders", folders.joinIntoString ("\n"));
                update();
                if (BasementHooks::findPlugins) BasementHooks::findPlugins();
            });
        };
        clearPluginFolders.onClick = [this] { props.setValue ("pluginFolders", juce::String()); update(); };
        for (auto* b : { &changeFolder, &clearRecent, &addPluginFolder, &clearPluginFolders }) addAndMakeVisible (b);
        hubui::styleLabel (pluginsL);
        hubui::styleLabel (pluginFolders, 12.0f, theme::paper);
        pluginFolders.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (pluginsL);
        addAndMakeVisible (pluginFolders);
        update();
        setSize (660, 340);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::surface); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (24);
        auto row = [&r] { auto x = r.removeFromTop (34); r.removeFromTop (16); return x; };
        auto a = row(); startL.setBounds (a.removeFromLeft (150)); startWith.setBounds (a);
        auto b = row(); folderL.setBounds (b.removeFromLeft (150)); changeFolder.setBounds (b.removeFromRight (100)); folderPath.setBounds (b.withTrimmedRight (8));
        auto c = row(); recentL.setBounds (c.removeFromLeft (150)); clearRecent.setBounds (c.removeFromLeft (180));
        auto d = r.removeFromTop (90);
        pluginsL.setBounds (d.removeFromLeft (150).removeFromTop (34));
        auto buttons = d.removeFromTop (34);
        addPluginFolder.setBounds (buttons.removeFromLeft (170));
        buttons.removeFromLeft (8);
        clearPluginFolders.setBounds (buttons.removeFromLeft (120));
        pluginFolders.setBounds (d.withTrimmedTop (6));
    }

private:
    juce::PropertiesFile& props;
    juce::ComboBox startWith;
    juce::TextButton changeFolder { "Change..." }, clearRecent { "Clear recent songs" }, addPluginFolder { "Add a plugin folder..." }, clearPluginFolders { "Clear added" };
    juce::Label startL { {}, "When Basement opens" }, folderL { {}, "Songs folder" }, folderPath, recentL { {}, "Recent list" },
                pluginsL { {}, "Plugin folders" }, pluginFolders;
    void update()
    {
        folderPath.setText (props.getValue ("projectsFolder", "(not chosen yet)"), juce::dontSendNotification);
        clearRecent.setButtonText ("Clear recent songs (" + juce::String (recent::load (props).size()) + ")");
        auto added = juce::StringArray::fromLines (props.getValue ("pluginFolders"));
        added.removeEmptyStrings();
        pluginFolders.setText ("Always searched: the standard VST3 folders. "
                               + (added.isEmpty() ? juce::String ("Add any other folder you keep plugins in.") : "Also: " + added.joinIntoString ("; ")),
                               juce::dontSendNotification);
    }
};

/** Transport > Metronome Settings: how loud, accent on beat one, when it plays, count-in */
class MetronomeComponent : public juce::Component
{
public:
    MetronomeComponent (te::Edit& e) : edit (e)
    {
        for (auto* l : { &volumeL, &countL }) { hubui::styleLabel (*l); addAndMakeVisible (l); }
        volume.setRange (0.2, 1.0, 0.01);
        volume.setValue (edit.getClickTrackVolume(), juce::dontSendNotification);
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.onValueChange = [this] { edit.setClickTrackVolume ((float) volume.getValue()); };
        addAndMakeVisible (volume);
        accent.setToggleState (edit.clickTrackEmphasiseBars.get(), juce::dontSendNotification);
        accent.onClick = [this] { edit.clickTrackEmphasiseBars = accent.getToggleState(); };
        recordingOnly.setToggleState (edit.clickTrackRecordingOnly.get(), juce::dontSendNotification);
        recordingOnly.onClick = [this] { edit.clickTrackRecordingOnly = recordingOnly.getToggleState(); };
        enabled.setToggleState (edit.clickTrackEnabled.get(), juce::dontSendNotification);
        enabled.onClick = [this] { edit.clickTrackEnabled = enabled.getToggleState(); };
        for (auto* b : { &enabled, &accent, &recordingOnly })
        {
            b->setColour (juce::ToggleButton::textColourId, theme::paper);
            b->setColour (juce::ToggleButton::tickColourId, theme::accent);
            addAndMakeVisible (b);
        }
        count.addItemList ({ "No count-in", "1 beat", "2 beats", "1 bar", "2 bars" }, 1);
        auto mode = edit.getCountInMode();
        count.setSelectedId (mode == te::Edit::CountIn::none ? 1 : mode == te::Edit::CountIn::oneBeat ? 2 : mode == te::Edit::CountIn::twoBeat ? 3
                             : mode == te::Edit::CountIn::oneBar ? 4 : 5, juce::dontSendNotification);
        count.onChange = [this]
        {
            const te::Edit::CountIn modes[] = { te::Edit::CountIn::none, te::Edit::CountIn::oneBeat, te::Edit::CountIn::twoBeat, te::Edit::CountIn::oneBar, te::Edit::CountIn::twoBar };
            edit.setCountInMode (modes[juce::jlimit (0, 4, count.getSelectedId() - 1)]);
        };
        hubui::styleCombo (count);
        addAndMakeVisible (count);
        setSize (440, 250);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::surface); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 20);
        enabled.setBounds (r.removeFromTop (28)); r.removeFromTop (10);
        auto v = r.removeFromTop (28); volumeL.setBounds (v.removeFromLeft (110)); volume.setBounds (v); r.removeFromTop (10);
        accent.setBounds (r.removeFromTop (28)); r.removeFromTop (6);
        recordingOnly.setBounds (r.removeFromTop (28)); r.removeFromTop (12);
        auto c = r.removeFromTop (30); countL.setBounds (c.removeFromLeft (110)); count.setBounds (c.removeFromLeft (180));
    }

private:
    te::Edit& edit;
    juce::Slider volume;
    juce::ToggleButton enabled { "Click on" }, accent { "Accent the first beat of each bar" }, recordingOnly { "Only while recording" };
    juce::ComboBox count;
    juce::Label volumeL { {}, "Volume" }, countL { {}, "Count-in" };
};

/** The big count-in numbers over the song while the click counts you in */
class CountInOverlay : public juce::Component, private juce::Timer
{
public:
    CountInOverlay() { setInterceptsMouseClicks (false, false); }
    void start (int beats, double secondsPerBeat)
    {
        total = beats; spb = secondsPerBeat; started = juce::Time::getMillisecondCounterHiRes();
        setVisible (true); startTimerHz (30); repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
        const int beat = (int) (elapsed / spb);
        if (beat >= total) return;
        const float pulse = 1.0f - (float) std::fmod (elapsed / spb, 1.0);
        auto box = getLocalBounds().withSizeKeepingCentre (220, 220).toFloat();
        g.setColour (theme::base.withAlpha (0.75f));
        g.fillRoundedRectangle (box, 24.0f);
        g.setColour (theme::record.withAlpha (0.5f + 0.5f * pulse));
        g.drawRoundedRectangle (box.reduced (2.0f), 24.0f, 3.0f);
        g.setColour (theme::paper);
        g.setFont (juce::FontOptions (120.0f, juce::Font::bold));
        g.drawText (juce::String (beat + 1), box.toNearestInt().withTrimmedBottom (30), juce::Justification::centred);
        g.setColour (theme::text2);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("Recording in " + juce::String (total - beat), box.toNearestInt().removeFromBottom (54), juce::Justification::centred);
    }
private:
    int total = 0;
    double spb = 0.5, started = 0;
    void timerCallback() override
    {
        if ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 >= total * spb) { stopTimer(); setVisible (false); }
        repaint();
    }
};

inline void showDialog (const juce::String& title, juce::Component* content, juce::Component* over)
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = title;
    o.dialogBackgroundColour = theme::surface;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = over;
    o.content.setOwned (content);
    o.launchAsync();
}

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::KeyListener,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    std::function<void (const juce::String&)> onTitleChange;

    enum Commands
    {
        // File
        cmdHub = 0x2000, cmdNew, cmdOpen, cmdClose, cmdSave, cmdSaveAs, cmdSaveVersion, cmdSaveTemplate, cmdRevert,
        cmdImportAudio, cmdExportMix, cmdExportZip, cmdPreferences, cmdQuit,
        // Edit
        cmdUndo, cmdRedo, cmdCut, cmdCopy, cmdPaste, cmdDuplicate, cmdDelete, cmdSplit, cmdSelectAll, cmdSelectNone, cmdMuteClip,
        cmdNudgeLeft, cmdNudgeRight, cmdSnapOff, cmdSnapBar, cmdSnapBeat, cmdSnapEighth, cmdSnapSixteenth, cmdRenameClip,
        // Project
        cmdAddTrack, cmdDuplicateTrack, cmdRenameTrack, cmdRemoveTrack, cmdSongSetup,
        // Audio
        cmdNormalize, cmdGainUp, cmdGainDown, cmdGainReset, cmdFadeIn, cmdFadeOut, cmdRemoveFades, cmdReverse,
        // Media
        cmdShowSongFolder, cmdShowRecordings, cmdShowAudio, cmdChangeProjectsFolder,
        // Transport
        cmdPlay, cmdRecord, cmdToStart, cmdToEnd, cmdBarBack, cmdBarForward, cmdLoop, cmdLoopToSelection, cmdClick,
        cmdCountNone, cmdCount1Beat, cmdCount2Beats, cmdCount1Bar, cmdCount2Bars, cmdMetronome, cmdPunch,
        // Studio
        cmdAudioSetup, cmdPlugins, cmdFindPlugins, cmdReassignInputs,
        // Window
        cmdEffects, cmdZoomIn, cmdZoomOut, cmdZoomFit, cmdHeightSmall, cmdHeightMedium, cmdHeightLarge, cmdHeightHuge,
        // Help
        cmdShortcuts, cmdGuide, cmdReport, cmdUpdates, cmdLogFolder, cmdAbout
    };

    MainComponent()
    {
        basementLog ("startup: engine ready in " + juce::String (juce::Time::getMillisecondCounterHiRes() - constructionStarted, 0) + " ms");
        behaviour->songFolder = [this] { return songFile.getParentDirectory(); };

        commandManager.registerAllCommandsForTarget (this);
        commandManager.setFirstCommandTarget (this);
        setApplicationCommandManagerToWatch (&commandManager);
        addAndMakeVisible (menuBar);

        hub = std::make_unique<HubComponent> (engine, props());
        hub->onOpen = [this] (juce::File f) { loadSong (f); };
        hub->onOpenOther = [this] { openSong(); };
        hub->onBack = [this] { showHub (false); };
        hub->onAudioSetup = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        hub->onCreate = [this] (SongPlan p) { createSong (p); };
        addChildComponent (*hub);

        for (auto* b : { &homeButton, &playButton, &recordButton, &loopButton, &clickButton, &addTrackButton, &importButton, &exportButton })
        {
            addChildComponent (b);
            b->setMouseClickGrabsKeyboardFocus (false);
        }
        for (auto* l : { &timeLabel, &barLabel, &tempoLabel, &tempoCaption, &statusLabel })
            addChildComponent (l);
        addChildComponent (masterMeter);
        addChildComponent (countIn);
        deviceLabel.setColour (juce::Label::textColourId, theme::muted);
        deviceLabel.setJustificationType (juce::Justification::centredRight);
        deviceLabel.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (deviceLabel);
        statusLabel.setFont (juce::FontOptions (12.0f));
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (snapBox);
        snapBox.addItemList ({ "Snap: Off", "Snap: Bar", "Snap: Beat", "Snap: 1/8", "Snap: 1/16" }, 1);
        snapBox.setTooltip ("What clips line up to when you drag them (hold Shift to ignore it)");
        snapBox.setColour (juce::ComboBox::textColourId, theme::paper);
        snapBox.setMouseClickGrabsKeyboardFocus (false);
        snapBox.onChange = [this] { setSnap (snapBox.getSelectedId() - 1); };
        masterMeter.setTooltip ("The whole mix. Red at the end means it clipped: click to reset.");

        exportButton.setColour (juce::TextButton::buttonColourId, theme::brand);
        recordButton.setColour (juce::TextButton::buttonOnColourId, theme::record);
        recordButton.setColour (juce::TextButton::textColourOnId, theme::paper);

        homeButton.setTooltip ("Back to the start (Home)");
        playButton.setTooltip ("Play / stop (Space)");
        recordButton.setTooltip ("Record on the armed tracks (R)");
        loopButton.setTooltip ("Loop the selected clips, or the whole song (L)");
        clickButton.setTooltip ("Metronome click (C)");
        addTrackButton.setTooltip ("Add a track to record on (Ctrl+T)");
        importButton.setTooltip ("Add audio files as new tracks, or drag them in (Ctrl+I)");
        exportButton.setTooltip ("Mix the whole song down to a WAV (Ctrl+E)");

        timeLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        barLabel.setFont (juce::FontOptions (13.0f));
        barLabel.setColour (juce::Label::textColourId, theme::text2);
        tempoCaption.setFont (juce::FontOptions (11.0f));
        tempoCaption.setColour (juce::Label::textColourId, theme::muted);
        tempoLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        tempoLabel.setEditable (false, true, false);
        tempoLabel.setTooltip ("Tempo: double-click to type a new one");
        tempoLabel.onTextChange = [this] { setTempo (tempoLabel.getText().getDoubleValue()); };
        statusLabel.setColour (juce::Label::textColourId, theme::muted);

        homeButton.onClick = [this] { invoke (cmdToStart); };
        playButton.onClick = [this] { invoke (cmdPlay); };
        recordButton.onClick = [this] { invoke (cmdRecord); };
        loopButton.onClick = [this] { invoke (cmdLoop); };
        clickButton.onClick = [this] { invoke (cmdClick); };
        addTrackButton.onClick = [this] { invoke (cmdAddTrack); };
        importButton.onClick = [this] { invoke (cmdImportAudio); };
        exportButton.onClick = [this] { invoke (cmdExportMix); };

        BasementHooks::commands = &commandManager;
        BasementHooks::fillClipMenu = [this] (juce::PopupMenu& m, te::Clip& c) { fillClipMenu (m, c); };
        BasementHooks::findPlugins = [this] { startPluginSearch(); };
        BasementHooks::showPluginManager = [this] { showPluginList(); };
        BasementHooks::pluginScanStatus = [this] { return pluginFinder.status(); };
        pluginFinder.onProgress = [this] (juce::String s) { status (s); };
        pluginFinder.onFinished = [this] (int found)
        {
            status (found > 0 ? "Found " + juce::String (found) + (found == 1 ? " new plugin" : " new plugins") + ": they're in every track's Effects menu"
                              : juce::String ("Plugins up to date"));
            commandManager.commandStatusChanged();
        };
        // look for new plugins once things have settled (the window is up first)
        juce::Timer::callAfterDelay (2500, [safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->startPluginSearch(); });

        // the interface opens a moment after startup; then see if it has its own ASIO driver
        juce::Timer::callAfterDelay (1200, [safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->preferInterfaceAsio(); });
        setupInputs();
        engine.getDeviceManager().addChangeListener (this);
        setSize (1400, 860);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            // set after the first song has opened (and checked for a crash), cleared on a clean exit
            if (safe) { safe->props().setValue ("running", true); safe->props().saveIfNeeded(); }
        });
        startTimerHz (20);

        crashedLastSession = props().getBoolValue ("running", false);
        auto last = juce::File (props().getValue ("lastSong"));
        if (last.existsAsFile() && recent::load (props()).isEmpty()) // songs made before the Hub existed
            if (auto e = songs::open (engine, last, te::Edit::forExamining)) recent::remember (props(), *e, last, last.getLastModificationTime());
        const bool openLast = props().getValue ("startWith", "hub") == "last" && last.existsAsFile();
        if (! openLast)
        {
            showHub (true);
            hub->showTab (hub->recentCount() > 0 ? 0 : 1);
        }
        // the window shows first; then the audio interface opens, then the last song (if that's the setting)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), openLast, last]
        {
            if (safe == nullptr) return;
            const auto t = juce::Time::getMillisecondCounterHiRes();
            safe->engine.getDeviceManager().initialise();
            basementLog ("startup: audio interface opened in " + juce::String (juce::Time::getMillisecondCounterHiRes() - t, 0) + " ms");
            safe->setupInputs();
            if (openLast) safe->loadSong (last);
            if (safe->hub) safe->hub->repaint();
        });
    }

    ~MainComponent() override
    {
        stopTimer();
        BasementHooks::findPlugins = nullptr;
        BasementHooks::showPluginManager = nullptr;
        BasementHooks::pluginScanStatus = nullptr;
        BasementHooks::commands = nullptr;
        BasementHooks::fillClipMenu = nullptr;
        setApplicationCommandManagerToWatch (nullptr);
        engine.getDeviceManager().removeChangeListener (this);
        closeSong (true);
        hub = nullptr;
        props().setValue ("running", false);
        props().saveIfNeeded();
        engine.getTemporaryFileManager().getTempDirectory().deleteRecursively();
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::base);
        g.setColour (theme::surface);
        g.fillRect (getLocalBounds().removeFromTop (menuH));
        g.fillRect (getLocalBounds().removeFromBottom (statusH));
        g.setColour (theme::strong);
        g.fillRect (getLocalBounds().removeFromBottom (statusH).removeFromTop (1));
        if (hubShown) return;

        auto bar = getLocalBounds().withTrimmedTop (menuH).removeFromTop (toolbarH);
        g.setColour (theme::surface);
        g.fillRect (bar);
        g.setColour (theme::strong);
        g.fillRect (bar.removeFromBottom (1));
        g.fillRect (getLocalBounds().withTrimmedTop (menuH).removeFromTop (1));

        // wordmark
        auto mark = getLocalBounds().withTrimmedTop (menuH).removeFromTop (toolbarH).reduced (14, 0).removeFromLeft (172);
        drawEncoreIcon (g, mark.removeFromLeft (36).toFloat().withSizeKeepingCentre (36.0f, 36.0f));
        mark.removeFromLeft (10);
        g.setColour (theme::text2);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("ENCORE:", mark.withTrimmedBottom (mark.getHeight() / 2 - 2), juce::Justification::bottomLeft);
        g.setColour (theme::paper);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("BASEMENT", mark.withTrimmedTop (mark.getHeight() / 2 - 2), juce::Justification::topLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        menuBar.setBounds (r.removeFromTop (menuH));
        {
            auto bar = r.removeFromBottom (statusH).reduced (12, 0);
            deviceLabel.setBounds (bar.removeFromRight (juce::jmin (520, bar.getWidth() / 2)));
            statusLabel.setBounds (bar);
            statusLabel.setVisible (! hubShown && edit != nullptr);
        }
        if (hubShown)
        {
            hub->setBounds (r);
            for (auto* c : { (juce::Component*) &importButton, (juce::Component*) &addTrackButton }) c->setVisible (false);
            return;
        }

        auto t = r.removeFromTop (toolbarH).reduced (12, 10);
        t.removeFromLeft (180);
        auto place = [&t] (juce::Component& c, int w, int gapAfter = 6) { c.setBounds (t.removeFromLeft (w)); t.removeFromLeft (gapAfter); };
        place (homeButton, 40); place (playButton, 72); place (recordButton, 80); place (loopButton, 60, 14);
        place (timeLabel, 100); place (barLabel, 70, 6);
        auto tempo = t.removeFromLeft (64);
        tempoCaption.setBounds (tempo.removeFromTop (12));
        tempoLabel.setBounds (tempo);
        t.removeFromLeft (8);
        place (clickButton, 60, 8);
        place (snapBox, 112);
        // narrow windows drop the extras first (they're all in the menus too)
        exportButton.setBounds (t.removeFromRight (110)); t.removeFromRight (10);
        masterMeter.setBounds (t.removeFromRight (100).withSizeKeepingCentre (100, 14)); t.removeFromRight (10);
        const bool roomy = t.getWidth() >= 98 + 116 + 16;
        importButton.setVisible (roomy && ! hubShown);
        addTrackButton.setVisible (roomy && ! hubShown);
        if (roomy)
        {
            importButton.setBounds (t.removeFromRight (116)); t.removeFromRight (6);
            addTrackButton.setBounds (t.removeFromRight (98)); t.removeFromRight (10);
        }

        if (editComponent) editComponent->setBounds (r);
        countIn.setBounds (r);
    }

    //==============================================================================
    // Keys go through the same commands as the menus. Some keyboards and tools
    // send Ctrl+letter as a control code (Ctrl+T arrives as 20), so letters are
    // normalised first.
    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        auto mods = k.getModifiers();
        int code = k.getKeyCode();
        if (code >= 1 && code <= 26) { code = 'a' + code - 1; mods = mods.withFlags (juce::ModifierKeys::commandModifier); }
        if (code >= 'A' && code <= 'Z') code += 'a' - 'A';
        if (auto id = commandManager.getKeyMappings()->findCommandForKeyPress (juce::KeyPress (code, mods, 0)))
            return commandManager.invokeDirectly (id, false);
        return false;
    }

    // Drop audio files anywhere: each becomes a track
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        if (! edit || hubShown) return false;
        for (auto& f : files) if (isAudioFile (juce::File (f))) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        juce::Array<juce::File> list;
        for (auto& f : files) if (isAudioFile (juce::File (f))) list.add (juce::File (f));
        importFiles (list);
    }

    void updateTitle()
    {
        if (onTitleChange) onTitleChange (edit ? "Encore: Basement - " + songTitle() : juce::String ("Encore: Basement"));
    }

    //==============================================================================
    // Menus
    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "Project", "Audio", "MIDI", "Score", "Media", "Transport", "Studio", "Window", "Help" };
    }

    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu m;
        auto add = [&] (int id) { addCommand (m, id); };
        auto later = [&m] (std::initializer_list<const char*> items)
        {
            m.addSectionHeader ("Coming in a later version");
            for (auto* s : items) m.addItem (comingSoonId, s, false);
        };
        switch (index)
        {
            case 0: // File
            {
                add (cmdHub); m.addSeparator();
                add (cmdNew); add (cmdOpen);
                juce::PopupMenu recentMenu;
                recentFiles.clear();
                for (auto& e : recent::load (props()))
                {
                    if (recentFiles.size() >= 12) break;
                    recentFiles.add (e.file);
                    recentMenu.addItem (recentBaseId + recentFiles.size() - 1, e.title + (e.artist.isNotEmpty() ? "  (" + e.artist + ")" : juce::String()), e.file.existsAsFile());
                }
                m.addSubMenu ("Open Recent", recentMenu, ! recentFiles.isEmpty());
                add (cmdClose); m.addSeparator();
                add (cmdSave); add (cmdSaveAs); add (cmdSaveVersion); add (cmdSaveTemplate); add (cmdRevert); m.addSeparator();
                juce::PopupMenu imp; addCommand (imp, cmdImportAudio); imp.addItem (comingSoonId, "MIDI File... (coming soon)", false);
                m.addSubMenu ("Import", imp);
                juce::PopupMenu exp; addCommand (exp, cmdExportMix); addCommand (exp, cmdExportZip);
                exp.addItem (comingSoonId, "Send to Encore... (coming soon)", false);
                m.addSubMenu ("Export", exp);
                m.addSeparator(); add (cmdPreferences); m.addSeparator(); add (cmdQuit);
                break;
            }
            case 1: // Edit
                add (cmdUndo); add (cmdRedo); m.addSeparator();
                add (cmdCut); add (cmdCopy); add (cmdPaste); add (cmdDuplicate); add (cmdDelete); m.addSeparator();
                add (cmdSplit); add (cmdMuteClip); add (cmdRenameClip); m.addSeparator();
                add (cmdNudgeLeft); add (cmdNudgeRight);
                {
                    juce::PopupMenu snapMenu;
                    for (auto id : { cmdSnapOff, cmdSnapBar, cmdSnapBeat, cmdSnapEighth, cmdSnapSixteenth }) addCommand (snapMenu, id);
                    snapMenu.addSeparator();
                    snapMenu.addItem (comingSoonId, "Hold Shift while dragging to ignore the grid", false);
                    m.addSubMenu ("Snap to Grid", snapMenu);
                }
                m.addSeparator();
                add (cmdSelectAll); add (cmdSelectNone);
                break;
            case 2: // Project
                add (cmdAddTrack); add (cmdDuplicateTrack); add (cmdRenameTrack); add (cmdRemoveTrack); m.addSeparator();
                add (cmdSongSetup);
                later ({ "Add Instrument Track", "Add Group Track", "Add FX Track", "Markers", "Arranger Track" });
                break;
            case 3: // Audio
                add (cmdNormalize); add (cmdGainUp); add (cmdGainDown); add (cmdGainReset); m.addSeparator();
                add (cmdFadeIn); add (cmdFadeOut); add (cmdRemoveFades); m.addSeparator();
                add (cmdReverse);
                later ({ "Time Stretch", "Pitch Shift", "Detect Tempo", "Bounce Selection" });
                break;
            case 4: // MIDI
                later ({ "Open Key Editor", "Open Drum Editor", "Quantize", "Transpose", "Velocity", "Import MIDI File" });
                break;
            case 5: // Score
                later ({ "Open Score Editor", "Chord Track", "Lyrics", "Print / Export Score" });
                break;
            case 6: // Media
                add (cmdImportAudio); m.addSeparator();
                add (cmdShowSongFolder); add (cmdShowRecordings); add (cmdShowAudio); m.addSeparator();
                add (cmdChangeProjectsFolder);
                break;
            case 7: // Transport
            {
                add (cmdPlay); add (cmdRecord); m.addSeparator();
                add (cmdToStart); add (cmdToEnd); add (cmdBarBack); add (cmdBarForward); m.addSeparator();
                add (cmdLoop); add (cmdLoopToSelection); add (cmdPunch); m.addSeparator();
                add (cmdClick); add (cmdMetronome);
                juce::PopupMenu count;
                for (auto id : { cmdCountNone, cmdCount1Beat, cmdCount2Beats, cmdCount1Bar, cmdCount2Bars }) addCommand (count, id);
                m.addSubMenu ("Count-in", count);
                break;
            }
            case 8: // Studio
                add (cmdAudioSetup); add (cmdPlugins); add (cmdFindPlugins); add (cmdReassignInputs); m.addSeparator();
                m.addItem (comingSoonId, "Sign in to Encore (coming soon)", false);
                m.addSeparator(); add (cmdPreferences);
                break;
            case 9: // Window
                add (cmdHub); m.addSeparator();
                add (cmdEffects); m.addSeparator();
                add (cmdZoomIn); add (cmdZoomOut); add (cmdZoomFit); m.addSeparator();
                {
                    juce::PopupMenu heights;
                    for (auto id : { cmdHeightSmall, cmdHeightMedium, cmdHeightLarge, cmdHeightHuge }) addCommand (heights, id);
                    heights.addSeparator();
                    heights.addItem (comingSoonId, "Or drag a track's bottom edge (Alt: all tracks)", false);
                    m.addSubMenu ("Track Height", heights, edit != nullptr && ! hubShown);
                }
                later ({ "Mixer", "Clip Editor" });
                break;
            case 10: // Help
                add (cmdShortcuts); add (cmdGuide); m.addSeparator();
                add (cmdReport); add (cmdUpdates); add (cmdLogFolder); m.addSeparator();
                add (cmdAbout);
                break;
            default: break;
        }
        return m;
    }

    /** A command in a menu, with its shortcut written the way DAWs write them (R, Ctrl+Shift+H, Space) */
    void addCommand (juce::PopupMenu& m, int id)
    {
        juce::ApplicationCommandInfo info (id);
        getCommandInfo (id, info);
        juce::PopupMenu::Item item (info.shortName);
        item.itemID = id;
        item.commandManager = &commandManager;
        item.isEnabled = (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
        item.isTicked = (info.flags & juce::ApplicationCommandInfo::isTicked) != 0;
        juce::StringArray keys;
        for (auto& k : info.defaultKeypresses) keys.add (keyText (k));
        item.shortcutKeyDescription = keys.joinIntoString (", ");
        m.addItem (item);
    }

    static juce::String keyText (const juce::KeyPress& k)
    {
        juce::StringArray parts;
        auto mods = k.getModifiers();
        if (mods.isCommandDown()) parts.add ("Ctrl");
        if (mods.isAltDown()) parts.add ("Alt");
        if (mods.isShiftDown()) parts.add ("Shift");
        using KP = juce::KeyPress;
        auto c = k.getKeyCode();
        juce::String key;
        if (c == KP::spaceKey) key = "Space";
        else if (c == KP::homeKey) key = "Home";
        else if (c == KP::endKey) key = "End";
        else if (c == KP::leftKey) key = "Left";
        else if (c == KP::rightKey) key = "Right";
        else if (c == KP::deleteKey) key = "Del";
        else if (c == KP::backspaceKey) key = "Backspace";
        else if (c == KP::F2Key) key = "F2";
        else key = juce::String::charToString ((juce::juce_wchar) c).toUpperCase();
        parts.add (key);
        return parts.joinIntoString ("+");
    }

    void menuItemSelected (int id, int) override
    {
        if (id >= recentBaseId && id < recentBaseId + recentFiles.size())
            if (recentFiles[id - recentBaseId].existsAsFile()) loadSong (recentFiles[id - recentBaseId]);
    }

    //==============================================================================
    // Commands
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }

    void getAllCommands (juce::Array<juce::CommandID>& c) override
    {
        for (int id = cmdHub; id <= cmdAbout; ++id) c.add (id);
    }

    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override
    {
        using KP = juce::KeyPress;
        const int cmd = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier, alt = juce::ModifierKeys::altModifier, none = 0;
        const bool song = edit != nullptr, songShown = song && ! hubShown;
        auto clips = selectedClips();
        const bool anyClip = songShown && ! clips.isEmpty();
        auto set = [&] (const char* name, const char* cat, bool active = true)
        {
            info.setInfo (name, {}, cat, juce::ApplicationCommandInfo::dontTriggerVisualFeedback); // keys don't flash the menu bar
            info.setActive (active);
        };
        auto key = [&] (int k, int mods) { info.addDefaultKeypress (k, mods); };

        switch (id)
        {
            case cmdHub:            set ("Hub", "File"); key ('h', cmd | shift); info.setTicked (hubShown); break;
            case cmdNew:            set ("New Song...", "File"); key ('n', cmd); break;
            case cmdOpen:           set ("Open...", "File"); key ('o', cmd); break;
            case cmdClose:          set ("Close", "File", song); key ('w', cmd); break;
            case cmdSave:           set ("Save", "File", song); key ('s', cmd); break;
            case cmdSaveAs:         set ("Save As...", "File", song); key ('s', cmd | shift); break;
            case cmdSaveVersion:    set ("Save New Version", "File", song); key ('s', cmd | alt); break;
            case cmdSaveTemplate:   set ("Save as Template...", "File", song); break;
            case cmdRevert:         set ("Revert", "File", song); break;
            case cmdImportAudio:    set ("Audio File...", "File", song); key ('i', cmd); break;
            case cmdExportMix:      set ("Audio Mixdown...", "File", song && ! currentExport); key ('e', cmd); break;
            case cmdExportZip:      set ("Song Folder as Zip...", "File", song); break;
            case cmdPreferences:    set ("Preferences...", "File"); key (',', cmd); break;
            case cmdQuit:           set ("Quit", "File"); key ('q', cmd); break;

            case cmdUndo:           set ("Undo", "Edit", songShown && edit->getUndoManager().canUndo()); key ('z', cmd); break;
            case cmdRedo:           set ("Redo", "Edit", songShown && edit->getUndoManager().canRedo()); key ('y', cmd); key ('z', cmd | shift); break;
            case cmdCut:            set ("Cut", "Edit", anyClip); key ('x', cmd); break;
            case cmdCopy:           set ("Copy", "Edit", anyClip); key ('c', cmd); break;
            case cmdPaste:          set ("Paste at Playhead", "Edit", songShown && ! clipboard.isEmpty()); key ('v', cmd); break;
            case cmdDuplicate:      set ("Duplicate", "Edit", anyClip); key ('d', cmd); break;
            case cmdDelete:         set ("Delete", "Edit", songShown && selectionManager.getNumObjectsSelected() > 0); key (KP::deleteKey, none); key (KP::backspaceKey, none); break;
            case cmdSplit:          set ("Split at Playhead", "Edit", songShown); key ('x', alt); break;
            case cmdMuteClip:       set ("Mute / Unmute Clip", "Edit", anyClip); key ('m', shift); break;
            case cmdSelectAll:      set ("Select All", "Edit", songShown); key ('a', cmd); break;
            case cmdSelectNone:     set ("Select None", "Edit", songShown); key ('a', cmd | shift); break;
            case cmdNudgeLeft:      set ("Nudge Left", "Edit", anyClip); key (KP::leftKey, cmd); break;
            case cmdNudgeRight:     set ("Nudge Right", "Edit", anyClip); key (KP::rightKey, cmd); break;
            case cmdRenameClip:     set ("Rename Clip...", "Edit", anyClip); break;
            case cmdSnapOff:        set ("Off", "Edit", songShown); info.setTicked (snapIs (0)); break;
            case cmdSnapBar:        set ("Bar", "Edit", songShown); info.setTicked (snapIs (1)); break;
            case cmdSnapBeat:       set ("Beat", "Edit", songShown); info.setTicked (snapIs (2)); break;
            case cmdSnapEighth:     set ("1/8", "Edit", songShown); info.setTicked (snapIs (3)); break;
            case cmdSnapSixteenth:  set ("1/16", "Edit", songShown); info.setTicked (snapIs (4)); break;

            case cmdAddTrack:       set ("Add Audio Track", "Project", songShown); key ('t', cmd); break;
            case cmdDuplicateTrack: set ("Duplicate Track", "Project", songShown && selectedTrack() != nullptr); key ('d', cmd | shift); break;
            case cmdRenameTrack:    set ("Rename Track...", "Project", songShown && selectedTrack() != nullptr); key (KP::F2Key, none); break;
            case cmdRemoveTrack:    set ("Remove Selected Track", "Project", songShown && selectedTrack() != nullptr); break;
            case cmdSongSetup:      set ("Song Setup...", "Project", song); key ('s', shift); break;

            case cmdNormalize:      set ("Normalize Clip (to -0.3 dB)", "Audio", anyClip); break;
            case cmdGainUp:         set ("Clip Gain +1 dB", "Audio", anyClip); key ('=', alt); break;
            case cmdGainDown:       set ("Clip Gain -1 dB", "Audio", anyClip); key ('-', alt); break;
            case cmdGainReset:      set ("Reset Clip Gain", "Audio", anyClip); break;
            case cmdFadeIn:         set ("Fade In to Playhead", "Audio", anyClip); key ('f', alt); break;
            case cmdFadeOut:        set ("Fade Out from Playhead", "Audio", anyClip); key ('g', alt); break;
            case cmdRemoveFades:    set ("Remove Fades", "Audio", anyClip); break;
            case cmdReverse:        set ("Reverse", "Audio", anyClip); break;

            case cmdShowSongFolder: set ("Show Song Folder", "Media", song); break;
            case cmdShowRecordings: set ("Show Recordings Folder", "Media", song); break;
            case cmdShowAudio:      set ("Show Imported Audio Folder", "Media", song); break;
            case cmdChangeProjectsFolder: set ("Change Songs Folder...", "Media"); break;

            case cmdPlay:           set ("Play / Stop", "Transport", songShown); key (KP::spaceKey, none); break;
            case cmdRecord:         set ("Record", "Transport", songShown); key ('r', none); key ('*', none); break;
            case cmdToStart:        set ("Go to Start", "Transport", songShown); key (KP::homeKey, none); break;
            case cmdToEnd:          set ("Go to End", "Transport", songShown); key (KP::endKey, none); break;
            case cmdBarBack:        set ("Back One Bar", "Transport", songShown); key (KP::leftKey, none); break;
            case cmdBarForward:     set ("Forward One Bar", "Transport", songShown); key (KP::rightKey, none); break;
            case cmdLoop:           set ("Loop", "Transport", songShown); key ('l', none); info.setTicked (song && edit->getTransport().looping.get()); break;
            case cmdLoopToSelection:set ("Set Loop to Selection", "Transport", anyClip); key ('p', none); break;
            case cmdClick:          set ("Metronome Click", "Transport", songShown); key ('c', none); info.setTicked (song && edit->clickTrackEnabled.get()); break;
            case cmdCountNone:      set ("No Count-in", "Transport", song); info.setTicked (song && edit->getCountInMode() == te::Edit::CountIn::none); break;
            case cmdCount1Beat:     set ("1 Beat", "Transport", song); info.setTicked (song && edit->getCountInMode() == te::Edit::CountIn::oneBeat); break;
            case cmdCount2Beats:    set ("2 Beats", "Transport", song); info.setTicked (song && edit->getCountInMode() == te::Edit::CountIn::twoBeat); break;
            case cmdCount1Bar:      set ("1 Bar", "Transport", song); info.setTicked (song && edit->getCountInMode() == te::Edit::CountIn::oneBar); break;
            case cmdCount2Bars:     set ("2 Bars", "Transport", song); info.setTicked (song && edit->getCountInMode() == te::Edit::CountIn::twoBar); break;
            case cmdMetronome:      set ("Metronome Settings...", "Transport", song); break;
            case cmdPunch:          set ("Punch In/Out (records only inside the loop range)", "Transport", songShown); key ('i', none); info.setTicked (song && edit->recordingPunchInOut.get()); break;

            case cmdAudioSetup:     set ("Audio Setup...", "Studio"); break;
            case cmdPlugins:        set ("Plugin Manager...", "Studio"); break;
            case cmdFindPlugins:    set ("Find New Plugins", "Studio", ! pluginFinder.isRunning()); break;
            case cmdReassignInputs: set ("Give Tracks Their Inputs", "Studio", songShown); break;

            case cmdEffects:        set ("Effects Column", "Window", songShown); info.setTicked (editComponent && editComponent->getEditViewState().showFooters.get()); break;
            case cmdZoomIn:         set ("Zoom In", "Window", songShown); key ('=', cmd); break;
            case cmdZoomOut:        set ("Zoom Out", "Window", songShown); key ('-', cmd); break;
            case cmdZoomFit:        set ("Zoom to Song", "Window", songShown); key ('f', shift); break;
            case cmdHeightSmall:    set ("Small", "Window", songShown); break;
            case cmdHeightMedium:   set ("Medium", "Window", songShown); break;
            case cmdHeightLarge:    set ("Large", "Window", songShown); break;
            case cmdHeightHuge:     set ("Extra Large", "Window", songShown); break;

            case cmdShortcuts:      set ("Keyboard Shortcuts...", "Help"); break;
            case cmdGuide:          set ("User Guide", "Help"); break;
            case cmdReport:         set ("Report a Problem...", "Help"); break;
            case cmdUpdates:        set ("Check for Updates...", "Help"); break;
            case cmdLogFolder:      set ("Open Log Folder", "Help"); break;
            case cmdAbout:          set ("About Encore: Basement", "Help"); break;
            default: break;
        }
    }

    bool perform (const InvocationInfo& i) override
    {
        auto url = [] (const char* u) { juce::URL (u).launchInDefaultBrowser(); };
        switch (i.commandID)
        {
            case cmdHub:            showHub (! hubShown || edit == nullptr); return true;
            case cmdNew:            showHub (true); hub->showTab (1); return true;
            case cmdOpen:           openSong(); return true;
            case cmdClose:          closeSong (true); showHub (true); return true;
            case cmdSave:           save(); status ("Saved"); return true;
            case cmdSaveAs:         saveAs(); return true;
            case cmdSaveVersion:    saveNewVersion(); return true;
            case cmdSaveTemplate:   saveTemplate(); return true;
            case cmdRevert:         revert(); return true;
            case cmdImportAudio:    browseForImport(); return true;
            case cmdExportMix:      exportMix(); return true;
            case cmdExportZip:      exportZip(); return true;
            case cmdPreferences:    showDialog ("Preferences", new PreferencesComponent (props()), this); return true;
            case cmdQuit:           juce::JUCEApplication::getInstance()->systemRequestedQuit(); return true;

            case cmdUndo:           edit->getUndoManager().undo(); return true;
            case cmdRedo:           edit->getUndoManager().redo(); return true;
            case cmdCut:            copyClips(); removeClips (selectedClips(), "Cut"); return true;
            case cmdCopy:           copyClips(); return true;
            case cmdPaste:          pasteClips (edit->getTransport().getPosition(), selectedTrack()); return true;
            case cmdDuplicate:      duplicateClips(); return true;
            case cmdDelete:         deleteSelection(); return true;
            case cmdSplit:          splitAtPlayhead(); return true;
            case cmdMuteClip:       forClips ("Mute clip", [] (te::AudioClipBase& c) { c.setMuted (! c.isMuted()); }); return true;
            case cmdSelectAll:      selectAll(); return true;
            case cmdSelectNone:     selectionManager.deselectAll(); return true;
            case cmdNudgeLeft:      nudge (-1); return true;
            case cmdNudgeRight:     nudge (1); return true;
            case cmdRenameClip:     renameClip(); return true;
            case cmdSnapOff:        setSnap (0); return true;
            case cmdSnapBar:        setSnap (1); return true;
            case cmdSnapBeat:       setSnap (2); return true;
            case cmdSnapEighth:     setSnap (3); return true;
            case cmdSnapSixteenth:  setSnap (4); return true;

            case cmdAddTrack:       addTrack(); return true;
            case cmdDuplicateTrack: duplicateTrack(); return true;
            case cmdRenameTrack:    renameTrack(); return true;
            case cmdRemoveTrack:    if (auto t = selectedTrack()) { edit->deleteTrack (t); selectionManager.deselectAll(); } return true;
            case cmdSongSetup:      songSetup(); return true;

            case cmdNormalize:      normalizeClips(); return true;
            case cmdGainUp:         forClips ("Clip gain", [] (te::AudioClipBase& c) { c.setGainDB (juce::jmin (24.0f, c.getGainDB() + 1.0f)); }); return true;
            case cmdGainDown:       forClips ("Clip gain", [] (te::AudioClipBase& c) { c.setGainDB (juce::jmax (-60.0f, c.getGainDB() - 1.0f)); }); return true;
            case cmdGainReset:      forClips ("Clip gain", [] (te::AudioClipBase& c) { c.setGainDB (0.0f); }); return true;
            case cmdFadeIn:         fadeToPlayhead (true); return true;
            case cmdFadeOut:        fadeToPlayhead (false); return true;
            case cmdRemoveFades:    forClips ("Remove fades", [] (te::AudioClipBase& c) { c.setFadeIn ({}); c.setFadeOut ({}); }); return true;
            case cmdReverse:        forClips ("Reverse", [] (te::AudioClipBase& c) { c.setIsReversed (! c.getIsReversed()); }); return true;

            case cmdShowSongFolder: songFile.revealToUser(); return true;
            case cmdShowRecordings: revealFolder ("Recordings"); return true;
            case cmdShowAudio:      revealFolder ("Audio"); return true;
            case cmdChangeProjectsFolder: showDialog ("Preferences", new PreferencesComponent (props()), this); return true;

            case cmdPlay:           togglePlay(); return true;
            case cmdRecord:         toggleRecord(); return true;
            case cmdToStart:        edit->getTransport().setPosition (0s); return true;
            case cmdToEnd:          edit->getTransport().setPosition (te::toPosition (edit->getLength())); return true;
            case cmdBarBack:        moveBars (-1); return true;
            case cmdBarForward:     moveBars (1); return true;
            case cmdLoop:           toggleLoop(); return true;
            case cmdLoopToSelection:loopToSelection(); edit->getTransport().looping = true; return true;
            case cmdClick:          edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); return true;
            case cmdCountNone:      edit->setCountInMode (te::Edit::CountIn::none); return true;
            case cmdCount1Beat:     edit->setCountInMode (te::Edit::CountIn::oneBeat); return true;
            case cmdCount2Beats:    edit->setCountInMode (te::Edit::CountIn::twoBeat); return true;
            case cmdCount1Bar:      edit->setCountInMode (te::Edit::CountIn::oneBar); return true;
            case cmdCount2Bars:     edit->setCountInMode (te::Edit::CountIn::twoBar); return true;
            case cmdMetronome:      showDialog ("Metronome", new MetronomeComponent (*edit), this); return true;
            case cmdPunch:          togglePunch(); return true;

            case cmdAudioSetup:     EngineHelpers::showAudioDeviceSettings (engine); return true;
            case cmdPlugins:        showPluginList(); return true;
            case cmdFindPlugins:    startPluginSearch(); return true;
            case cmdReassignInputs: assignInputs (true); return true;

            case cmdEffects:        { auto& v = editComponent->getEditViewState(); v.showFooters = ! v.showFooters.get(); editComponent->setBounds (editComponent->getBounds().withWidth (editComponent->getWidth() - 1)); resized(); return true; }
            case cmdZoomIn:         zoom (0.5); return true;
            case cmdZoomOut:        zoom (2.0); return true;
            case cmdZoomFit:        zoomToSong(); return true;
            case cmdHeightSmall:    setAllTrackHeights (48); return true;
            case cmdHeightMedium:   setAllTrackHeights (EditComponent::trackHeight); return true;
            case cmdHeightLarge:    setAllTrackHeights (150); return true;
            case cmdHeightHuge:     setAllTrackHeights (260); return true;

            case cmdShortcuts:      showShortcuts(); return true;
            case cmdGuide:          url ("https://github.com/jimmyrlondo/encore-basement#readme"); return true;
            case cmdReport:         url ("https://github.com/jimmyrlondo/encore-basement/issues/new"); return true;
            case cmdUpdates:        url ("https://github.com/jimmyrlondo/encore-basement/releases"); return true;
            case cmdLogFolder:      juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (basementProfile()).startAsProcess(); return true;
            case cmdAbout:          showAbout(); return true;
            default: return false;
        }
    }

private:
    //==============================================================================
    static constexpr int menuH = 26, toolbarH = 56, recentBaseId = 0x5000, comingSoonId = 0x5fff;
    double constructionStarted = juce::Time::getMillisecondCounterHiRes();
    BasementBehaviour* behaviour = new BasementBehaviour (true); // owned by the engine
    te::Engine engine { basementProfile(), std::make_unique<BasementUIBehaviour>(), std::unique_ptr<te::EngineBehaviour> (behaviour) };
    te::SelectionManager selectionManager { engine };
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<EditComponent> editComponent;
    juce::File songFile;
    juce::TooltipWindow tooltips { this, 600 };
    juce::ApplicationCommandManager commandManager;
    PluginFinder pluginFinder { engine };
    juce::MenuBarComponent menuBar { this };
    std::unique_ptr<HubComponent> hub;
    bool hubShown = false;
    juce::Array<juce::File> recentFiles;

    juce::TextButton homeButton { "|<" }, playButton { "Play" }, recordButton { "Record" }, loopButton { "Loop" }, clickButton { "Click" },
                     addTrackButton { "Add track" }, importButton { "Import audio" }, exportButton { "Export mix" };
    juce::Label timeLabel { {}, "0:00.0" }, barLabel { {}, "Bar 1.1" }, tempoLabel { {}, "120" }, tempoCaption { {}, "BPM" }, statusLabel;
    LevelMeter masterMeter { LevelMeter::Orientation::horizontal };
    juce::ComboBox snapBox;
    juce::Label deviceLabel;
    CountInOverlay countIn;
    juce::uint32 lastAutosave = 0;
    bool crashedLastSession = false;
    static constexpr int statusH = 24;

    juce::PropertiesFile& props() { return engine.getPropertyStorage().getPropertiesFile(); }
    static bool isAudioFile (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf;w64;bwf"); }
    void status (const juce::String& s) { statusLabel.setText (s, juce::dontSendNotification); }
    void invoke (int id) { commandManager.invokeDirectly (id, false); }
    juce::String songTitle() { return edit ? SongInfo::readFrom (*edit, songFile).title : juce::String(); }

    void showHub (bool show)
    {
        hubShown = show;
        hub->setVisible (show);
        if (show) hub->refresh (edit ? songTitle() : juce::String());
        const bool tools = ! show && edit != nullptr;
        for (auto* c : std::initializer_list<juce::Component*> { &homeButton, &playButton, &recordButton, &loopButton, &clickButton,
                                                                 &exportButton, &timeLabel, &barLabel, &tempoLabel, &tempoCaption, &masterMeter, &snapBox })
            c->setVisible (tools);
        if (editComponent) editComponent->setVisible (! show);
        commandManager.commandStatusChanged();
        resized();
        repaint();
        updateTitle();
    }

    //==============================================================================
    // Inputs. Every input on the interface is its own mono input (Input 1,
    // Input 2...), so a mic on 1 and a guitar on 2 go to separate tracks.
    void setupInputs()
    {
        auto& dm = engine.getDeviceManager();
        // split stereo pairs into separate mono inputs (this rebuilds the device list,
        // which calls back here through changeListenerCallback until nothing is left to split)
        juce::Array<te::WaveInputDevice*> pairs;
        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto wip = dm.getWaveInDevice (i))
                if (! wip->isTrackDevice() && wip->isStereoPair())
                    pairs.add (wip);
        for (auto* wip : pairs)
        {
            basementLog ("splitting " + wip->getName() + " into mono inputs");
            dm.setDeviceNumChannels (*wip, 1);
        }
        juce::StringArray names;
        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto wip = dm.getWaveInDevice (i)) names.add (wip->getName() + (wip->isStereoPair() ? " (stereo)" : ""));
        basementLog ("inputs: " + names.joinIntoString (", "));

        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto wip = dm.getWaveInDevice (i))
            {
                wip->setEnabled (true);
                if (edit) wip->setBitDepth (SongInfo::readFrom (*edit, songFile).bitDepth);
            }
    }

    /** Windows' shared audio path adds latency and its own input level in the
        way. When the interface you're using has its own ASIO driver (most do:
        Volt, Scarlett, Audient, MOTU...), switch to it, once. Generic
        wrappers (ASIO4ALL and friends) are never picked automatically, and if
        the driver won't open (another app using it) nothing changes. */
    void preferInterfaceAsio()
    {
        if (props().getBoolValue ("asioChecked", false)) return;
        props().setValue ("asioChecked", true);
        auto& dm = engine.getDeviceManager().deviceManager;
        if (dm.getCurrentAudioDeviceType() == "ASIO") return;

        juce::AudioIODeviceType* asio = nullptr;
        juce::StringArray typeNames;
        for (auto* t : dm.getAvailableDeviceTypes()) { typeNames.add (t->getTypeName()); if (t->getTypeName() == "ASIO") asio = t; }
        if (asio == nullptr) { basementLog ("ASIO check: no ASIO support (types: " + typeNames.joinIntoString (", ") + ")"); return; }
        asio->scanForDevices();

        auto setup = dm.getAudioDeviceSetup();
        const juce::StringArray generic { "input", "output", "line", "speakers", "speaker", "headphones", "microphone", "mic", "audio", "device",
                                          "usb", "high", "definition", "realtek", "digital", "stereo", "default", "communications", "array" };
        juce::StringArray words;
        for (auto& n : { setup.inputDeviceName, setup.outputDeviceName })
            for (auto& w : juce::StringArray::fromTokens (n.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 "), " ", {}))
                if (w.length() >= 3 && ! generic.contains (w) && ! w.containsOnly ("0123456789")) words.addIfNotAlreadyThere (w);

        basementLog ("ASIO check: using " + dm.getCurrentAudioDeviceType() + " (" + setup.inputDeviceName + " / " + setup.outputDeviceName + "), looking for "
                     + words.joinIntoString ("/") + " among ASIO drivers: " + asio->getDeviceNames().joinIntoString (", "));
        for (auto& name : asio->getDeviceNames())
        {
            auto lower = name.toLowerCase();
            if (lower.contains ("asio4all") || lower.contains ("generic") || lower.contains ("magix") || lower.contains ("built-in") || lower.contains ("flexasio"))
                continue;
            bool match = false;
            for (auto& w : words) match = match || lower.contains (w);
            if (! match) continue;

            auto before = setup;
            auto beforeType = dm.getCurrentAudioDeviceType();
            dm.setCurrentAudioDeviceType ("ASIO", true);
            auto s = dm.getAudioDeviceSetup();
            s.inputDeviceName = s.outputDeviceName = name;
            s.useDefaultInputChannels = s.useDefaultOutputChannels = true;
            auto err = dm.setAudioDeviceSetup (s, true);
            if (err.isEmpty() && dm.getCurrentAudioDevice() != nullptr)
            {
                basementLog ("switched to the interface's ASIO driver: " + name);
                return;
            }
            basementLog ("ASIO driver " + name + " didn't open (" + err + "), staying on " + beforeType);
            props().setValue ("asioChecked", false); // try again next time (another app may have had it)
            dm.setCurrentAudioDeviceType (beforeType, true);
            dm.setAudioDeviceSetup (before, true);
            return;
        }
    }

    /** Each track gets the input it was planned with (saved on the track), or
        the next free one; tracks planned with no input are left alone. Inputs
        appear a moment after startup or a device change, so this keeps trying
        for a few seconds. */
    juce::uint32 assignUntil = 0;
    void assignInputs (bool force = false)
    {
        if (! edit) return;
        bool all = true;
        for (auto t : te::getAudioTracks (*edit))
        {
            if (EngineHelpers::trackHasInput (*t)) continue;
            auto want = t->state.getProperty ("basementInput").toString();
            if (want == "none" && ! force) continue;
            if (! assignInput (*t, want == "none" ? juce::String() : want))
                all = false;
        }
        if (all) assignUntil = 0;
    }

    bool assignInput (te::AudioTrack& t, const juce::String& wanted)
    {
        te::InputDeviceInstance* pick = nullptr;
        for (auto instance : edit->getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice && instance->getTargets().isEmpty())
            {
                if (wanted.isNotEmpty() && instance->getInputDevice().getName() == wanted) { pick = instance; break; }
                if (pick == nullptr && wanted.isEmpty()) pick = instance;
            }
        bool wantedHere = false, anyHere = false;
        for (auto instance : edit->getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
            {
                anyHere = true;
                wantedHere = wantedHere || instance->getInputDevice().getName() == wanted;
            }
        if (pick == nullptr && wanted.isNotEmpty() && ((anyHere && ! wantedHere) || juce::Time::getMillisecondCounter() > assignUntil))
            for (auto instance : edit->getAllInputDevices()) // that input isn't here (another interface): use the next free one
                if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice && instance->getTargets().isEmpty()) { pick = instance; break; }
        if (pick == nullptr) return false;
        auto res = pick->setTarget (t.itemID, true, &edit->getUndoManager(), 0);
        basementLog (t.getName() + " <- " + pick->getInputDevice().getName() + (res ? " ok" : " FAILED: " + juce::String (res.error())));
        if (res) t.state.setProperty ("basementInput", pick->getInputDevice().getName(), nullptr);
        return (bool) res;
    }

    //==============================================================================
    // Songs
    juce::File projectsFolder()
    {
        auto f = juce::File (props().getValue ("projectsFolder"));
        return f != juce::File() ? f : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Encore Basement");
    }

    void createSong (SongPlan plan)
    {
        closeSong (true);
        { auto made = songs::createFromPlan (engine, plan); }
        loadSong (plan.songFile());
        if (plan.importAfterCreate) browseForImport();
    }

    void openSong()
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Open a song", projectsFolder(), "*.basement;*.tracktionedit");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser&)
                              {
                                  if (chooser->getResult().existsAsFile()) loadSong (chooser->getResult());
                              });
    }

    void closeSong (bool saveFirst)
    {
        if (! edit) return;
        if (saveFirst) save();
        edit->getTransport().stop (false, true);
        edit->getTransport().removeChangeListener (this);
        selectionManager.deselectAll();
        editComponent = nullptr;
        edit = nullptr;
        songFile = juce::File();
        updateTitle();
    }

    void loadSong (const juce::File& f, bool saveCurrent = true)
    {
        closeSong (saveCurrent);
        songFile = f;
        edit = f.existsAsFile() ? songs::open (engine, f) : songs::create (engine, f);
        if (edit == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open the song", f.getFullPathName());
            songFile = juce::File();
            showHub (true);
            return;
        }
        edit->playInStopEnabled = true;
        edit->getTransport().addChangeListener (this);
        edit->getTransport().ensureContextAllocated();

        // the song's format: interface sample rate and recording bit depth
        auto info = SongInfo::readFrom (*edit, f);
        auto& dm = engine.getDeviceManager().deviceManager;
        if (auto* d = dm.getCurrentAudioDevice())
            if (std::abs (d->getCurrentSampleRate() - info.sampleRate) > 1 && d->getAvailableSampleRates().contains (info.sampleRate))
            {
                auto setup = dm.getAudioDeviceSetup();
                setup.sampleRate = info.sampleRate;
                dm.setAudioDeviceSetup (setup, true);
                basementLog ("switched the interface to " + juce::String (info.sampleRate) + " Hz for " + info.title);
            }
        setupInputs();

        int i = 0;
        for (auto t : te::getAudioTracks (*edit)) if (t->getColour() == juce::Colour()) t->setColour (theme::trackColour (i++));
        assignUntil = juce::Time::getMillisecondCounter() + 4000;
        assignInputs();

        editComponent = std::make_unique<EditComponent> (*edit, selectionManager);
        addAndMakeVisible (*editComponent);
        snapBox.setSelectedId (editComponent->getEditViewState().snapMode.get() + 1, juce::dontSendNotification);
        props().setValue ("lastSong", f.getFullPathName());
        recent::remember (props(), *edit, songFile);
        lastAutosave = juce::Time::getMillisecondCounter();
        if (crashedLastSession) juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->offerAutosave(); });
        edit->restartPlayback();
        showHub (false);
        status ("Opened " + info.title);
        statusLabel.setTooltip (info.title + ": " + info.formatText() + ", " + info.bpmText() + " BPM " + info.sigText());
    }

    void save()
    {
        if (! edit) return;
        te::EditFileOperations (*edit).save (true, true, false);
        autosaveFile().deleteFile(); // saved for real: the automatic copy is no longer newer
        recent::remember (props(), *edit, songFile);
        updateTitle();
    }

    void askForText (const juce::String& title, const juce::String& message, const juce::String& initial, std::function<void (juce::String)> then)
    {
        auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("text", initial);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, then] (int r)
        {
            auto text = w->getTextEditorContents ("text").trim();
            if (r == 1 && text.isNotEmpty()) then (text);
        }), true);
    }

    /** A copy of the whole song folder under a new name, then work on the copy */
    void saveAs()
    {
        askForText ("Save As", "Save a copy of this song (its whole folder) as:", songTitle() + " copy", [this] (juce::String name)
        {
            save();
            auto from = songFile.getParentDirectory();
            auto to = projectsFolder().getNonexistentChildFile (juce::File::createLegalFileName (name), {}, true);
            if (! from.copyDirectoryTo (to)) { status ("Couldn't copy the song folder"); return; }
            auto copied = to.getChildFile (songFile.getFileName());
            auto renamed = to.getChildFile (to.getFileName() + ".basement");
            copied.moveFileTo (renamed);
            loadSong (renamed);
            auto info = SongInfo::readFrom (*edit, songFile);
            info.title = name;
            info.writeTo (*edit);
            save();
            status ("Now working on " + name);
        });
    }

    /** Song v2, v3... in the same folder (the earlier versions stay as they were) */
    void saveNewVersion()
    {
        save();
        auto base = songFile.getFileNameWithoutExtension();
        int v = 2;
        if (auto at = base.lastIndexOf (" v"); at > 0 && base.substring (at + 2).containsOnly ("0123456789"))
        {
            v = base.substring (at + 2).getIntValue() + 1;
            base = base.substring (0, at);
        }
        auto next = songFile.getSiblingFile (base + " v" + juce::String (v) + ".basement");
        while (next.exists()) next = songFile.getSiblingFile (base + " v" + juce::String (++v) + ".basement");
        if (! songFile.copyFileTo (next)) { status ("Couldn't save a new version"); return; }
        loadSong (next);
        status ("Saved " + next.getFileNameWithoutExtension() + ". The earlier version is still in the folder.");
    }

    /** The song's tracks, names, colours, effects, tempo and routing, without any audio */
    void saveTemplate()
    {
        askForText ("Save as Template", "Name this template. It shows up under Start from when you make a new song.", songTitle(), [this] (juce::String name)
        {
            save();
            templates::folder().createDirectory();
            auto file = templates::folder().getChildFile (juce::File::createLegalFileName (name) + ".basement");
            songFile.copyFileTo (file);
            if (auto t = songs::open (engine, file))
            {
                for (auto track : te::getAudioTracks (*t))
                    for (auto* c : juce::Array<te::Clip*> (track->getClips())) // a copy: removing changes the list
                        c->removeFromParent();
                auto info = SongInfo::readFrom (*t, file);
                info.title = {};
                info.notes = {};
                info.writeTo (*t);
                te::EditFileOperations (*t).save (true, true, false);
            }
            status ("Saved the template " + name);
        });
    }

    void revert()
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Revert", "Go back to the last saved version of this song? Changes since then are lost.",
                                            "Revert", "Cancel", this, juce::ModalCallbackFunction::create ([this] (int r)
        {
            if (r == 1) { auto f = songFile; loadSong (f, false); status ("Reverted to the last save"); }
        }));
    }

    void songSetup()
    {
        auto* c = new SongSetupComponent (SongInfo::readFrom (*edit, songFile));
        c->onSave = [this] (SongInfo info) { if (! edit) return; info.writeTo (*edit); save(); status ("Song details saved"); };
        showDialog ("Song Setup", c, this);
    }

    void revealFolder (const juce::String& sub)
    {
        auto d = songFile.getParentDirectory().getChildFile (sub);
        (d.isDirectory() ? d : songFile).revealToUser();
        if (! d.isDirectory()) status ("No " + sub + " folder yet");
    }

    //==============================================================================
    // Tracks and audio
    te::AudioTrack* selectedTrack()
    {
        if (! edit) return nullptr;
        for (auto* s : selectionManager.getSelectedObjects())
        {
            if (auto t = dynamic_cast<te::AudioTrack*> (s)) return t;
            if (auto c = dynamic_cast<te::Clip*> (s)) if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack())) return t;
        }
        return nullptr;
    }

    juce::Array<te::AudioClipBase*> selectedClips()
    {
        juce::Array<te::AudioClipBase*> out;
        if (! edit) return out;
        for (auto* s : selectionManager.getSelectedObjects())
            if (auto c = dynamic_cast<te::AudioClipBase*> (s)) out.add (c);
        return out;
    }

    void forClips (const juce::String& what, std::function<void (te::AudioClipBase&)> fn)
    {
        edit->getUndoManager().beginNewTransaction (what);
        for (auto* c : selectedClips()) fn (*c);
    }

    void addTrack()
    {
        auto tracks = te::getAudioTracks (*edit);
        auto after = selectedTrack() ? selectedTrack() : (tracks.isEmpty() ? nullptr : tracks.getLast());
        if (auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, after), &selectionManager))
        {
            t->setName ("Track " + juce::String (tracks.size() + 1));
            t->setColour (theme::trackColour (tracks.size()));
            assignInput (*t, {});
        }
    }

    void duplicateTrack()
    {
        auto src = selectedTrack();
        if (src == nullptr) return;
        auto copy = src->state.createCopy();
        te::EditItemID::remapIDs (copy, nullptr, *edit);
        copy.setProperty (te::IDs::name, src->getName() + " copy", nullptr);
        copy.removeProperty ("basementInput", nullptr);
        if (auto t = edit->insertTrack (te::TrackInsertPoint (nullptr, src), copy, &selectionManager))
            status ("Duplicated " + src->getName());
    }

    void renameTrack()
    {
        auto t = selectedTrack();
        if (t == nullptr) return;
        juce::Component::SafePointer<MainComponent> safe (this);
        askForText ("Rename Track", "New name:", t->getName(), [safe, t] (juce::String name) { if (safe != nullptr) t->setName (name); });
    }

    void browseForImport()
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Add audio files as tracks", juce::File (props().getValue ("importFolder")),
                                                            "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this, chooser] (const juce::FileChooser&)
                              {
                                  auto files = chooser->getResults();
                                  if (! files.isEmpty()) props().setValue ("importFolder", files[0].getParentDirectory().getFullPathName());
                                  importFiles (files);
                              });
    }

    /** Each file becomes a new track, starting at the top of the song (so stems line up).
        The file is copied into the song's Audio folder so the song stays in one piece. */
    void importFiles (const juce::Array<juce::File>& files)
    {
        if (! edit || files.isEmpty()) return;
        auto audioDir = songFile.getParentDirectory().getChildFile ("Audio");
        int added = 0;
        for (auto& src : files)
        {
            auto file = src;
            if (! src.isAChildOf (songFile.getParentDirectory()))
            {
                audioDir.createDirectory();
                file = audioDir.getNonexistentChildFile (src.getFileNameWithoutExtension(), src.getFileExtension(), false);
                if (! src.copyFileTo (file)) { status ("Couldn't copy " + src.getFileName()); continue; }
            }
            te::AudioFile af (engine, file);
            if (! af.isValid()) { status ("Couldn't read " + src.getFileName()); continue; }

            // use an empty track if there is one, otherwise add one
            te::AudioTrack* track = nullptr;
            for (auto t : te::getAudioTracks (*edit))
                if (t->getClips().isEmpty() && ! EngineHelpers::isTrackArmed (*t)) { track = t; break; }
            if (track == nullptr)
            {
                auto tracks = te::getAudioTracks (*edit);
                track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, tracks.isEmpty() ? nullptr : tracks.getLast()), nullptr).get();
                if (track) track->setColour (theme::trackColour (tracks.size()));
            }
            if (track == nullptr) continue;
            track->setName (src.getFileNameWithoutExtension());
            track->insertWaveClip (src.getFileNameWithoutExtension(), file,
                                   { { 0_tp, te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
            ++added;
        }
        if (added) status ("Added " + juce::String (added) + (added == 1 ? " track" : " tracks"));
        save();
    }

    void deleteSelection()
    {
        auto clips = selectedClips();
        if (! clips.isEmpty()) { removeClips (clips, "Delete"); return; }
        if (auto track = selectedTrack()) edit->deleteTrack (track);
        selectionManager.deselectAll();
    }

    void removeClips (juce::Array<te::AudioClipBase*> clips, const juce::String& what)
    {
        edit->getUndoManager().beginNewTransaction (what);
        selectionManager.deselectAll();
        for (auto* c : clips) c->removeFromParent();
    }

    //==============================================================================
    // Clipboard: audio clips, pasted at the playhead keeping their spacing
    struct ClipCopy
    {
        juce::File file;
        juce::String name;
        te::TimeDuration startFromFirst, length, offset, fadeIn, fadeOut;
        float gainDb = 0;
        bool reversed = false, muted = false;
    };
    juce::Array<ClipCopy> clipboard;

    void copyClips()
    {
        auto clips = selectedClips();
        if (clips.isEmpty()) return;
        clipboard.clear();
        auto first = clips[0]->getPosition().getStart();
        for (auto* c : clips) first = juce::jmin (first, c->getPosition().getStart());
        for (auto* c : clips)
            if (auto w = dynamic_cast<te::WaveAudioClip*> (c))
                clipboard.add ({ w->getAudioFile().getFile(), w->getName(), w->getPosition().getStart() - first, w->getPosition().getLength(),
                                 w->getPosition().getOffset(), w->getFadeIn(), w->getFadeOut(), w->getGainDB(), w->getIsReversed(), w->isMuted() });
        status ("Copied " + juce::String (clipboard.size()) + (clipboard.size() == 1 ? " clip" : " clips"));
    }

    void pasteClips (te::TimePosition at, te::AudioTrack* track)
    {
        if (clipboard.isEmpty()) return;
        if (track == nullptr) { auto all = te::getAudioTracks (*edit); if (all.isEmpty()) return; track = all[0]; }
        edit->getUndoManager().beginNewTransaction ("Paste");
        selectionManager.deselectAll();
        for (auto& c : clipboard)
            if (auto w = track->insertWaveClip (c.name, c.file, { { at + c.startFromFirst, c.length }, c.offset }, false))
            {
                w->setGainDB (c.gainDb);
                w->setFadeIn (c.fadeIn);
                w->setFadeOut (c.fadeOut);
                w->setIsReversed (c.reversed);
                w->setMuted (c.muted);
                selectionManager.select (w.get(), true);
            }
    }

    void duplicateClips()
    {
        auto clips = selectedClips();
        if (clips.isEmpty()) return;
        auto end = clips[0]->getPosition().getEnd();
        for (auto* c : clips) end = juce::jmax (end, c->getPosition().getEnd());
        auto track = dynamic_cast<te::AudioTrack*> (clips[0]->getTrack());
        auto keep = clipboard;
        copyClips();
        pasteClips (end, track);
        clipboard = keep;
    }

    /** Splits the selected clips at the playhead, or every clip under it if none are selected */
    void splitAtPlayhead()
    {
        auto t = edit->getTransport().getPosition();
        auto clips = selectedClips();
        if (clips.isEmpty())
            for (auto track : te::getAudioTracks (*edit))
                for (auto* c : track->getClips())
                    if (auto a = dynamic_cast<te::AudioClipBase*> (c)) clips.add (a);
        edit->getUndoManager().beginNewTransaction ("Split");
        int n = 0;
        for (auto* c : clips)
            if (c->getPosition().time.contains (t) && t > c->getPosition().getStart())
                if (auto ct = c->getClipTrack()) { ct->splitClip (*c, t); ++n; }
        status (n == 0 ? juce::String ("Nothing under the playhead to split") : "Split " + juce::String (n) + (n == 1 ? " clip" : " clips"));
    }

    void selectAll()
    {
        selectionManager.deselectAll();
        for (auto track : te::getAudioTracks (*edit))
            for (auto* c : track->getClips()) selectionManager.select (c, true);
    }

    /** Clip gain so the loudest peak in the clip lands at -0.3 dBFS */
    void normalizeClips()
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        edit->getUndoManager().beginNewTransaction ("Normalize");
        int done = 0;
        for (auto* c : selectedClips())
            if (auto w = dynamic_cast<te::WaveAudioClip*> (c))
                if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (w->getAudioFile().getFile()) })
                {
                    auto from = (juce::int64) (w->getPosition().getOffset().inSeconds() * r->sampleRate);
                    auto num = (juce::int64) (w->getPosition().getLength().inSeconds() * r->sampleRate);
                    juce::Range<float> ranges[8];
                    const int ch = juce::jmin (8, (int) r->numChannels);
                    r->readMaxLevels (from, juce::jmin (num, r->lengthInSamples - from), ranges, ch);
                    float peak = 0;
                    for (int i = 0; i < ch; ++i) peak = juce::jmax (peak, std::abs (ranges[i].getStart()), std::abs (ranges[i].getEnd()));
                    if (peak > 0.00001f) { w->setGainDB (juce::jlimit (-60.0f, 24.0f, -0.3f - juce::Decibels::gainToDecibels (peak))); ++done; }
                }
        status (done ? "Normalized " + juce::String (done) + (done == 1 ? " clip" : " clips") + " to -0.3 dB" : juce::String ("Nothing to normalize"));
    }

    void fadeToPlayhead (bool in)
    {
        auto t = edit->getTransport().getPosition();
        forClips (in ? "Fade in" : "Fade out", [t, in] (te::AudioClipBase& c)
        {
            auto pos = c.getPosition();
            if (! pos.time.contains (t)) return;
            if (in) c.setFadeIn (t - pos.getStart());
            else    c.setFadeOut (pos.getEnd() - t);
        });
    }

    void startPluginSearch()
    {
        pluginFinder.start (juce::StringArray::fromLines (props().getValue ("pluginFolders")));
    }

    void showPluginList()
    {
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Plugin Manager";
        o.dialogBackgroundColour = theme::surface;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = true;
        auto* v = new juce::PluginListComponent (engine.getPluginManager().pluginFormatManager,
                                                 engine.getPluginManager().knownPluginList,
                                                 engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                                 std::addressof (props()), true);
        v->setSize (800, 560);
        o.content.setOwned (v);
        o.launchAsync();
        status ("Options > Scan for new or updated VST3 plug-ins finds everything installed, Encore's included.");
    }

    //==============================================================================
    // Export: the whole song (every track, every effect) to a 24-bit WAV
    void exportMix()
    {
        if (currentExport) { status ("Already exporting..."); return; }
        if (edit->getLength() <= 0s) { status ("Nothing to export yet: record or import something first."); return; }
        save();
        auto def = songFile.getParentDirectory().getChildFile (songTitle() + " mix.wav");
        auto chooser = std::make_shared<juce::FileChooser> ("Export the mix", def, "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, chooser] (const juce::FileChooser&)
                              {
                                  auto f = chooser->getResult();
                                  if (f == juce::File()) return;
                                  const double sr = engine.getDeviceManager().getSampleRate() > 0 ? engine.getDeviceManager().getSampleRate() : 48000.0;
                                  exportButton.setEnabled (false);
                                  currentExport = exportSong (engine, songFile, f.withFileExtension ("wav"), sr, [this] (juce::File out, juce::String err)
                                  {
                                      currentExport = nullptr;
                                      exportButton.setEnabled (true);
                                      if (out.existsAsFile()) { status ("Exported " + out.getFileName()); out.revealToUser(); }
                                      else status ("The export didn't work: " + err);
                                  });
                                  if (currentExport == nullptr) exportButton.setEnabled (true);
                              });
    }
    std::shared_ptr<SongExport> currentExport;

    /** The whole song folder in one zip (audio stored, not recompressed): for backups and sending to a studio */
    void exportZip()
    {
        save();
        auto def = songFile.getParentDirectory().getSiblingFile (songFile.getParentDirectory().getFileName() + ".zip");
        auto chooser = std::make_shared<juce::FileChooser> ("Zip the song folder", def, "*.zip");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, chooser] (const juce::FileChooser&)
        {
            if (chooser->getResult() == juce::File()) return;
            auto dest = chooser->getResult().withFileExtension ("zip");
            struct Zipper : public juce::ThreadWithProgressWindow
            {
                Zipper (juce::File d, juce::File o) : ThreadWithProgressWindow ("Zipping " + d.getFileName(), true, true), dir (d), out (o) {}
                void run() override
                {
                    juce::ZipFile::Builder b;
                    for (auto& f : dir.findChildFiles (juce::File::findFiles, true))
                        b.addFile (f, isAudio (f) ? 0 : 6, dir.getFileName() + "/" + f.getRelativePathFrom (dir).replaceCharacter ('\\', '/'));
                    out.deleteFile();
                    juce::FileOutputStream os (out);
                    double progress = 0;
                    ok = os.openedOk() && b.writeToStream (os, &progress);
                    if (threadShouldExit()) ok = false;
                }
                static bool isAudio (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a"); }
                juce::File dir, out;
                bool ok = false;
            } z (songFile.getParentDirectory(), dest);
            z.runThread();
            if (z.ok) { status ("Zipped to " + dest.getFileName()); dest.revealToUser(); }
            else { dest.deleteFile(); status ("The zip didn't finish"); }
        });
    }

    //==============================================================================
    // Transport
    void togglePlay()
    {
        const bool wasRecording = edit->getTransport().isRecording();
        EngineHelpers::togglePlay (*edit);
        if (wasRecording) save();
    }
    void toggleRecord()
    {
        auto& tr = edit->getTransport();
        if (tr.isRecording()) { tr.stop (false, false); save(); return; }
        bool anyArmed = false;
        for (auto t : te::getAudioTracks (*edit)) anyArmed = anyArmed || EngineHelpers::isTrackArmed (*t);
        if (! anyArmed) { status ("Arm a track first: the R button on the track, after choosing its input."); return; }
        const int beats = tr.isPlaying() ? 0 : edit->getNumCountInBeats();
        tr.record (false);
        if (beats > 0)
        {
            const double bpm = edit->tempoSequence.getTempo (0)->getBpm();
            countIn.toFront (false);
            countIn.start (beats, 60.0 / juce::jmax (20.0, bpm));
        }
    }
    void togglePunch()
    {
        auto& tr = edit->getTransport();
        if (! edit->recordingPunchInOut.get() && tr.getLoopRange().getLength() <= 0s) loopToSelection();
        edit->recordingPunchInOut = ! edit->recordingPunchInOut.get();
        status (edit->recordingPunchInOut.get() ? "Punch in/out on: recording only happens inside the loop range (shown on the ruler)"
                                                : juce::String ("Punch in/out off"));
    }

    void moveBars (int n)
    {
        auto& ts = edit->tempoSequence;
        const int num = juce::jmax (1, ts.getTimeSig (0)->numerator.get());
        auto beats = ts.toBeats (edit->getTransport().getPosition()).inBeats();
        auto bar = n < 0 ? std::ceil (beats / num - 0.001) - 1 : std::floor (beats / num + 0.001) + 1;
        edit->getTransport().setPosition (ts.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, bar) * num)));
    }
    void loopToSelection()
    {
        auto clips = selectedClips();
        te::TimeRange r { 0_tp, edit->getLength() };
        if (! clips.isEmpty())
        {
            r = clips[0]->getPosition().time;
            for (auto* c : clips) r = r.getUnionWith (c->getPosition().time);
        }
        if (r.getLength() > 0s) edit->getTransport().setLoopRange (r);
    }
    void toggleLoop()
    {
        auto& tr = edit->getTransport();
        if (! tr.looping.get() && (tr.getLoopRange().getLength() <= 0s || ! selectedClips().isEmpty())) loopToSelection();
        tr.looping = ! tr.looping.get();
    }
    void setTempo (double bpm)
    {
        if (! edit) return;
        if (bpm >= 20 && bpm <= 400) edit->tempoSequence.getTempo (0)->setBpm (bpm);
        SongInfo i;
        i.bpm = edit->tempoSequence.getTempo (0)->getBpm();
        tempoLabel.setText (i.bpmText(), juce::dontSendNotification);
    }

    //==============================================================================
    // View
    void zoom (double factor)
    {
        auto& v = editComponent->getEditViewState();
        auto centre = edit->getTransport().getPosition().inSeconds();
        auto x1 = v.viewX1.get().inSeconds(), x2 = v.viewX2.get().inSeconds();
        if (centre < x1 || centre > x2) centre = (x1 + x2) * 0.5;
        auto w = juce::jlimit (0.5, 3600.0, (x2 - x1) * factor);
        auto start = juce::jmax (0.0, centre - (centre - x1) * (w / (x2 - x1)));
        v.viewX1 = te::TimePosition::fromSeconds (start);
        v.viewX2 = te::TimePosition::fromSeconds (start + w);
    }
    bool snapIs (int mode) { return editComponent != nullptr && editComponent->getEditViewState().snapMode.get() == mode; }

    void setSnap (int mode)
    {
        if (editComponent == nullptr) return;
        editComponent->getEditViewState().snapMode = juce::jlimit (0, 4, mode);
        snapBox.setSelectedId (editComponent->getEditViewState().snapMode.get() + 1, juce::dontSendNotification);
    }

    /** Selected clips one grid step left or right (a beat when snap is off) */
    void nudge (int dir)
    {
        auto clips = selectedClips();
        if (clips.isEmpty()) return;
        auto& ts = edit->tempoSequence;
        const double step = editComponent->getEditViewState().gridBeats() * dir;
        edit->getUndoManager().beginNewTransaction ("Nudge");
        for (auto* c : clips)
        {
            auto beats = ts.toBeats (c->getPosition().getStart()).inBeats() + step;
            c->setStart (ts.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, beats))), false, true);
        }
    }

    void renameClip()
    {
        auto clips = selectedClips();
        if (clips.isEmpty()) return;
        te::Clip::Ptr c = clips[0];
        askForText ("Rename Clip", "New name:", c->getName(), [c] (juce::String name) { c->setName (name); });
    }

    /** The clip right-click menu */
    void fillClipMenu (juce::PopupMenu& m, te::Clip& clip)
    {
        for (auto id : { cmdCut, cmdCopy, cmdDuplicate, cmdDelete }) addCommand (m, id);
        m.addSeparator();
        for (auto id : { cmdSplit, cmdRenameClip, cmdMuteClip }) addCommand (m, id);
        m.addSeparator();
        juce::PopupMenu audio;
        for (auto id : { cmdNormalize, cmdGainUp, cmdGainDown, cmdGainReset, cmdFadeIn, cmdFadeOut, cmdRemoveFades, cmdReverse }) addCommand (audio, id);
        m.addSubMenu ("Audio", audio);
        if (auto w = dynamic_cast<te::WaveAudioClip*> (&clip); w != nullptr && w->hasAnyTakes())
        {
            juce::PopupMenu takes;
            const int n = w->getNumTakes (false), current = w->getCurrentTake();
            te::Clip::Ptr keep = w;
            for (int i = 0; i < n; ++i)
                takes.addItem ("Take " + juce::String (i + 1), true, i == current, [keep, i] { keep->setCurrentTake (i); });
            m.addSubMenu ("Takes (" + juce::String (n) + ")", takes);
        }
        if (auto w = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            auto f = w->getAudioFile().getFile();
            m.addSeparator();
            m.addItem ("Show File in Folder", f.existsAsFile(), false, [f] { f.revealToUser(); });
        }
    }

    void setAllTrackHeights (int h)
    {
        for (auto t : te::getAllTracks (*edit)) t->state.setProperty ("basementHeight", h, nullptr);
        editComponent->relayout();
    }

    void zoomToSong()
    {
        auto& v = editComponent->getEditViewState();
        v.viewX1 = 0s;
        v.viewX2 = te::TimePosition::fromSeconds (juce::jmax (10.0, edit->getLength().inSeconds() * 1.05));
    }

    //==============================================================================
    // Help
    void showShortcuts()
    {
        juce::String text;
        juce::String category;
        juce::Array<juce::CommandID> ids;
        getAllCommands (ids);
        for (auto id : ids)
        {
            juce::ApplicationCommandInfo info (id);
            getCommandInfo (id, info);
            if (info.defaultKeypresses.isEmpty()) continue;
            if (info.categoryName != category) { category = info.categoryName; text << (text.isEmpty() ? "" : "\n") << category.toUpperCase() << "\n"; }
            juce::StringArray keys;
            for (auto& k : info.defaultKeypresses) keys.add (keyText (k));
            text << "  " << info.shortName.paddedRight (' ', 30) << keys.joinIntoString ("  or  ") << "\n";
        }
        auto* ed = new juce::TextEditor();
        ed->setMultiLine (true);
        ed->setReadOnly (true);
        ed->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
        ed->setText (text);
        ed->setSize (560, 620);
        showDialog ("Keyboard Shortcuts", ed, this);
    }

    void showAbout()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Encore: Basement " + juce::String (ProjectInfo::versionString),
            "A free, open-source DAW from Encore Studio.\n\n"
            "Copyright (C) 2026 Encore Studio. Free software under the GNU AGPLv3.\n"
            "Built on Tracktion Engine (GPLv3) and JUCE (AGPLv3).\n\n"
            "github.com/jimmyrlondo/encore-basement");
    }

    //==============================================================================
    /** Right of the status bar: the interface, its format, latency and how busy the audio is */
    void updateDeviceLabel()
    {
        static int tick = 0;
        if (++tick % 10 != 0) return; // twice a second is plenty
        auto& dm = engine.getDeviceManager().deviceManager;
        juce::String s;
        if (auto* d = dm.getCurrentAudioDevice())
        {
            const double ms = d->getCurrentBufferSizeSamples() * 1000.0 / juce::jmax (1.0, d->getCurrentSampleRate());
            s << d->getName() << " (" << dm.getCurrentAudioDeviceType() << ")   " << juce::String (d->getCurrentSampleRate() / 1000.0, 1) << " kHz   "
              << d->getCurrentBufferSizeSamples() << " samples (" << juce::String (ms, 1) << " ms)   CPU "
              << juce::roundToInt (engine.getDeviceManager().getCpuUsage() * 100.0) << "%";
        }
        else s = "No audio interface: Studio > Audio Setup";
        if (deviceLabel.getText() != s) deviceLabel.setText (s, juce::dontSendNotification);
    }

    //==============================================================================
    // Autosave: every two minutes, a copy of the song (only if it changed) next to
    // it in .autosave, never over the song itself. If Basement didn't close
    // properly last time, opening the song offers the newer copy back.
    juce::File autosaveFile() const
    {
        return songFile.getParentDirectory().getChildFile (".autosave").getChildFile (songFile.getFileName());
    }

    void autosaveIfDue()
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastAutosave < 120000 || edit->getTransport().isRecording()) return;
        lastAutosave = now;
        if (! edit->hasChangedSinceSaved()) return;
        auto f = autosaveFile();
        f.getParentDirectory().createDirectory();
        if (te::EditFileOperations (*edit).writeToFile (f, false))
            basementLog ("autosaved " + f.getFullPathName());
    }

    /** Called when a song opens: if a newer autosave is there from a session that
        didn't end cleanly, offer it */
    void offerAutosave()
    {
        auto f = autosaveFile();
        if (! f.existsAsFile() || f.getLastModificationTime() <= songFile.getLastModificationTime()) return;
        auto when = f.getLastModificationTime().formatted ("%b %d, %H:%M");
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Recover unsaved changes?",
            "Basement didn't close properly last time. There's an automatic copy of \"" + songTitle() + "\" from " + when
            + " that's newer than the last save.\n\nOpen the recovered copy? (The last saved version is kept as \"before recovery\".)",
            "Recover", "Keep the last save", this, juce::ModalCallbackFunction::create ([this, f] (int r)
        {
            if (r != 1 || ! f.existsAsFile()) return;
            auto song = songFile;
            song.copyFileTo (song.getSiblingFile (song.getFileNameWithoutExtension() + " before recovery.basement"));
            f.copyFileTo (song);
            loadSong (song, false);
            status ("Recovered the automatic copy");
        }));
    }

    void changeListenerCallback (juce::ChangeBroadcaster* b) override
    {
        if (b == &engine.getDeviceManager()) // an interface was added, removed or switched
        {
            setupInputs();
            assignUntil = juce::Time::getMillisecondCounter() + 4000;
            if (edit) assignInputs();
            return;
        }
        timerCallback();
    }

    void timerCallback() override
    {
        updateDeviceLabel();
        if (! edit) return;
        autosaveIfDue();
        if (assignUntil != 0 && juce::Time::getMillisecondCounter() < assignUntil + 1000) assignInputs();
        if (hubShown) return;
        auto& tr = edit->getTransport();
        if (auto* ctx = tr.getCurrentPlaybackContext()) masterMeter.setSource (&ctx->masterLevels);
        const auto pos = tr.getPosition().inSeconds();
        timeLabel.setText (juce::String ((int) pos / 60) + ":" + juce::String (std::fmod (pos, 60.0), 1).paddedLeft ('0', 4), juce::dontSendNotification);
        auto& ts = edit->tempoSequence;
        const int num = juce::jmax (1, ts.getTimeSig (0)->numerator.get());
        const double beats = ts.toBeats (tr.getPosition()).inBeats();
        barLabel.setText ("Bar " + juce::String ((int) (beats / num) + 1) + "." + juce::String ((int) std::fmod (beats, (double) num) + 1), juce::dontSendNotification);
        playButton.setButtonText (tr.isPlaying() ? "Stop" : "Play");
        if (currentExport) status ("Exporting... " + juce::String (juce::roundToInt (currentExport->progress() * 100)) + "%");
        recordButton.setToggleState (tr.isRecording(), juce::dontSendNotification);
        clickButton.setToggleState (edit->clickTrackEnabled.get(), juce::dontSendNotification);
        loopButton.setToggleState (tr.looping.get(), juce::dontSendNotification);
        if (! tempoLabel.isBeingEdited())
        {
            SongInfo i;
            i.bpm = ts.getTempo (0)->getBpm();
            auto bpm = i.bpmText();
            if (tempoLabel.getText() != bpm) tempoLabel.setText (bpm, juce::dontSendNotification);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
