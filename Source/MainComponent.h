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
/** Basement.log next to the settings: what happened with devices and songs,
    for bug reports. Kept short (trimmed when it passes 200 KB). */
inline void basementLog (const juce::String& line)
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Encore Basement").getChildFile ("Basement.log");
    if (f.getSize() > 200000) f.deleteFile();
    f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + line + juce::newLine);
}

//==============================================================================
/** Engine hooks: recordings go into the song's own folder */
class BasementBehaviour : public te::EngineBehaviour
{
public:
    std::function<juce::File()> songFolder;

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

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::KeyListener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    std::function<void (const juce::String&)> onTitleChange;

    MainComponent()
    {
        behaviour->songFolder = [this] { return songFile.getParentDirectory(); };

        for (auto* b : { &newButton, &openButton, &saveButton, &importButton, &addTrackButton, &pluginsButton, &audioButton, &exportButton,
                         &homeButton, &playButton, &recordButton, &clickButton })
        {
            addAndMakeVisible (b);
            b->setMouseClickGrabsKeyboardFocus (false);
        }
        Helpers::addAndMakeVisible (*this, { &timeLabel, &barLabel, &tempoLabel, &tempoCaption, &statusLabel });

        exportButton.setColour (juce::TextButton::buttonColourId, theme::brand);
        recordButton.setColour (juce::TextButton::buttonOnColourId, theme::record);
        recordButton.setColour (juce::TextButton::textColourOnId, theme::paper);
        clickButton.setClickingTogglesState (false);

        newButton.setTooltip ("Start a new song (Ctrl+N)");
        openButton.setTooltip ("Open a song (Ctrl+O)");
        saveButton.setTooltip ("Save (Ctrl+S). Basement also saves when you stop recording and when you quit.");
        importButton.setTooltip ("Add audio files as new tracks (or drag them in) (Ctrl+I)");
        addTrackButton.setTooltip ("Add a track to record on (Ctrl+T)");
        pluginsButton.setTooltip ("Find your VST3 plugins (Encore plugins included)");
        audioButton.setTooltip ("Pick your audio interface, inputs and outputs");
        exportButton.setTooltip ("Mix the whole song down to a WAV");
        homeButton.setTooltip ("Back to the start (Home)");
        playButton.setTooltip ("Play / stop (Space)");
        recordButton.setTooltip ("Record on the armed tracks (R)");
        clickButton.setTooltip ("Metronome click");

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
        statusLabel.setJustificationType (juce::Justification::centredRight);

        newButton.onClick = [this] { askForNewSong(); };
        openButton.onClick = [this] { openSong(); };
        saveButton.onClick = [this] { save(); };
        importButton.onClick = [this] { browseForImport(); };
        addTrackButton.onClick = [this] { addTrack(); };
        pluginsButton.onClick = [this] { showPluginList(); };
        audioButton.onClick = [this] { EngineHelpers::showAudioDeviceSettings (engine); };
        exportButton.onClick = [this] { exportMix(); };
        homeButton.onClick = [this] { if (edit) edit->getTransport().setPosition (0s); };
        playButton.onClick = [this] { togglePlay(); };
        recordButton.onClick = [this] { toggleRecord(); };
        clickButton.onClick = [this] { if (edit) edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); };

        setupInputs();
        engine.getDeviceManager().addChangeListener (this);
        setSize (1400, 860);
        startTimerHz (20);

        // pick up where you left off, or start fresh
        auto last = juce::File (props().getValue ("lastSong"));
        if (last.existsAsFile())
            loadSong (last);
        else
            juce::MessageManager::callAsync ([this] { ensureProjectsFolder ([this] { newSong ("Untitled"); }); });
    }

    ~MainComponent() override
    {
        stopTimer();
        engine.getDeviceManager().removeChangeListener (this);
        if (edit) { save(); edit->getTransport().removeChangeListener (this); }
        editComponent = nullptr;
        edit = nullptr;
        engine.getTemporaryFileManager().getTempDirectory().deleteRecursively();
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::base);
        auto top = getLocalBounds().removeFromTop (topBarH);
        g.setColour (theme::surface);
        g.fillRect (top);
        g.setColour (theme::strong);
        g.fillRect (top.removeFromBottom (1));

        // wordmark: ENCORE: in the brand square's spirit, BASEMENT in paper
        auto mark = getLocalBounds().removeFromTop (topBarH).reduced (16, 0).removeFromLeft (180);
        g.setColour (theme::brand);
        g.fillRect (mark.getX(), mark.getCentreY() + 4, 7, 7);
        g.setColour (theme::text2);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("ENCORE:", mark.withTrimmedLeft (12).withTrimmedBottom (mark.getHeight() / 2 - 2), juce::Justification::bottomLeft);
        g.setColour (theme::paper);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("BASEMENT", mark.withTrimmedLeft (12).withTrimmedTop (mark.getHeight() / 2 - 2), juce::Justification::topLeft);

        auto bar = getLocalBounds().withTrimmedTop (topBarH).removeFromTop (transportH);
        g.setColour (theme::surface);
        g.fillRect (bar);
        g.setColour (theme::strong);
        g.fillRect (bar.removeFromBottom (1));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (topBarH).reduced (12, 12);
        top.removeFromLeft (190);
        auto place = [&top] (juce::Button& b, int w) { b.setBounds (top.removeFromLeft (w)); top.removeFromLeft (6); };
        place (newButton, 96); place (openButton, 70); place (saveButton, 62);
        top.removeFromLeft (14);
        place (addTrackButton, 98); place (importButton, 116); place (pluginsButton, 78); place (audioButton, 104);
        exportButton.setBounds (top.removeFromRight (118));
        statusLabel.setBounds (top.withTrimmedRight (10));

        auto t = r.removeFromTop (transportH).reduced (12, 9);
        homeButton.setBounds (t.removeFromLeft (40)); t.removeFromLeft (6);
        playButton.setBounds (t.removeFromLeft (84)); t.removeFromLeft (6);
        recordButton.setBounds (t.removeFromLeft (90)); t.removeFromLeft (20);
        timeLabel.setBounds (t.removeFromLeft (130));
        barLabel.setBounds (t.removeFromLeft (90)); t.removeFromLeft (16);
        auto tempo = t.removeFromLeft (70);
        tempoCaption.setBounds (tempo.removeFromTop (12));
        tempoLabel.setBounds (tempo);
        t.removeFromLeft (8);
        clickButton.setBounds (t.removeFromLeft (70));

        if (editComponent) editComponent->setBounds (r);
    }

    //==============================================================================
    // Keyboard: Space play/stop, R record, Home to start, Ctrl+S/O/N/T/I/Z/Y, Delete
    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        using KP = juce::KeyPress;
        auto cmd = k.getModifiers().isCommandDown();
        auto key = (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode()); // letters can arrive in either case
        if (key >= 1 && key <= 26) { key = 'A' + key - 1; cmd = true; } // some keyboards/tools send Ctrl+letter as a control code
        if (! edit) return false;
        if (k == KP::spaceKey) { togglePlay(); return true; }
        if (k == KP::homeKey) { edit->getTransport().setPosition (0s); return true; }
        if (! cmd && key == 'R') { toggleRecord(); return true; }
        if (cmd && key == 'S') { save(); return true; }
        if (cmd && key == 'O') { openSong(); return true; }
        if (cmd && key == 'N') { askForNewSong(); return true; }
        if (cmd && key == 'T') { addTrack(); return true; }
        if (cmd && key == 'I') { browseForImport(); return true; }
        if (cmd && key == 'Z' && ! k.getModifiers().isShiftDown()) { edit->getUndoManager().undo(); return true; }
        if (cmd && (key == 'Y' || (key == 'Z' && k.getModifiers().isShiftDown()))) { edit->getUndoManager().redo(); return true; }
        if (k == KP::deleteKey || k == KP::backspaceKey) { deleteSelection(); return true; }
        return false;
    }

    // Drop audio files anywhere: each becomes a track
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (isAudioFile (juce::File (f))) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        juce::Array<juce::File> list;
        for (auto& f : files) if (isAudioFile (juce::File (f))) list.add (juce::File (f));
        importFiles (list);
    }

private:
    //==============================================================================
    static constexpr int topBarH = 60, transportH = 52;
    BasementBehaviour* behaviour = new BasementBehaviour(); // owned by the engine
    te::Engine engine { "Encore Basement", std::make_unique<BasementUIBehaviour>(), std::unique_ptr<te::EngineBehaviour> (behaviour) };
    te::SelectionManager selectionManager { engine };
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<EditComponent> editComponent;
    juce::File songFile;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton newButton { "New song" }, openButton { "Open" }, saveButton { "Save" },
                     importButton { "Import audio" }, addTrackButton { "Add track" }, pluginsButton { "Plugins" },
                     audioButton { "Audio setup" }, exportButton { "Export mix" },
                     homeButton { "|<" }, playButton { "Play" }, recordButton { "Record" }, clickButton { "Click" };
    juce::Label timeLabel { {}, "0:00.0" }, barLabel { {}, "Bar 1.1" }, tempoLabel { {}, "120" }, tempoCaption { {}, "BPM" }, statusLabel;

    juce::PropertiesFile& props() { return engine.getPropertyStorage().getPropertiesFile(); }
    static bool isAudioFile (const juce::File& f)
    {
        return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf;w64;bwf");
    }
    void status (const juce::String& s) { statusLabel.setText (s, juce::dontSendNotification); }

    //==============================================================================
    // Every input on the interface is its own mono input (Input 1, Input 2...),
    // so a mic on 1 and a guitar on 2 go to separate tracks. Pairs can still be
    // recorded as stereo by picking a stereo input on the track.
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
                wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                wip->setEnabled (true);
            }
    }

    /** A new song's tracks get inputs 1, 2, 3... (the input list fills in a moment
        after startup, so this runs again once it has) */
    bool pendingInputs = false;
    void assignInputsToTracksWithout()
    {
        if (! edit) return;
        {
            juce::StringArray inst;
            for (auto instance : edit->getAllInputDevices())
                inst.add (instance->getInputDevice().getName() + (instance->getTargets().isEmpty() ? "" : " (in use)"));
            basementLog ("assigning inputs; song sees: " + inst.joinIntoString (", "));
        }
        bool all = true;
        for (auto t : te::getAudioTracks (*edit))
            if (! EngineHelpers::trackHasInput (*t))
            {
                assignNextFreeInput (*t);
                all = all && EngineHelpers::trackHasInput (*t);
            }
        if (all && pendingInputs) { pendingInputs = false; save(); }
    }

    /** The first input no track is using yet */
    void assignNextFreeInput (te::AudioTrack& t)
    {
        for (auto instance : edit->getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice && instance->getTargets().isEmpty())
            {
                auto res = instance->setTarget (t.itemID, true, &edit->getUndoManager(), 0);
                basementLog (t.getName() + " <- " + instance->getInputDevice().getName() + (res ? " ok" : " FAILED: " + juce::String (res.error())));
                return;
            }
    }

    //==============================================================================
    // Songs
    void ensureProjectsFolder (std::function<void()> then)
    {
        auto f = juce::File (props().getValue ("projectsFolder"));
        if (f.isDirectory()) { then(); return; }
        auto chooser = std::make_shared<juce::FileChooser> ("Where should Basement keep your songs? (Pick a roomy drive)",
                                                            juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser, then] (const juce::FileChooser&)
                              {
                                  auto dir = chooser->getResult();
                                  if (dir == juce::File()) dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Encore Basement");
                                  if (dir.getFileName() != "Encore Basement" && ! dir.getChildFile ("Encore Basement").exists()) dir = dir.getChildFile ("Encore Basement");
                                  dir.createDirectory();
                                  props().setValue ("projectsFolder", dir.getFullPathName());
                                  then();
                              });
    }
    juce::File projectsFolder() { return juce::File (props().getValue ("projectsFolder")); }

    void askForNewSong()
    {
        ensureProjectsFolder ([this]
        {
            auto* w = new juce::AlertWindow ("New song", "What's it called? (You can rename it later in its folder.)", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "Untitled");
            w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
            {
                if (r == 1) newSong (w->getTextEditorContents ("name"));
            }), true);
        });
    }

    void newSong (juce::String name)
    {
        name = juce::File::createLegalFileName (name.trim().isEmpty() ? juce::String ("Untitled") : name.trim());
        auto folder = projectsFolder().getNonexistentChildFile (name, {}, true);
        folder.createDirectory();
        loadSong (folder.getChildFile (folder.getFileName() + ".basement"));
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

    void loadSong (const juce::File& f)
    {
        if (edit) { save(); edit->getTransport().removeChangeListener (this); }
        selectionManager.deselectAll();
        editComponent = nullptr;

        const bool isNew = ! f.existsAsFile();
        songFile = f;
        edit = isNew ? songs::create (engine, f) : songs::open (engine, f);
        if (edit == nullptr) { status ("Couldn't open " + f.getFileName()); edit = songs::create (engine, f.getSiblingFile (f.getFileNameWithoutExtension() + " (recovered).basement")); }
        edit->playInStopEnabled = true;
        edit->getTransport().addChangeListener (this);
        edit->getTransport().ensureContextAllocated();

        if (isNew)
        {
            edit->ensureNumberOfAudioTracks (2);
            int i = 0;
            for (auto t : te::getAudioTracks (*edit))
            {
                t->setName ("Track " + juce::String (i + 1));
                t->setColour (theme::trackColour (i++));
            }
            pendingInputs = true;
            assignInputsToTracksWithout();
            save();
        }

        if (! isNew) { pendingInputs = true; assignInputsToTracksWithout(); } // inputs saved with the song may be gone (different interface)

        editComponent = std::make_unique<EditComponent> (*edit, selectionManager);
        addAndMakeVisible (*editComponent);
        props().setValue ("lastSong", f.getFullPathName());
        edit->restartPlayback();
        updateTitle();
        status (isNew ? "New song in " + f.getParentDirectory().getFullPathName() : "Opened " + f.getFileNameWithoutExtension());
        resized();
    }

    void save()
    {
        if (! edit) return;
        te::EditFileOperations (*edit).save (true, true, false);
        updateTitle();
    }

public:
    void updateTitle()
    {
        if (onTitleChange) onTitleChange ("Encore: Basement - " + songFile.getFileNameWithoutExtension());
    }

private:
    //==============================================================================
    // Tracks and audio
    void addTrack()
    {
        if (! edit) return;
        auto tracks = te::getAudioTracks (*edit);
        basementLog ("add track (have " + juce::String (tracks.size()) + ")");
        if (auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, tracks.isEmpty() ? nullptr : tracks.getLast()), nullptr))
        {
            t->setName ("Track " + juce::String (tracks.size() + 1));
            t->setColour (theme::trackColour (tracks.size()));
            assignNextFreeInput (*t);
        }
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
        auto sel = selectionManager.getSelectedObject (0);
        if (auto clip = dynamic_cast<te::Clip*> (sel))
            clip->removeFromParent();
        else if (auto track = dynamic_cast<te::Track*> (sel))
            if (! (track->isMasterTrack() || track->isMarkerTrack() || track->isTempoTrack() || track->isChordTrack()))
                edit->deleteTrack (track);
        selectionManager.deselectAll();
    }

    void showPluginList()
    {
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Plugins";
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
        if (! edit) return;
        if (currentExport) { status ("Already exporting..."); return; }
        if (edit->getLength() <= 0s) { status ("Nothing to export yet: record or import something first."); return; }
        save();
        auto def = songFile.getParentDirectory().getChildFile (songFile.getFileNameWithoutExtension() + " mix.wav");
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

    //==============================================================================
    // Transport
    void togglePlay()
    {
        if (! edit) return;
        const bool wasRecording = edit->getTransport().isRecording();
        EngineHelpers::togglePlay (*edit);
        if (wasRecording) save();
    }
    void toggleRecord()
    {
        if (! edit) return;
        auto& tr = edit->getTransport();
        if (tr.isRecording()) { tr.stop (false, false); save(); return; }
        bool anyArmed = false;
        for (auto t : te::getAudioTracks (*edit)) anyArmed = anyArmed || EngineHelpers::isTrackArmed (*t);
        if (! anyArmed) { status ("Arm a track first: the R button on the track, after choosing its input."); return; }
        tr.record (false);
    }
    void setTempo (double bpm)
    {
        if (! edit) return;
        if (bpm >= 20 && bpm <= 400) edit->tempoSequence.getTempo (0)->setBpm (bpm);
        tempoLabel.setText (juce::String (edit->tempoSequence.getTempo (0)->getBpm(), 1).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster* b) override
    {
        if (b == &engine.getDeviceManager()) // an interface was added, removed or switched
        {
            setupInputs();
            if (pendingInputs) assignInputsToTracksWithout();
            return;
        }
        timerCallback();
    }

    void timerCallback() override
    {
        if (! edit) return;
        if (pendingInputs) assignInputsToTracksWithout();
        auto& tr = edit->getTransport();
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
        if (! tempoLabel.isBeingEdited())
        {
            auto bpm = juce::String (ts.getTempo (0)->getBpm(), 1);
            if (bpm.endsWith (".0")) bpm = bpm.dropLastCharacters (2);
            if (tempoLabel.getText() != bpm) tempoLabel.setText (bpm, juce::dontSendNotification);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
