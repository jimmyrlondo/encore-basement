// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The main window: a song (a Tracktion "Edit") with its tracks, the transport,
// and the things you do to a song (record, import, add effects, export).
// Every song lives in its own folder inside your projects folder:
//
//   <Projects>/<Song>/<Song>.encore     the song itself (XML)
//   <Projects>/<Song>/Recordings/       everything recorded into it
//   <Projects>/<Song>/Audio/            copies of imported files
//   <Projects>/<Song>/<Song> mix.wav    exports (wherever you choose)
//
// so a song can be zipped, backed up or moved to another drive in one piece.
#pragma once

#include "Theme.h"


//==============================================================================
/** Engine hooks: recordings go into the song's own folder */
class StudioBehaviour : public te::EngineBehaviour
{
public:
    StudioBehaviour (bool openAudioLater = false) : deferDevices (openAudioLater) {}
    std::function<juce::File()> songFolder;

    /** The app opens the audio interface itself once its window is on screen
        (it's the slowest part of starting up), so the window appears at once */
    bool autoInitialiseDeviceManager() override { return ! deferDevices; }
    const bool deferDevices;

    /** Plugins are first opened in a helper copy of Studio, so one that crashes, hangs, or stops
        to ask for activation can't freeze or take down the app (Tracktion's default is in-process) */
    bool canScanPluginsOutOfProcess() override { return true; }

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
    folder): one ".." off, so clips go missing after a song is reopened. Studio
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
    std::atomic<int> finishing { 0 };   // rendered, now 1 mastering or 2 making the MP3
    float progress() const { return finishing > 0 ? 1.0f : handle ? handle->getProgress() : 0.0f; }
};

/** How a mix (or one track) is written out */
struct ExportOptions
{
    double sampleRate = 48000.0;
    int bitDepth = 24;                 // 16, 24, or 32 (WAV only: float)
    bool flac = false;
    int mp3Kbps = 0;                   // an MP3 at this bitrate, or 0 for WAV/FLAC
    mp3::Tags tags;                    // title, artist... written into an MP3
    std::optional<te::TimeRange> range;// the whole song when empty
    int trackIndex = -1;               // one audio track (a stem), or -1 for the mix
    bool masterEffects = true;
    bool tail = true;                  // let reverbs and delays ring out (2 s)
    bool loudness = false;             // normalise to -14 LUFS (streaming level)
    mastering::Settings mastering;     // through Encore Master on the way out (the mix only)
};

inline std::shared_ptr<SongExport> exportSong (te::Engine& engine, const juce::File& songFile, juce::File dest, ExportOptions o,
                                               std::function<void (juce::File, juce::String)> done)
{
    auto job = std::make_shared<SongExport>();
    job->edit = songs::open (engine, songFile, te::Edit::EditRole::forRendering);
    if (job->edit == nullptr || job->edit->getLength() <= 0s) { done ({}, "There's nothing in the song to export yet."); return {}; }

    // mastering and MP3 happen after the render: Tracktion writes a float WAV next to the
    // file, then a background thread masters and/or encodes it into the real one
    const bool master = o.mastering.on() && o.trackIndex < 0;
    if (master || o.mp3Kbps > 0)
    {
        const auto finalFile = dest;
        const auto want = o;
        dest = dest.getSiblingFile (dest.getFileNameWithoutExtension() + " (finishing).wav");
        o.flac = false;
        o.bitDepth = 32;
        if (o.mp3Kbps > 0) o.sampleRate = mp3::rateFor (o.sampleRate);
        if (master) o.loudness = false;   // Encore Master sets the loudness itself
        std::weak_ptr<SongExport> weak = job;
        done = [weak, finalFile, want, master, done] (juce::File wav, juce::String err)
        {
            auto j = weak.lock();
            if (! wav.existsAsFile() || j == nullptr) { wav.deleteFile(); done ({}, err); return; }
            j->finishing = master ? 1 : 2;
            std::thread ([weak, wav, finalFile, want, master, done]
            {
                juce::String problem;
                auto source = wav;
                const auto mastered = wav.getSiblingFile (wav.getFileNameWithoutExtension() + " mastered.wav");
                if (master)
                {
                    const bool toMp3 = want.mp3Kbps > 0;
                    problem = mastering::masterFile (wav, toMp3 ? mastered : finalFile, want.mastering, toMp3 ? 32 : want.bitDepth, ! toMp3 && want.flac);
                    source = mastered;
                    if (auto j2 = weak.lock()) j2->finishing = 2;
                }
                if (problem.isEmpty() && want.mp3Kbps > 0)
                    problem = mp3::encode (source, finalFile, want.mp3Kbps, want.tags);
                wav.deleteFile();
                mastered.deleteFile();
                juce::MessageManager::callAsync ([weak, finalFile, problem, done]
                {
                    if (weak.expired()) return;   // the app closed meanwhile
                    done (problem.isEmpty() ? finalFile : juce::File(), problem);
                });
            }).detach();
        };
    }

    te::Renderer::Parameters p (*job->edit);
    p.destFile = dest;
    p.audioFormat = o.flac ? engine.getAudioFileFormatManager().getFlacFormat() : engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = o.flac ? juce::jmin (24, o.bitDepth) : o.bitDepth;
    p.sampleRateForAudio = o.sampleRate;
    p.time = o.range ? *o.range : te::TimeRange { 0_tp, job->edit->getLength() };
    if (o.tail) p.endAllowance = te::TimeDuration::fromSeconds (2.0);
    auto tracks = te::getAudioTracks (*job->edit);
    if (o.trackIndex >= 0 && o.trackIndex < tracks.size())
    {
        // (this is the export's own copy of the song, so routing can change here)
        auto* t = tracks[o.trackIndex];
        juce::Array<te::Track*> render { t };
        if (groups::isGroup (*t))
        {
            // a group's stem: the group with everything playing into it (groups of groups too)
            for (int i = 0; i < render.size(); ++i)
                if (auto* at = dynamic_cast<te::AudioTrack*> (render[i]))
                    for (auto* m : groups::members (*at)) render.addIfNotAlreadyThere (m);
        }
        else
            groups::route (*t, nullptr);   // a track's stem: just that track, straight out
        p.tracksToDo = te::toBitSet (render);
    }
    else
        p.tracksToDo = te::toBitSet (te::getAllTracks (*job->edit));
    p.usePlugins = true;
    p.useMasterPlugins = o.masterEffects && o.trackIndex < 0;
    p.canRenderInMono = false;
    p.ditheringEnabled = p.bitDepth < 32;
    if (o.loudness) { p.shouldNormaliseByLUFS = true; p.normaliseToLevelDb = -14.0f; }
    dest.deleteFile();
    std::weak_ptr<SongExport> weak = job;
    auto start = [weak, p, done]
    {
        auto j = weak.lock();
        if (j == nullptr) return;
        j->handle = te::EditRenderer::render (p, [weak, done] (tl::expected<juce::File, std::string> res)
        {
            juce::MessageManager::callAsync ([weak, done, res]
            {
                done (res ? *res : juce::File(), res ? juce::String() : juce::String (res.error()));
                if (auto j = weak.lock()) { j->handle = nullptr; }
            });
        });
    };
    start();
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

class StudioUIBehaviour : public ExtendedUIBehaviour
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
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Encore Studio", message);
    }
};


#include "Account.h"
#include "Hub.h"
#include "AccountUI.h"
#include "Support.h"
#include "Versions.h"

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
            auto chooser = std::make_shared<juce::FileChooser> ("Where should Encore Studio keep your songs?", juce::File (props.getValue ("projectsFolder")));
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
            auto chooser = std::make_shared<juce::FileChooser> ("A folder with plugins in it (VST3 or VST2)", juce::File());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser&)
            {
                if (chooser->getResult() == juce::File()) return;
                auto folders = juce::StringArray::fromLines (props.getValue ("pluginFolders"));
                folders.removeEmptyStrings();
                folders.addIfNotAlreadyThere (chooser->getResult().getFullPathName());
                props.setValue ("pluginFolders", folders.joinIntoString ("\n"));
                update();
                if (StudioHooks::findPlugins) StudioHooks::findPlugins();
            });
        };
        clearPluginFolders.onClick = [this] { props.setValue ("pluginFolders", juce::String()); update(); };
        hubui::styleLabel (offsetL);
        addAndMakeVisible (offsetL);
        offset.setRange (-100.0, 100.0, 0.5);
        offset.setValue (props.getDoubleValue ("recordOffsetMs", 0.0), juce::dontSendNotification);
        offset.setTextValueSuffix (" ms");
        offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        offset.setTooltip ("If recordings land a little late or early against what you played along to, move them by this much. "
                           "Encore Studio already allows for what your interface reports, so most people leave this at 0.");
        offset.onValueChange = [this] { props.setValue ("recordOffsetMs", offset.getValue()); if (StudioHooks::applyRecordOffset) StudioHooks::applyRecordOffset(); };
        addAndMakeVisible (offset);
        for (auto* b : { &changeFolder, &clearRecent, &addPluginFolder, &clearPluginFolders }) addAndMakeVisible (b);
        hubui::styleLabel (pluginsL);
        hubui::styleLabel (pluginFolders, 12.0f, theme::paper);
        pluginFolders.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (pluginsL);
        addAndMakeVisible (pluginFolders);
        update();
        setSize (660, 390);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::surface); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (24);
        auto row = [&r] { auto x = r.removeFromTop (34); r.removeFromTop (16); return x; };
        auto a = row(); startL.setBounds (a.removeFromLeft (150)); startWith.setBounds (a);
        auto b = row(); folderL.setBounds (b.removeFromLeft (150)); changeFolder.setBounds (b.removeFromRight (100)); folderPath.setBounds (b.withTrimmedRight (8));
        auto c = row(); recentL.setBounds (c.removeFromLeft (150)); clearRecent.setBounds (c.removeFromLeft (180));
        auto o = row(); offsetL.setBounds (o.removeFromLeft (150)); offset.setBounds (o.removeFromLeft (320));
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
    juce::Label startL { {}, "When Encore Studio opens" }, folderL { {}, "Songs folder" }, folderPath, recentL { {}, "Recent list" },
                pluginsL { {}, "Plugin folders" }, pluginFolders, offsetL { {}, "Recording offset" };
    juce::Slider offset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    void update()
    {
        folderPath.setText (props.getValue ("projectsFolder", "(not chosen yet)"), juce::dontSendNotification);
        clearRecent.setButtonText ("Clear recent songs (" + juce::String (recent::load (props).size()) + ")");
        auto added = juce::StringArray::fromLines (props.getValue ("pluginFolders"));
        added.removeEmptyStrings();
        pluginFolders.setText ("Always searched: the standard VST3 and VST2 folders. "
                               + (added.isEmpty() ? juce::String ("Add any other folder you keep plugins in.") : "Also: " + added.joinIntoString ("; ")),
                               juce::dontSendNotification);
    }
};

/** File > Export > Audio Mixdown: the mix or every track (stems), whole song or loop, format and level,
    and optionally mastered on the way out with Encore Master (with a quick before/after listen) */
class ExportComponent : public juce::Component
{
public:
    std::function<void (ExportOptions, bool stems, juce::File folder, juce::String baseName)> onExport;
    // the before/after preview (MainComponent renders and plays it)
    std::function<void (ExportOptions)> onListen;
    std::function<void()> onStopListening;
    std::function<void (bool original)> onHearOriginal;
    std::function<void (bool same)> onSameLoudness;

    ExportComponent (const juce::String& songName, const juce::File& defaultFolder, double songRate, bool hasLoop, int lastStyle = -1)
        : folder (defaultFolder)
    {
        for (auto* l : { &whatL, &rangeL, &formatL, &rateL, &whereL, &nameL, &masterL, &intensityL }) { hubui::styleLabel (*l); addAndMakeVisible (l); }
        what.addItemList ({ "The whole mix", "Each track as its own file (stems)" }, 1);
        range.addItemList ({ "Whole song", "Loop range" }, 1);
        range.setItemEnabled (2, hasLoop);
        format.addItemList ({ "WAV 24-bit", "WAV 16-bit (CD)", "WAV 32-bit float", "FLAC 24-bit", "MP3 320 kbps", "MP3 192 kbps (smaller)" }, 1);
        rate.addItem ("Same as the song (" + juce::String (songRate / 1000.0, 1) + " kHz)", 1);
        rate.addItemList ({ "44.1 kHz", "48 kHz", "96 kHz" }, 2);
        style.addItem ("No mastering", 1);
        style.addItemList (mastering::styleNames(), 2);
        target.addItemList (mastering::targetNames(), 1);
        for (auto* c : { &what, &range, &format, &rate, &style, &target }) { c->setSelectedId (1, juce::dontSendNotification); hubui::styleCombo (*c); addAndMakeVisible (c); }
        style.setSelectedId (lastStyle >= 0 ? lastStyle + 2 : 1, juce::dontSendNotification);
        what.onChange = [this] { updateEnabled(); };
        style.onChange = [this] { updateEnabled(); relisten(); };
        target.onChange = [this] { relisten(); };

        intensity.setRange (0.0, 1.0, 0.01);
        intensity.setValue (0.5, juce::dontSendNotification);
        intensity.setSliderStyle (juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        intensity.setColour (juce::Slider::thumbColourId, theme::paper);
        intensity.setColour (juce::Slider::trackColourId, theme::accent);
        intensity.setDoubleClickReturnValue (true, 0.5);
        intensity.onDragEnd = [this] { relisten(); };
        intensity.onValueChange = [this] { updateBlurb(); };
        addAndMakeVisible (intensity);

        hubui::styleLabel (blurb, 12.0f, theme::muted);
        addAndMakeVisible (blurb);
        listen.onClick = [this]
        {
            if (listening) { stopListening(); return; }
            listening = true;
            listen.setButtonText ("Stop");
            if (onListen) onListen (options());
        };
        addAndMakeVisible (listen);
        for (auto* b : { &original, &same, &master, &tail, &loud })
        {
            b->setColour (juce::ToggleButton::textColourId, theme::paper);
            b->setColour (juce::ToggleButton::tickColourId, theme::accent);
            addAndMakeVisible (b);
        }
        original.onClick = [this] { if (onHearOriginal) onHearOriginal (original.getToggleState()); };
        same.onClick = [this] { if (onSameLoudness) onSameLoudness (same.getToggleState()); };

        hubui::styleEditor (name);
        name.setText (songName, false);
        addAndMakeVisible (name);
        hubui::styleLabel (path, 12.0f, theme::paper);
        path.setText (folder.getFullPathName(), juce::dontSendNotification);
        addAndMakeVisible (path);
        change.onClick = [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Export to", folder);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser&)
            {
                if (chooser->getResult() == juce::File()) return;
                folder = chooser->getResult();
                path.setText (folder.getFullPathName(), juce::dontSendNotification);
            });
        };
        addAndMakeVisible (change);
        master.setToggleState (true, juce::dontSendNotification);
        tail.setToggleState (true, juce::dontSendNotification);
        go.setColour (juce::TextButton::buttonColourId, theme::brand);
        go.onClick = [this] { start(); };
        cancel.onClick = [this] { close(); };
        addAndMakeVisible (go);
        addAndMakeVisible (cancel);
        songSampleRate = songRate;
        updateEnabled();
        setSize (600, 600);
    }

    ~ExportComponent() override { if (listening && onStopListening) onStopListening(); }

    /** What the preview is doing ("Rendering...", "Mastered is 3.2 dB louder") */
    void setPreviewText (const juce::String& s) { blurb.setText (s, juce::dontSendNotification); previewNote = s.isNotEmpty(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::surface);
        // the mastering block
        g.setColour (theme::subtle);
        g.fillRoundedRectangle (masterArea.toFloat(), 8.0f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 20);
        auto row = [&r] (juce::Label& l, int h = 30) { auto x = r.removeFromTop (h); r.removeFromTop (10); l.setBounds (x.removeFromLeft (120)); return x; };
        what.setBounds (row (whatL));
        range.setBounds (row (rangeL).removeFromLeft (220));
        format.setBounds (row (formatL).removeFromLeft (220));
        rate.setBounds (row (rateL).removeFromLeft (260));
        name.setBounds (row (nameL));
        auto w = row (whereL);
        change.setBounds (w.removeFromRight (90));
        path.setBounds (w.withTrimmedRight (8));

        masterArea = r.removeFromTop (132).expanded (8, 0);
        auto m = masterArea.reduced (8, 10);
        auto m1 = m.removeFromTop (30);
        masterL.setBounds (m1.removeFromLeft (112));
        style.setBounds (m1.removeFromLeft (170));
        m1.removeFromLeft (8);
        target.setBounds (m1.removeFromLeft (180));
        m.removeFromTop (8);
        auto m2 = m.removeFromTop (30);
        intensityL.setBounds (m2.removeFromLeft (112));
        listen.setBounds (m2.removeFromRight (90));
        m2.removeFromRight (10);
        intensity.setBounds (m2);
        m.removeFromTop (6);
        auto m3 = m.removeFromTop (26);
        m3.removeFromLeft (112);
        original.setBounds (m3.removeFromLeft (150));
        same.setBounds (m3.removeFromLeft (170));
        blurb.setBounds (m.withTrimmedLeft (112));
        r.removeFromTop (14);

        master.setBounds (r.removeFromTop (26).withTrimmedLeft (120));
        tail.setBounds (r.removeFromTop (26).withTrimmedLeft (120));
        loud.setBounds (r.removeFromTop (26).withTrimmedLeft (120));
        auto b = r.removeFromBottom (36);
        go.setBounds (b.removeFromRight (120));
        b.removeFromRight (8);
        cancel.setBounds (b.removeFromRight (90));
    }

private:
    juce::File folder;
    double songSampleRate = 48000.0;
    juce::ComboBox what, range, format, rate, style, target;
    juce::Slider intensity;
    juce::TextEditor name;
    juce::Label whatL { {}, "Export" }, rangeL { {}, "Range" }, formatL { {}, "Format" }, rateL { {}, "Sample rate" },
                whereL { {}, "Folder" }, nameL { {}, "File name" }, masterL { {}, "Master it" }, intensityL { {}, "How hard" }, path, blurb;
    juce::TextButton change { "Change..." }, go { "Export" }, cancel { "Cancel" }, listen { "Listen" };
    juce::ToggleButton master { "Include the master effects (your own, on the master track)" },
                       tail { "Let reverbs and delays ring out at the end" },
                       loud { "Make it streaming-loud (normalise to -14 LUFS)" },
                       original { "Hear the original" }, same { "Same loudness (fair A/B)" };
    juce::Rectangle<int> masterArea;
    bool listening = false, previewNote = false;

    bool mixOnly() const { return what.getSelectedId() == 1; }
    bool mastered() const { return mixOnly() && style.getSelectedId() > 1; }

    void updateEnabled()
    {
        style.setEnabled (mixOnly());
        for (juce::Component* c : { (juce::Component*) &target, (juce::Component*) &intensity, (juce::Component*) &listen,
                                    (juce::Component*) &original, (juce::Component*) &same })
            c->setEnabled (mastered());
        master.setEnabled (mixOnly());
        loud.setEnabled (mixOnly() && ! mastered());   // mastering sets its own loudness
        if (! mastered() && listening) stopListening();
        updateBlurb();
    }

    void updateBlurb()
    {
        if (previewNote) return;
        if (! mixOnly()) blurb.setText ("Mastering is for the whole mix; stems go out as they are.", juce::dontSendNotification);
        else if (! mastered()) blurb.setText ("Pick a style to run the mix through Encore Master as it exports. Your song doesn't change.", juce::dontSendNotification);
        else
        {
            const auto v = intensity.getValue();
            blurb.setText (mastering::styleBlurbs()[style.getSelectedId() - 2] + (v < 0.3 ? ", gently." : v > 0.7 ? ", pushed hard." : "."),
                           juce::dontSendNotification);
        }
    }

    void relisten() { if (listening && mastered() && onListen) { original.setToggleState (false, juce::dontSendNotification); onListen (options()); } }

    void stopListening()
    {
        listening = false;
        listen.setButtonText ("Listen");
        previewNote = false;
        updateBlurb();
        if (onStopListening) onStopListening();
    }

    void close() { if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (0); }

    ExportOptions options() const
    {
        ExportOptions o;
        const int f = format.getSelectedId();
        o.bitDepth = f == 2 ? 16 : f == 3 ? 32 : 24;
        o.flac = f == 4;
        o.mp3Kbps = f == 5 ? 320 : f == 6 ? 192 : 0;
        const double rates[] = { songSampleRate, 44100.0, 48000.0, 96000.0 };
        o.sampleRate = rates[juce::jlimit (0, 3, rate.getSelectedId() - 1)];
        o.masterEffects = master.getToggleState() || ! mixOnly();
        o.tail = tail.getToggleState();
        if (mastered())
        {
            o.mastering.style = style.getSelectedId() - 2;
            o.mastering.target = target.getSelectedId() - 1;
            o.mastering.intensity = (float) intensity.getValue();
        }
        o.loudness = loud.getToggleState() && mixOnly() && ! mastered();
        if (range.getSelectedId() == 2) o.range = te::TimeRange(); // filled in by the caller from the loop
        return o;
    }

    void start()
    {
        if (listening) stopListening();
        auto o = options();
        auto base = juce::File::createLegalFileName (name.getText().trim().isEmpty() ? juce::String ("Mix") : name.getText().trim());
        if (onExport) onExport (o, what.getSelectedId() == 2, folder, base);
        close();
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

/** File > Send to Encore: a release package for Encore Music (the mastered mix, stems if you
    want them, the cover art and the song's details in release.json), made in the song's
    folder, then Encore opens in the browser to upload it. Signing in and uploading straight
    from Studio come once Encore's upload API exists (see docs/ENCORE-API.md). */
class EncoreSendComponent : public juce::Component
{
public:
    struct Choice { bool loud = true, masterEffects = true, stems = false; juce::File cover; };
    std::function<void (Choice)> onSend;

    EncoreSendComponent (const SongInfo& info, const juce::File& songFolder) : folder (songFolder)
    {
        hubui::styleLabel (heading, 15.0f, theme::paper);
        heading.setText ((info.title.isNotEmpty() ? info.title : juce::String ("Untitled")) + (info.artist.isNotEmpty() ? "  by  " + info.artist : juce::String()),
                         juce::dontSendNotification);
        addAndMakeVisible (heading);
        hubui::styleLabel (details, 12.5f, theme::text2);
        juce::StringArray d;
        if (info.album.isNotEmpty()) d.add (info.album);
        if (info.genre.isNotEmpty()) d.add (info.genre);
        d.add (info.bpmText() + " BPM");
        if (info.key.isNotEmpty()) d.add (info.key);
        details.setText (d.joinIntoString ("  ·  ") + (info.artist.isEmpty() ? "\nNo artist yet: add it in Project > Song Setup first" : juce::String()), juce::dontSendNotification);
        addAndMakeVisible (details);

        for (auto* t : { &loud, &master, &stems })
        {
            t->setColour (juce::ToggleButton::textColourId, theme::paper);
            addAndMakeVisible (t);
        }
        loud.setToggleState (true, juce::dontSendNotification);
        master.setToggleState (true, juce::dontSendNotification);

        hubui::styleLabel (coverL);
        addAndMakeVisible (coverL);
        hubui::styleLabel (coverPath, 12.0f, theme::paper);
        coverPath.setText ("None yet (Encore can add one later)", juce::dontSendNotification);
        addAndMakeVisible (coverPath);
        pickCover.onClick = [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Cover art (square, 3000 x 3000 is ideal)", folder, "*.jpg;*.jpeg;*.png");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser&)
            {
                auto f = chooser->getResult();
                if (! f.existsAsFile()) return;
                cover = f;
                coverPath.setText (f.getFileName(), juce::dontSendNotification);
            });
        };
        addAndMakeVisible (pickCover);

        hubui::styleLabel (note, 12.0f, theme::muted);
        note.setText ("The package goes in the song's folder, under Encore. Then Encore opens in your browser: upload the files from there. "
                      "Signing in from Encore Studio and uploading in one click come next.", juce::dontSendNotification);
        addAndMakeVisible (note);

        send.setColour (juce::TextButton::buttonColourId, theme::brand);
        send.onClick = [this]
        {
            if (onSend) onSend ({ loud.getToggleState(), master.getToggleState(), stems.getToggleState(), cover });
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (1);
        };
        cancel.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };
        addAndMakeVisible (send);
        addAndMakeVisible (cancel);
        setSize (560, 400);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 20);
        heading.setBounds (r.removeFromTop (24));
        details.setBounds (r.removeFromTop (38));
        r.removeFromTop (8);
        loud.setBounds (r.removeFromTop (26));
        master.setBounds (r.removeFromTop (26));
        stems.setBounds (r.removeFromTop (26));
        r.removeFromTop (10);
        auto c = r.removeFromTop (30);
        coverL.setBounds (c.removeFromLeft (90));
        pickCover.setBounds (c.removeFromRight (110));
        c.removeFromRight (8);
        coverPath.setBounds (c);
        r.removeFromTop (10);
        auto b = r.removeFromBottom (36);
        send.setBounds (b.removeFromRight (170));
        b.removeFromRight (8);
        cancel.setBounds (b.removeFromRight (90));
        note.setBounds (r);
    }

private:
    juce::File folder, cover;
    juce::Label heading, details, coverL { {}, "Cover art" }, coverPath, note;
    juce::ToggleButton loud { "Streaming level (-14 LUFS)" }, master { "With the effects on the master (Encore Master)" },
                       stems { "Include each track as a stem (for remixes and live mixes)" };
    juce::TextButton pickCover { "Choose..." }, send { "Make the package" }, cancel { "Cancel" };
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
    if (auto* w = o.launchAsync()) darkTitleBar (*w);
}

//==============================================================================
/** The bar above the piano roll: drag it to give the piano roll more or less room */
class DividerBar : public juce::Component
{
public:
    std::function<void (int deltaY, bool finished)> onDrag;
    DividerBar() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::base);
        g.setColour (theme::strong);
        g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (40.0f, 3.0f), 1.5f);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY(), false); }
    void mouseUp (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY(), true); }
};

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
        cmdHub = 0x2000, cmdNew, cmdOpen, cmdClose, cmdSave, cmdSaveAs, cmdSaveVersion, cmdVersions, cmdSaveTemplate, cmdRevert,
        cmdImportAudio, cmdImportMidi, cmdImportLive, cmdExportMix, cmdExportZip, cmdPreferences, cmdQuit,
        // Edit
        cmdUndo, cmdRedo, cmdCut, cmdCopy, cmdPaste, cmdDuplicate, cmdDelete, cmdSplit, cmdSelectAll, cmdSelectNone, cmdMuteClip,
        cmdNudgeLeft, cmdNudgeRight, cmdSnapOff, cmdSnapBar, cmdSnapBeat, cmdSnapEighth, cmdSnapSixteenth, cmdRenameClip,
        // Project
        cmdAddTrack, cmdAddDrumTrack, cmdDuplicateTrack, cmdRenameTrack, cmdRemoveTrack, cmdSongSetup,
        // Audio
        cmdNormalize, cmdGainUp, cmdGainDown, cmdGainReset, cmdFadeIn, cmdFadeOut, cmdRemoveFades, cmdReverse,
        // Media
        cmdShowSongFolder, cmdShowRecordings, cmdShowAudio, cmdChangeProjectsFolder,
        cmdAddMarker, cmdPrevMarker, cmdNextMarker,
        // Transport
        cmdPlay, cmdRecord, cmdToStart, cmdToEnd, cmdBarBack, cmdBarForward, cmdLoop, cmdLoopToSelection, cmdClick,
        cmdCountNone, cmdCount1Beat, cmdCount2Beats, cmdCount1Bar, cmdCount2Bars, cmdMetronome, cmdPunch,
        // Studio
        cmdAudioSetup, cmdPlugins, cmdFindPlugins, cmdReassignInputs,
        // Window
        cmdEffects, cmdZoomIn, cmdZoomOut, cmdZoomFit, cmdHeightSmall, cmdHeightMedium, cmdHeightLarge, cmdHeightHuge,
        cmdMixer, cmdTuner, cmdWaveBigger, cmdWaveSmaller, cmdAddFxReverb, cmdAddFxDelay, cmdAddFxEmpty, cmdAddGroup, cmdGroupSelected, cmdSendToEncore, cmdPianoRoll, cmdQuantize, cmdTransposeUp, cmdTransposeDown, cmdOctaveUp, cmdOctaveDown, cmdVelocityLouder, cmdVelocitySofter, cmdAutomationOff, cmdAutomationVolume, cmdAutomationPan, cmdAutomationCycle,
        // Help
        cmdShortcuts, cmdGuide, cmdReport, cmdUpdates, cmdAutoUpdates, cmdLogFolder, cmdAbout
    };

    MainComponent()
    {
        studioLog ("startup: engine ready in " + juce::String (juce::Time::getMillisecondCounterHiRes() - constructionStarted, 0) + " ms");
        registerEncorePlugins (engine);
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
        hub->onSignIn = [this] { showFirstScreen(); };
        hub->onAddArtist = [this] { addArtist(); };
        hub->onAccountChanged = [this] { accountChanged(); };
        hub->onAccountMenu = [this]
        {
            accountMenu().showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                                         [safe = juce::Component::SafePointer<MainComponent> (this)] (int r) { if (safe && r > 0) safe->accountItem (r); });
        };
        addChildComponent (*hub);

        for (auto* b : { &homeButton, &playButton, &recordButton, &loopButton, &clickButton, &addTrackButton, &importButton, &exportButton })
        {
            addChildComponent (b);
            b->setMouseClickGrabsKeyboardFocus (false);
        }
        for (auto* l : { &timeLabel, &barLabel, &tempoLabel, &tempoCaption, &statusLabel })
            addChildComponent (l);
        addChildComponent (masterMeter);
        addChildComponent (mixerButton);
        mixerButton.setTooltip ("Switch between the tracks and the mixer (F3)");
        mixerButton.setMouseClickGrabsKeyboardFocus (false);
        mixerButton.onClick = [this] { invoke (cmdMixer); };
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

        StudioHooks::commands = &commandManager;
        StudioHooks::fillClipMenu = [this] (juce::PopupMenu& m, te::Clip& c) { fillClipMenu (m, c); };
        StudioHooks::forwardKey = [this] (const juce::KeyPress& k) { return keyPressed (k, nullptr); };
        StudioHooks::openPianoRoll = [this] (te::MidiClip& c) { openPianoRoll (c); };
        addChildComponent (pianoDivider);
        pianoDivider.onDrag = [this] (int dy, bool finished)
        {
            if (! dragStartHeight) dragStartHeight = pianoRollH;
            pianoRollH = juce::jmax (160, *dragStartHeight - dy);
            if (finished) { props().setValue ("pianoRollHeight", pianoRollH); dragStartHeight.reset(); }
            resized();
        };
        StudioHooks::findPlugins = [this] { startPluginSearch(); };
        StudioHooks::applyRecordOffset = [this] { setupInputs(); };
        StudioHooks::showPluginManager = [this] { showPluginList(); };
        StudioHooks::pluginScanStatus = [this] { return pluginFinder.status(); };
        pluginFinder.onProgress = [this] (juce::String s) { statusSoft (s); };
        pluginFinder.onFinished = [this] (int found)
        {
            statusSoft (found > 0 ? "Found " + juce::String (found) + (found == 1 ? " new plugin" : " new plugins") + ": they're in every track's Effects menu"
                                  : juce::String());
            commandManager.commandStatusChanged();
        };
        // a newer Studio? (once a day, a few seconds in)
        juce::Timer::callAfterDelay (5000, [safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->checkForUpdates (false); });
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
        if (crashedLastSession)
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safe) safe->warn ("Encore Studio didn't close properly last time. Help > Report a Problem sends us what happened.");
            });
        auto last = juce::File (props().getValue ("lastSong"));
        if (last.existsAsFile() && recent::load (props()).isEmpty()) // songs made before the Hub existed
            if (auto e = songs::open (engine, last, te::Edit::forExamining)) recent::remember (props(), *e, last, last.getLastModificationTime());
        const bool openLast = props().getValue ("startWith", "hub") == "last" && last.existsAsFile();
        if (! openLast)
        {
            showHub (true);
            hub->showTab (hub->recentCount() > 0 ? 0 : 1);
        }
        if (account::neverAsked()) showFirstScreen();   // the very first screen: who you're making music as
        // the window shows first; then the audio interface opens, then the last song (if that's the setting)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), openLast, last]
        {
            if (safe == nullptr) return;
            const auto t = juce::Time::getMillisecondCounterHiRes();
            juce::String wanted;
            if (auto xml = safe->engine.getPropertyStorage().getXmlProperty (te::SettingID::audio_device_setup))
                wanted = xml->getStringAttribute ("audioInputDeviceName", xml->getStringAttribute ("audioOutputDeviceName"));
            safe->engine.getDeviceManager().initialise();
            auto* d = safe->engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
            studioLog ("startup: audio interface opened in " + juce::String (juce::Time::getMillisecondCounterHiRes() - t, 0) + " ms: "
                         + (d ? d->getName() : juce::String ("none")) + (wanted.isNotEmpty() ? " (saved: " + wanted + ")" : juce::String()));
            // the interface from last time didn't open (unplugged, or another app has it)
            auto& dm = safe->engine.getDeviceManager().deviceManager;
            auto setup = dm.getAudioDeviceSetup();
            if (wanted.isNotEmpty() && setup.inputDeviceName != wanted && setup.outputDeviceName != wanted && (d == nullptr || d->getName() != wanted))
            {
                // never sit on a generic ASIO wrapper by accident (ASIO4ALL etc. show "Not Connected" inputs): use Windows audio instead
                if (d != nullptr && dm.getCurrentAudioDeviceType() == "ASIO" && isGenericAsio (d->getName()))
                {
                    dm.setCurrentAudioDeviceType ("Windows Audio", true);
                    d = dm.getCurrentAudioDevice();
                    studioLog ("startup: " + wanted + " didn't open, so not using a generic ASIO driver; now on " + (d ? d->getName() : juce::String ("none")));
                }
                safe->warn ("Couldn't open " + wanted + " (unplugged, or another app like Cubase is using it?). Using "
                              + (d ? d->getName() : juce::String ("no interface")) + " for now. Studio > Audio Setup to change.");
            }
            safe->setupInputs();
            if (openLast) safe->loadSong (last);
            if (safe->hub) safe->hub->repaint();
        });
    }

    ~MainComponent() override
    {
        stopTimer();
        stopMasterPreview();
        if (autosaveWriter.joinable()) autosaveWriter.join();
        StudioHooks::findPlugins = nullptr;
        StudioHooks::applyRecordOffset = nullptr;
        StudioHooks::showPluginManager = nullptr;
        StudioHooks::pluginScanStatus = nullptr;
        StudioHooks::commands = nullptr;
        StudioHooks::fillClipMenu = nullptr;
        StudioHooks::forwardKey = nullptr;
        StudioHooks::openPianoRoll = nullptr;
        pianoRoll = nullptr;
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
        g.drawText ("STUDIO", mark.withTrimmedTop (mark.getHeight() / 2 - 2), juce::Justification::topLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        menuBar.setBounds (r.removeFromTop (menuH));
        if (firstScreen) firstScreen->setBounds (r);
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
        place (clickButton, 56, 6);
        place (snapBox, 104, 6);
        place (mixerButton, 66);
        mixerButton.setToggleState (mixerShown, juce::dontSendNotification);
        // narrow windows drop the extras first (they're all in the menus too)
        exportButton.setBounds (t.removeFromRight (110)); t.removeFromRight (10);
        masterMeter.setBounds (t.removeFromRight (100).withSizeKeepingCentre (100, 14)); t.removeFromRight (10);
        const bool roomy = t.getWidth() >= 80 + 78 + 16;
        importButton.setVisible (roomy && ! hubShown);
        addTrackButton.setVisible (roomy && ! hubShown);
        if (roomy)
        {
            importButton.setBounds (t.removeFromRight (78)); t.removeFromRight (6);
            addTrackButton.setBounds (t.removeFromRight (80)); t.removeFromRight (10);
        }

        // the piano roll takes the bottom of the window, under a bar you drag to resize it
        const bool showPiano = pianoRoll != nullptr && ! mixerShown;
        if (pianoRoll) pianoRoll->setVisible (showPiano);
        pianoDivider.setVisible (showPiano);
        auto main = r;
        if (showPiano)
        {
            pianoRoll->setBounds (main.removeFromBottom (juce::jlimit (160, juce::jmax (160, main.getHeight() - 140), pianoRollH)));
            pianoDivider.setBounds (main.removeFromBottom (8));
        }
        if (editComponent) editComponent->setBounds (main);
        if (mixer) mixer->setBounds (r);
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

    /** Opens a song from outside (double-clicked in Explorer / Finder) */
    void openSongFile (const juce::File& f) { showHub (false); loadSong (f); }

    void updateTitle()
    {
        if (onTitleChange) onTitleChange (edit ? "Encore: Studio - " + songTitle() : juce::String ("Encore: Studio"));
    }

    //==============================================================================
    // Menus
    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Edit", "Project", "Audio", "MIDI", "Score", "Media", "Transport", "Studio", "Window", "Account", "Help" };
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
                add (cmdSave); add (cmdSaveAs); add (cmdSaveVersion); add (cmdVersions); add (cmdSaveTemplate); add (cmdRevert); m.addSeparator();
                juce::PopupMenu imp; addCommand (imp, cmdImportAudio); addCommand (imp, cmdImportMidi);
                m.addSubMenu ("Import", imp);
                juce::PopupMenu exp; addCommand (exp, cmdExportMix); addCommand (exp, cmdExportZip);
                exp.addSeparator(); addCommand (exp, cmdSendToEncore);
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
                add (cmdAddTrack); add (cmdAddDrumTrack); add (cmdDuplicateTrack); add (cmdRenameTrack); add (cmdRemoveTrack); m.addSeparator();
                add (cmdSongSetup); m.addSeparator();
                add (cmdAddMarker);
                m.addItem (tempoChangeId, "Tempo or Time Signature Change at the Playhead...", editComponent != nullptr && ! hubShown);
                m.addSeparator();
                add (cmdAddFxReverb); add (cmdAddFxDelay); add (cmdAddFxEmpty); m.addSeparator();
                add (cmdAddGroup); add (cmdGroupSelected); m.addSeparator();
                m.addSubMenu ("Add Instrument Track", instrumentMenu(), edit != nullptr && ! hubShown);
                later ({ "Arranger Track" });
                break;
            case 3: // Audio
                add (cmdNormalize); add (cmdGainUp); add (cmdGainDown); add (cmdGainReset); m.addSeparator();
                add (cmdFadeIn); add (cmdFadeOut); add (cmdRemoveFades); m.addSeparator();
                add (cmdReverse);
                later ({ "Time Stretch", "Pitch Shift", "Detect Tempo", "Bounce Selection" });
                break;
            case 4: // MIDI
            {
                add (cmdPianoRoll); m.addSeparator();
                m.addItem (drumSnapId, "Snap Recorded Drum Hits to the Grid", true, props().getBoolValue ("drumRecordSnap", true));
                add (cmdQuantize);
                const bool notes = ! midiTargets().empty();
                juce::PopupMenu tr; addCommand (tr, cmdTransposeUp); addCommand (tr, cmdTransposeDown); addCommand (tr, cmdOctaveUp); addCommand (tr, cmdOctaveDown);
                m.addSubMenu ("Transpose", tr, notes);
                juce::PopupMenu vel; addCommand (vel, cmdVelocityLouder); addCommand (vel, cmdVelocitySofter);
                for (int v : { 127, 100, 80, 64 })
                    vel.addItem ("Set to " + juce::String (v), notes, false, [this, v] { editNotes ("Note velocity", [v] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::velocity (c, n, v); }); });
                m.addSubMenu ("Velocity", vel, notes);
                m.addSeparator();
                m.addSubMenu ("Add Instrument Track", instrumentMenu(), edit != nullptr && ! hubShown);
                add (cmdImportMidi); add (cmdImportLive);
                later ({ "Drum Editor" });
                break;
            }
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
                add (cmdToStart); add (cmdToEnd); add (cmdBarBack); add (cmdBarForward); add (cmdPrevMarker); add (cmdNextMarker); m.addSeparator();
                add (cmdLoop); add (cmdLoopToSelection); add (cmdPunch); m.addSeparator();
                add (cmdClick); add (cmdMetronome);
                juce::PopupMenu count;
                for (auto id : { cmdCountNone, cmdCount1Beat, cmdCount2Beats, cmdCount1Bar, cmdCount2Bars }) addCommand (count, id);
                m.addSubMenu ("Count-in", count);
                break;
            }
            case 8: // Studio
                add (cmdAudioSetup); add (cmdPlugins); add (cmdFindPlugins); add (cmdReassignInputs); m.addSeparator();
                add (cmdPreferences);
                break;
            case 9: // Window
                add (cmdHub); add (cmdMixer); add (cmdTuner); m.addSeparator();
                add (cmdEffects);
                {
                    juce::PopupMenu automation;
                    for (auto id : { cmdAutomationOff, cmdAutomationVolume, cmdAutomationPan }) addCommand (automation, id);
                    automation.addSeparator();
                    automation.addItem (comingSoonId, "Click a track to add points, drag them, double-click to remove", false);
                    m.addSubMenu ("Automation (A)", automation, edit != nullptr && ! hubShown);
                }
                m.addSeparator();
                add (cmdZoomIn); add (cmdZoomOut); add (cmdZoomFit); add (cmdWaveBigger); add (cmdWaveSmaller); m.addSeparator();
                {
                    juce::PopupMenu heights;
                    for (auto id : { cmdHeightSmall, cmdHeightMedium, cmdHeightLarge, cmdHeightHuge }) addCommand (heights, id);
                    heights.addSeparator();
                    heights.addItem (comingSoonId, "Or drag a track's bottom edge (Alt: all tracks)", false);
                    m.addSubMenu ("Track Height", heights, edit != nullptr && ! hubShown);
                }
                later ({ "Clip Editor" });
                break;
            case 10: // Account
                m = accountMenu();
                break;
            case 11: // Help
                add (cmdShortcuts); add (cmdGuide); m.addSeparator();
                add (cmdReport); add (cmdUpdates); add (cmdAutoUpdates); add (cmdLogFolder); m.addSeparator();
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
        else if (c == KP::insertKey) key = "Insert";
        else if (c == KP::F3Key) key = "F3";
        else if (c == KP::upKey) key = "Up";
        else if (c == KP::downKey) key = "Down";
        else key = juce::String::charToString ((juce::juce_wchar) c).toUpperCase();
        parts.add (key);
        return parts.joinIntoString ("+");
    }

    void menuItemSelected (int id, int) override
    {
        if (id >= recentBaseId && id < recentBaseId + recentFiles.size())
            if (recentFiles[id - recentBaseId].existsAsFile()) loadSong (recentFiles[id - recentBaseId]);
        if (id >= accountBaseId && id <= accountLastId) accountItem (id);
        if (id == drumSnapId) props().setValue ("drumRecordSnap", ! props().getBoolValue ("drumRecordSnap", true));
        if (id == tempoChangeId && editComponent != nullptr && edit) editComponent->getRuler().editTempoChange (edit->getTransport().getPosition());
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
            case cmdSaveVersion:    set ("Save New Version...", "File", song); key ('s', cmd | alt); break;
            case cmdVersions:       set ("Versions...", "File", song); key ('v', cmd | alt); break;
            case cmdSaveTemplate:   set ("Save as Template...", "File", song); break;
            case cmdRevert:         set ("Revert", "File", song); break;
            case cmdImportAudio:    set ("Audio File...", "File", song); key ('i', cmd); break;
            case cmdImportMidi:     set ("MIDI File...", "File", songShown); break;
            case cmdImportLive:     set ("Live Recording (Encore Live or multitrack WAV)...", "File"); break;
            case cmdExportMix:      set ("Audio Mixdown or Stems...", "File", song && ! currentExport); key ('e', cmd); break;
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
            case cmdAddDrumTrack:   set ("Add Drum Track", "Project", songShown); key ('t', cmd | alt); break;
            case cmdDuplicateTrack: set ("Duplicate Track", "Project", songShown && selectedTrack() != nullptr); key ('d', cmd | shift); break;
            case cmdRenameTrack:    set ("Rename Track...", "Project", songShown && selectedTrack() != nullptr); key (KP::F2Key, none); break;
            case cmdRemoveTrack:    set ("Remove Selected Track", "Project", songShown && selectedTrack() != nullptr); break;
            case cmdSongSetup:      set ("Song Setup...", "Project", song); key ('s', shift); break;
            case cmdAddMarker:      set ("Add Marker at Playhead", "Project", songShown); key (KP::insertKey, none); break;
            case cmdPrevMarker:     set ("Previous Marker", "Transport", songShown); key (KP::leftKey, shift); break;
            case cmdNextMarker:     set ("Next Marker", "Transport", songShown); key (KP::rightKey, shift); break;

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
            case cmdPunch:          set ("Punch In/Out", "Transport", songShown); key ('i', none); info.setTicked (song && edit->recordingPunchInOut.get()); break;

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
            case cmdMixer:          set ("Mixer", "Window", songShown); key (KP::F3Key, none); info.setTicked (mixerShown); break;
            case cmdTuner:          set ("Tuner", "Window"); key ('t', none); break;
            case cmdWaveBigger:     set ("Taller Waveforms", "Window", songShown); key (KP::upKey, cmd | shift); break;
            case cmdWaveSmaller:    set ("Shorter Waveforms", "Window", songShown && ! waveZoomIs (1.0f)); key (KP::downKey, cmd | shift); break;
            case cmdAutomationOff:  set ("Off", "Window", songShown); info.setTicked (automationIs (0)); break;
            case cmdAutomationVolume: set ("Volume", "Window", songShown); info.setTicked (automationIs (1)); break;
            case cmdAutomationPan:  set ("Pan", "Window", songShown); info.setTicked (automationIs (2)); break;
            case cmdAutomationCycle: set ("Show Automation (cycle)", "Window", songShown); key ('a', none); break;
            case cmdAddFxReverb:    set ("Add FX Track: Reverb", "Project", songShown); break;
            case cmdAddFxDelay:     set ("Add FX Track: Delay", "Project", songShown); break;
            case cmdAddFxEmpty:     set ("Add FX Track: Empty", "Project", songShown); break;
            case cmdAddGroup:       set ("Add Group Track", "Project", songShown); break;
            case cmdSendToEncore:   set ("Send to Encore...", "File", song && ! currentExport); break;
            case cmdPianoRoll:      set (pianoRoll ? "Close Piano Roll" : "Open Piano Roll", "MIDI", songShown && (pianoRoll != nullptr || selectedMidiClip() != nullptr)); key ('e', cmd | shift); break;
            case cmdQuantize:       set ("Quantize Notes", "MIDI", songShown && ! midiTargets().empty()); key ('q', none); break;
            case cmdTransposeUp:    set ("Up a Semitone", "MIDI", songShown && ! midiTargets().empty()); break;
            case cmdTransposeDown:  set ("Down a Semitone", "MIDI", songShown && ! midiTargets().empty()); break;
            case cmdOctaveUp:       set ("Up an Octave", "MIDI", songShown && ! midiTargets().empty()); break;
            case cmdOctaveDown:     set ("Down an Octave", "MIDI", songShown && ! midiTargets().empty()); break;
            case cmdVelocityLouder: set ("Louder (+10)", "MIDI", songShown && ! midiTargets().empty()); break;
            case cmdVelocitySofter: set ("Softer (-10)", "MIDI", songShown && ! midiTargets().empty()); break;
            case cmdGroupSelected:  set ("Group Selected Tracks", "Project", songShown && ! selectedTracks().isEmpty()); key ('g', cmd); break;

            case cmdShortcuts:      set ("Keyboard Shortcuts...", "Help"); break;
            case cmdGuide:          set ("User Guide", "Help"); break;
            case cmdReport:         set ("Report a Problem...", "Help"); break;
            case cmdUpdates:        set ("Check for Updates...", "Help"); break;
            case cmdAutoUpdates:    set ("Check for Updates Once a Day", "Help"); info.setTicked (props().getBoolValue ("checkUpdates", true)); break;
            case cmdLogFolder:      set ("Open Log Folder", "Help"); break;
            case cmdAbout:          set ("About Encore: Studio", "Help"); break;
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
            case cmdVersions:       showVersions(); return true;
            case cmdSaveTemplate:   saveTemplate(); return true;
            case cmdRevert:         revert(); return true;
            case cmdImportAudio:    browseForImport(); return true;
            case cmdImportMidi:     importMidi(); return true;
            case cmdImportLive:     importLiveRecording(); return true;
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
            case cmdAddDrumTrack:   addDrumTrack(); return true;
            case cmdDuplicateTrack: duplicateTrack(); return true;
            case cmdRenameTrack:    renameTrack(); return true;
            case cmdRemoveTrack:    if (auto t = selectedTrack()) { edit->deleteTrack (t); selectionManager.deselectAll(); } return true;
            case cmdSongSetup:      songSetup(); return true;
            case cmdAddMarker:      markers::add (*edit, edit->getTransport().getPosition()); status ("Marker added: double-click it on the ruler to rename, right-click for quick names"); return true;
            case cmdPrevMarker:     edit->getTransport().setPosition (markers::next (*edit, edit->getTransport().getPosition(), -1)); return true;
            case cmdNextMarker:     edit->getTransport().setPosition (markers::next (*edit, edit->getTransport().getPosition(), 1)); return true;

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
            case cmdMixer:          showMixer (! mixerShown); return true;
            case cmdTuner:          showTuner(); return true;
            case cmdWaveBigger:     setWaveZoom (2.0f); return true;
            case cmdWaveSmaller:    setWaveZoom (0.5f); return true;
            case cmdAutomationOff:  setAutomation (0); return true;
            case cmdAutomationVolume: setAutomation (1); return true;
            case cmdAutomationPan:  setAutomation (2); return true;
            case cmdAutomationCycle: setAutomation ((editComponent->getEditViewState().automationMode.get() + 1) % 3); return true;
            case cmdAddFxReverb:    addFxTrack (0); return true;
            case cmdAddFxDelay:     addFxTrack (1); return true;
            case cmdAddFxEmpty:     addFxTrack (2); return true;
            case cmdAddGroup:       addGroup ({}); return true;
            case cmdSendToEncore:   sendToEncore(); return true;
            case cmdPianoRoll:      if (pianoRoll) closePianoRoll(); else if (auto mc = selectedMidiClip()) openPianoRoll (*mc); return true;
            case cmdQuantize:
            {
                const double grid = pianoRoll ? pianoRoll->gridBeats() : 0.25;
                editNotes ("Quantize notes", [grid] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::quantize (c, n, grid); });
                return true;
            }
            case cmdTransposeUp:    editNotes ("Transpose", [] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::transpose (c, n, 1); }); return true;
            case cmdTransposeDown:  editNotes ("Transpose", [] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::transpose (c, n, -1); }); return true;
            case cmdOctaveUp:       editNotes ("Transpose", [] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::transpose (c, n, 12); }); return true;
            case cmdOctaveDown:     editNotes ("Transpose", [] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::transpose (c, n, -12); }); return true;
            case cmdVelocityLouder: editNotes ("Note velocity", [] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::velocity (c, n, -1, 10); }); return true;
            case cmdVelocitySofter: editNotes ("Note velocity", [] (te::MidiClip& c, juce::Array<te::MidiNote*> n) { midiedit::velocity (c, n, -1, -10); }); return true;
            case cmdGroupSelected:  addGroup (selectedTracks()); return true;

            case cmdShortcuts:      showShortcuts(); return true;
            case cmdGuide:          url ("https://github.com/jimmyrlondo/encore-studio#readme"); return true;
            case cmdReport:         showReport(); return true;
            case cmdUpdates:        checkForUpdates (true); return true;
            case cmdAutoUpdates:    props().setValue ("checkUpdates", ! props().getBoolValue ("checkUpdates", true)); commandManager.commandStatusChanged(); return true;
            case cmdLogFolder:      juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (studioProfile()).startAsProcess(); return true;
            case cmdAbout:          showAbout(); return true;
            default: return false;
        }
    }

private:
    //==============================================================================
    static constexpr int menuH = 26, toolbarH = 56, recentBaseId = 0x5000, comingSoonId = 0x5fff;
    double constructionStarted = juce::Time::getMillisecondCounterHiRes();
    StudioBehaviour* behaviour = new StudioBehaviour (true); // owned by the engine
    te::Engine engine { studioProfile(), std::make_unique<StudioUIBehaviour>(), std::unique_ptr<te::EngineBehaviour> (behaviour) };
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
                     addTrackButton { "+ Track" }, importButton { "Import" }, exportButton { "Export mix" };
    juce::Label timeLabel { {}, "0:00.0" }, barLabel { {}, "Bar 1.1" }, tempoLabel { {}, "120" }, tempoCaption { {}, "BPM" }, statusLabel;
    LevelMeter masterMeter { LevelMeter::Orientation::horizontal };
    std::unique_ptr<MixerComponent> mixer;
    std::unique_ptr<NoteEditor> pianoRoll;   // the piano roll or the drum grid
    DividerBar pianoDivider;
    int pianoRollH = 320;
    std::optional<int> dragStartHeight;
    bool mixerShown = false;
    juce::TextButton mixerButton { "Mixer" };
    juce::ComboBox snapBox;
    juce::Label deviceLabel;
    CountInOverlay countIn;
    juce::uint32 lastAutosave = 0;
    std::thread autosaveWriter;              // writes the copy, so a slow disk never stalls the app
    std::atomic<bool> autosaveWriting { false };
    bool crashedLastSession = false;
    static constexpr int statusH = 24;

    juce::PropertiesFile& props() { return engine.getPropertyStorage().getPropertiesFile(); }
    static bool isAudioFile (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf;w64;bwf"); }
    void status (const juce::String& s) { statusLabel.setColour (juce::Label::textColourId, theme::muted); statusLabel.setText (s, juce::dontSendNotification); }
    /** A message that stays put for a while (background progress doesn't replace it) */
    void warn (const juce::String& s) { status (s); statusHoldUntil = juce::Time::getMillisecondCounter() + 20000; statusLabel.setColour (juce::Label::textColourId, theme::solo); }
    /** Background progress: only when nothing more important is showing */
    void statusSoft (const juce::String& s)
    {
        if (juce::Time::getMillisecondCounter() < statusHoldUntil) return;
        statusLabel.setColour (juce::Label::textColourId, theme::muted);
        status (s);
    }
    juce::uint32 statusHoldUntil = 0;
    void invoke (int id) { commandManager.invokeDirectly (id, false); }
    juce::String songTitle() { return edit ? SongInfo::readFrom (*edit, songFile).title : juce::String(); }

    void showHub (bool show)
    {
        hubShown = show;
        hub->setVisible (show);
        if (show) hub->refresh (edit ? songTitle() : juce::String());
        const bool tools = ! show && edit != nullptr;
        for (auto* c : std::initializer_list<juce::Component*> { &homeButton, &playButton, &recordButton, &loopButton, &clickButton,
                                                                 &exportButton, &timeLabel, &barLabel, &tempoLabel, &tempoCaption, &masterMeter, &snapBox, &mixerButton })
            c->setVisible (tools);
        if (editComponent) editComponent->setVisible (! show && ! mixerShown);
        if (mixer) mixer->setVisible (! show && mixerShown);
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
            studioLog ("splitting " + wip->getName() + " into mono inputs");
            dm.setDeviceNumChannels (*wip, 1);
        }
        juce::StringArray names;
        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto wip = dm.getWaveInDevice (i)) names.add (wip->getName() + (wip->isStereoPair() ? " (stereo)" : ""));
        studioLog ("inputs: " + names.joinIntoString (", "));

        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto wip = dm.getWaveInDevice (i))
            {
                wip->setEnabled (true);
                wip->setRecordAdjustmentMs (props().getDoubleValue ("recordOffsetMs", 0.0));
                if (edit) wip->setBitDepth (SongInfo::readFrom (*edit, songFile).bitDepth);
            }
    }

    /** ASIO drivers that wrap other sound cards rather than belonging to an interface */
    static bool isGenericAsio (const juce::String& name)
    {
        auto lower = name.toLowerCase();
        return lower.contains ("asio4all") || lower.contains ("generic") || lower.contains ("magix") || lower.contains ("built-in") || lower.contains ("flexasio");
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
        if (asio == nullptr) { studioLog ("ASIO check: no ASIO support (types: " + typeNames.joinIntoString (", ") + ")"); return; }
        asio->scanForDevices();

        auto setup = dm.getAudioDeviceSetup();
        const juce::StringArray generic { "input", "output", "line", "speakers", "speaker", "headphones", "microphone", "mic", "audio", "device",
                                          "usb", "high", "definition", "realtek", "digital", "stereo", "default", "communications", "array" };
        juce::StringArray words;
        for (auto& n : { setup.inputDeviceName, setup.outputDeviceName })
            for (auto& w : juce::StringArray::fromTokens (n.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 "), " ", {}))
                if (w.length() >= 3 && ! generic.contains (w) && ! w.containsOnly ("0123456789")) words.addIfNotAlreadyThere (w);

        studioLog ("ASIO check: using " + dm.getCurrentAudioDeviceType() + " (" + setup.inputDeviceName + " / " + setup.outputDeviceName + "), looking for "
                     + words.joinIntoString ("/") + " among ASIO drivers: " + asio->getDeviceNames().joinIntoString (", "));
        for (auto& name : asio->getDeviceNames())
        {
            auto lower = name.toLowerCase();
            if (isGenericAsio (name))
                continue;
            bool match = false;
            for (auto& w : words) match = match || lower.contains (w);
            if (! match) continue;

            // try exactly this driver (no falling back to some other ASIO device), and go back if it won't open
            const auto beforeType = dm.getCurrentAudioDeviceType();
            auto before = dm.createStateXml();
            juce::XmlElement want ("DEVICESETUP");
            want.setAttribute ("deviceType", "ASIO");
            want.setAttribute ("audioOutputDeviceName", name);
            want.setAttribute ("audioInputDeviceName", name);
            auto err = dm.initialise (512, 512, &want, false);
            if (err.isEmpty() && dm.getCurrentAudioDevice() != nullptr && dm.getCurrentAudioDevice()->getName() == name)
            {
                studioLog ("switched to the interface's ASIO driver: " + name);
                status ("Now using " + name + "'s own driver (ASIO) for low latency. Studio > Audio Setup to change.");
                return;
            }
            studioLog ("ASIO driver " + name + " didn't open (" + err + "), staying on " + beforeType);
            props().setValue ("asioChecked", false); // try again next time (another app may have had it)
            if (before != nullptr) dm.initialise (512, 512, before.get(), true);
            else { dm.setCurrentAudioDeviceType (beforeType, true); dm.setAudioDeviceSetup (setup, true); }
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
            if (groups::isGroup (*t) || groups::isFxTrack (*t)) continue;   // these play other tracks, not an input
            auto want = t->state.getProperty ("basementInput").toString();
            if (want == "none" && ! force) continue;
            if (want == "midi")
            {
                if (auto midi = instruments::midiInput (*edit)) [[maybe_unused]] auto r = midi->setTarget (t->itemID, true, &edit->getUndoManager(), 0);
                continue;
            }
            if (! assignInput (*t, want == "none" ? juce::String() : want))
                all = false;
        }
        if (all) assignUntil = 0;
    }

    bool assignInput (te::AudioTrack& t, const juce::String& wanted)
    {
        te::InputDeviceInstance* pick = nullptr;
        // a named input can feed several tracks (overdubs: bass, then guitar, both on input 1);
        // a track with no particular input gets one nobody's using
        for (auto instance : edit->getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
            {
                if (wanted.isNotEmpty() && instance->getInputDevice().getName() == wanted) { pick = instance; break; }
                if (pick == nullptr && wanted.isEmpty() && instance->getTargets().isEmpty()) pick = instance;
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
        auto res = pick->setTarget (t.itemID, false, &edit->getUndoManager(), 0);
        studioLog (t.getName() + " <- " + pick->getInputDevice().getName() + (res ? " ok" : " FAILED: " + juce::String (res.error())));
        // remember the input, unless this is a stand-in for one that isn't here right now
        if (res && (wanted.isEmpty() || wanted == pick->getInputDevice().getName()))
            t.state.setProperty ("basementInput", pick->getInputDevice().getName(), nullptr);
        return (bool) res;
    }

    //==============================================================================
    // Songs
    juce::File projectsFolder() { return library::songsFolderOr (props().getValue ("projectsFolder")); }

    //==============================================================================
    // Account: who you're making music as (Account menu, the Hub's artist picker, the first screen)
    std::unique_ptr<AccountComponent> firstScreen;
    static constexpr int tempoChangeId = 0x5200, drumSnapId = 0x5201;
    static constexpr int accountBaseId = 0x5100, accountAddId = 0x51f0, accountFolderId = 0x51f1, accountEditId = 0x51f2,
                         accountSignInId = 0x51f3, accountSignOutId = 0x51f4, accountLastId = 0x51f4;

    void showFirstScreen()
    {
        firstScreen = std::make_unique<AccountComponent> (true);
        firstScreen->onDone = [safe = juce::Component::SafePointer<MainComponent> (this)] (bool)
        {
            juce::MessageManager::callAsync ([safe] { if (safe) { safe->firstScreen = nullptr; safe->accountChanged(); } });
        };
        addAndMakeVisible (*firstScreen);
        resized();
    }

    void editProfile()
    {
        auto* c = new AccountComponent (false);
        c->onDone = [safe = juce::Component::SafePointer<MainComponent> (this)] (bool) { if (safe) safe->accountChanged(); };
        showDialog ("Your profile", c, this);
    }

    void addArtist()
    {
        if (! account::get().signedIn()) { showFirstScreen(); return; }
        askForText ("Add an artist", "The artist or band's name (they get their own folder for songs, releases and masters):", {}, [this] (juce::String name)
        {
            name = name.trim();
            if (name.isEmpty()) return;
            auto& p = account::get();
            int at = -1;
            for (int i = 0; i < p.artists.size(); ++i) if (p.artists[i].name.equalsIgnoreCase (name)) at = i;
            if (at < 0) { p.artists.add ({ name, {} }); at = p.artists.size() - 1; }
            p.current = at;
            library::ensure (p.library, p.artists[at]);
            account::save();
            accountChanged();
            status ("New songs are for " + name + " now");
        });
    }

    juce::PopupMenu accountMenu()
    {
        juce::PopupMenu m;
        auto& p = account::get();
        if (! p.signedIn())
        {
            m.addItem (accountSignInId, "Set Up Your Profile...");
            m.addItem (comingSoonId, "Sign in with Encore (coming soon)", false);
            return m;
        }
        m.addSectionHeader (p.isLabel() ? p.label : (p.name.isNotEmpty() ? p.name : p.who()));
        for (int i = 0; i < p.artists.size(); ++i) m.addItem (accountBaseId + i, p.artists[i].name, true, i == p.current);
        m.addItem (accountAddId, "Add an Artist...");
        m.addSeparator();
        m.addItem (accountFolderId, "Show " + p.artist()->name + "'s Folder");
        m.addItem (accountEditId, "Edit Profile...");
        m.addItem (comingSoonId, "Sign in with Encore (coming soon)", false);
        m.addSeparator();
        m.addItem (accountSignOutId, "Sign Out");
        return m;
    }

    void accountItem (int id)
    {
        auto& p = account::get();
        if (id >= accountBaseId && id < accountBaseId + p.artists.size())
        {
            p.current = id - accountBaseId;
            account::save();
            accountChanged();
            status ("New songs are for " + p.artist()->name + " now");
        }
        else if (id == accountAddId) addArtist();
        else if (id == accountEditId) editProfile();
        else if (id == accountSignInId) showFirstScreen();
        else if (id == accountFolderId) { if (auto* a = p.artist()) library::ensure (p.library, *a).startAsProcess(); }
        else if (id == accountSignOutId)
        {
            account::signOut();
            accountChanged();
            status ("Signed out. Your songs and folders are all still where they were.");
        }
    }

    void accountChanged()
    {
        hub->refreshAccount();
        menuItemsChanged();
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
        auto chooser = std::make_shared<juce::FileChooser> ("Open a song", projectsFolder(), juce::String (songfile::patterns) + ";*.tracktionedit");
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
        closePianoRoll();
        mixer = nullptr;
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
                studioLog ("switched the interface to " + juce::String (info.sampleRate) + " Hz for " + info.title);
            }
        setupInputs();

        int i = 0;
        for (auto t : te::getAudioTracks (*edit)) if (t->getColour() == juce::Colour()) t->setColour (theme::trackColour (i++));
        assignUntil = juce::Time::getMillisecondCounter() + 4000;
        assignInputs();

        editComponent = std::make_unique<EditComponent> (*edit, selectionManager);
        editComponent->getEditViewState().automationMode = 0; // songs open showing the clips, not automation
        editComponent->getEditViewState().waveformZoom = 1.0f; // and waveforms at their true size
        addAndMakeVisible (*editComponent);
        mixer = std::make_unique<MixerComponent> (*edit, selectionManager);
        mixer->onAddFxTrack = [this]
        {
            juce::PopupMenu m;
            m.addItem ("Reverb (Encore Verb)", [this] { addFxTrack (0); });
            m.addItem ("Delay", [this] { addFxTrack (1); });
            m.addItem ("Empty (add your own effect)", [this] { addFxTrack (2); });
            m.showMenuAsync ({});
        };
        mixer->onAddGroup = [this] { addGroup (selectedTracks()); };
        addChildComponent (*mixer);
        mixer->setVisible (mixerShown);
        editComponent->setVisible (! mixerShown);
        snapBox.setSelectedId (editComponent->getEditViewState().snapMode.get() + 1, juce::dontSendNotification);
        props().setValue ("lastSong", f.getFullPathName());
        recent::remember (props(), *edit, songFile);
        lastAutosave = juce::Time::getMillisecondCounter();
        if (crashedLastSession) juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->offerAutosave(); });
        edit->restartPlayback();
        showHub (false);
        statusSoft ("Opened " + info.title);
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
            auto renamed = to.getChildFile (to.getFileName() + songFile.getFileExtension());
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
        if (edit == nullptr) return;
        save();
        const auto next = versions::nextFile (songFile);
        auto* w = new juce::AlertWindow ("Save New Version",
                                         "This saves the song as " + next.getFileNameWithoutExtension() + " and keeps working there; the one you have now stays as it is.\n\n"
                                         "What's different in this one? (optional, e.g. \"chorus louder\")", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("note", {});
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, next] (int r)
        {
            if (r != 1 || safe == nullptr) return;
            auto note = w->getTextEditorContents ("note").trim();
            if (! safe->songFile.copyFileTo (next)) { safe->warn ("Couldn't save a new version"); return; }
            if (note.isNotEmpty()) versions::setNote (next, note);
            safe->loadSong (next);
            safe->status ("Saved " + next.getFileNameWithoutExtension() + (note.isNotEmpty() ? " (" + note + ")" : juce::String())
                          + ". The earlier version is still there: File > Versions.");
        }), true);
    }

    /** The song's tracks, names, colours, effects, tempo and routing, without any audio */
    void saveTemplate()
    {
        askForText ("Save as Template", "Name this template. It shows up under Start from when you make a new song.", songTitle(), [this] (juce::String name)
        {
            save();
            templates::folder().createDirectory();
            auto file = templates::folder().getChildFile (juce::File::createLegalFileName (name) + songfile::ext);
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

    //==============================================================================
    // MIDI editing
    te::MidiClip* selectedMidiClip()
    {
        if (! edit) return nullptr;
        for (auto* s : selectionManager.getSelectedObjects())
            if (auto mc = dynamic_cast<te::MidiClip*> (s)) return mc;
        return nullptr;
    }

    void openPianoRoll (te::MidiClip& c, std::optional<bool> asDrums = {})
    {
        auto* track = dynamic_cast<te::AudioTrack*> (c.getTrack());
        const bool drumsView = asDrums ? *asDrums : track != nullptr && drums::isDrumTrack (*track);
        if (pianoRoll && pianoRoll->getClip() == &c && (dynamic_cast<DrumGrid*> (pianoRoll.get()) != nullptr) == drumsView) { pianoRoll->grabKeyboardFocus(); return; }
        if (drumsView) pianoRoll = std::make_unique<DrumGrid> (c);
        else pianoRoll = std::make_unique<PianoRoll> (c);
        pianoRoll->onClose = [this] { closePianoRoll(); };
        te::SafeSelectable<te::MidiClip> safeClip (&c);
        pianoRoll->onSwitchView = [this, safeClip, drumsView]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), safeClip, drumsView]
            {
                if (safe && safeClip != nullptr) safe->openPianoRoll (*safeClip.get(), ! drumsView);
            });
        };
        pianoRollH = props().getIntValue ("pianoRollHeight", 320);
        addAndMakeVisible (*pianoRoll);
        if (mixerShown) showMixer (false);
        resized();
        pianoRoll->grabKeyboardFocus();
        commandManager.commandStatusChanged();
        statusSoft (drumsView ? "Drums: click a step for a hit, drag to paint, Shift+click for an accent, right-click for more"
                              : "Piano roll: click to add a note, drag to move, drag a note's end to resize, double-click to delete");
    }

    /** Project > Add Drum Track: the Studio Kit on a Sampler, and a 4-bar pattern to fill in */
    void addDrumTrack()
    {
        if (! edit) return;
        auto plugin = edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {});
        auto* sampler = dynamic_cast<te::SamplerPlugin*> (plugin.get());
        if (sampler == nullptr) return;
        auto tracks = te::getAudioTracks (*edit);
        auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, tracks.isEmpty() ? nullptr : tracks.getLast()), &selectionManager);
        if (t == nullptr) return;
        t->setName ("Drums");
        t->setColour (theme::trackColour (tracks.size()));
        t->state.setProperty ("basementRole", "drums", nullptr);
        instruments::makeInstrumentTrack (*t, plugin, false);
        drums::kit::load (*sampler, songFile.getParentDirectory());
        // 4 bars from the bar the playhead is in
        auto& ts = edit->tempoSequence;
        const int beatsPerBar = juce::jmax (1, ts.getTimeSig (0)->numerator.get());
        const double startBeat = std::floor (ts.toBeats (edit->getTransport().getPosition()).inBeats() / beatsPerBar) * beatsPerBar;
        const auto start = ts.toTime (te::BeatPosition::fromBeats (startBeat));
        const auto end = ts.toTime (te::BeatPosition::fromBeats (startBeat + 4.0 * beatsPerBar));
        if (auto clip = t->insertMIDIClip ("Beat", te::TimeRange (start, end), nullptr))
            openPianoRoll (*clip, true);
        status ("Drum track ready: click steps in the drum grid, then play. Drag the clip's end to make it longer.");
    }

    void closePianoRoll()
    {
        if (! pianoRoll) return;
        pianoRoll = nullptr;
        resized();
        commandManager.commandStatusChanged();
    }

    /** What the MIDI menu edits: the piano roll's selection (or its whole clip), else every note in the selected MIDI clips */
    std::vector<std::pair<te::MidiClip*, juce::Array<te::MidiNote*>>> midiTargets()
    {
        std::vector<std::pair<te::MidiClip*, juce::Array<te::MidiNote*>>> out;
        if (! edit) return out;
        if (pianoRoll && pianoRoll->getClip() != nullptr)
        {
            out.push_back ({ pianoRoll->getClip(), pianoRoll->targetNotes() });
            return out;
        }
        for (auto* s : selectionManager.getSelectedObjects())
            if (auto mc = dynamic_cast<te::MidiClip*> (s))
                if (mc->getSequence().getNumNotes() > 0) out.push_back ({ mc, midiedit::allNotes (*mc) });
        return out;
    }

    void editNotes (const juce::String& what, std::function<void (te::MidiClip&, juce::Array<te::MidiNote*>)> f)
    {
        auto targets = midiTargets();
        if (targets.empty()) { status ("Select a MIDI clip first (or open one in the piano roll)"); return; }
        edit->getUndoManager().beginNewTransaction (what);
        int n = 0;
        for (auto& [clip, notes] : targets) { f (*clip, notes); n += notes.size(); }
        if (pianoRoll) pianoRoll->repaint();
        statusSoft (what + ": " + juce::String (n) + (n == 1 ? " note" : " notes"));
    }

    /** Every selected track (Ctrl-click track headers to select several), for grouping */
    juce::Array<te::AudioTrack*> selectedTracks()
    {
        juce::Array<te::AudioTrack*> out;
        if (! edit) return out;
        for (auto* s : selectionManager.getSelectedObjects())
            if (auto t = dynamic_cast<te::AudioTrack*> (s))
                if (! groups::isGroup (*t) && ! groups::isFxTrack (*t)) out.addIfNotAlreadyThere (t);
        return out;
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
            auto clip = track->insertWaveClip (src.getFileNameWithoutExtension(), file,
                                               { { 0_tp, te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
            // a loop with its tempo in the name ("Beat 92bpm.wav") plays in time with the song
            if (const double bpm = stretch::tempoInName (src.getFileNameWithoutExtension()); bpm > 0 && clip != nullptr)
            {
                stretch::follow (*clip, bpm);
                status (src.getFileNameWithoutExtension() + " is at " + juce::String (bpm, 0) + " BPM: it follows the song's tempo (right-click it to change that)");
            }
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
        status ("Duplicated " + juce::String (clips.size()) + (clips.size() == 1 ? " clip" : " clips"));
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
        if (auto* w = o.launchAsync()) darkTitleBar (*w);
        status ("Options > Scan for new or updated plug-ins finds everything installed, Encore's included.");
    }

    //==============================================================================
    // Export: the whole song (every track, every effect) to a 24-bit WAV
    void exportMix()
    {
        if (currentExport) { status ("Already exporting..."); return; }
        if (edit->getLength() <= 0s) { status ("Nothing to export yet: record or import something first."); return; }
        save();
        const bool hasLoop = edit->getTransport().getLoopRange().getLength() > 0s;
        auto* c = new ExportComponent (songTitle(), songFile.getParentDirectory().getChildFile ("Exports"),
                                       SongInfo::readFrom (*edit, songFile).sampleRate, hasLoop, props().getIntValue ("masterStyle", -1));
        juce::Component::SafePointer<ExportComponent> dialog (c);
        juce::Component::SafePointer<MainComponent> safe (this);
        c->onListen = [safe, dialog] (ExportOptions o) { if (safe) safe->startMasterPreview (o, dialog); };
        c->onStopListening = [safe] { if (safe) safe->stopMasterPreview(); };
        c->onHearOriginal = [safe] (bool on) { if (safe) safe->previewPlayer.original = on; };
        c->onSameLoudness = [safe] (bool on) { if (safe) safe->previewPlayer.sameLoudness = on; };
        c->onExport = [this] (ExportOptions o, bool stems, juce::File folder, juce::String base)
        {
            if (! stems) props().setValue ("masterStyle", o.mastering.style);
            if (o.range) o.range = edit->getTransport().getLoopRange();
            folder.createDirectory();
            exportQueue.clear();
            const auto ext = o.mp3Kbps > 0 ? juce::String (".mp3") : o.flac ? juce::String (".flac") : juce::String (".wav");
            const auto info = SongInfo::readFrom (*edit, songFile);
            o.tags = { info.title.isNotEmpty() ? info.title : songTitle(), info.artist, info.album, info.genre };
            if (stems)
            {
                auto tracks = te::getAudioTracks (*edit);
                for (int i = 0; i < tracks.size(); ++i)
                {
                    auto job = o;
                    job.trackIndex = i;
                    job.tags.title << " (" << tracks[i]->getName() << ")";
                    exportQueue.add ({ job, folder.getChildFile (base + " - " + juce::File::createLegalFileName (tracks[i]->getName()) + ext), tracks[i]->getName() });
                }
            }
            else exportQueue.add ({ o, folder.getChildFile (base + ext), "the mix", ! o.range }); // a whole-song mix is kept in Masters
            exportTotal = exportQueue.size();
            exportFolder = folder;
            exportFailures.clear();
            runNextExport();
        };
        showDialog ("Export", c, this);
    }

    struct ExportJob { ExportOptions options; juce::File dest; juce::String what; bool master = false; };
    juce::Array<ExportJob> exportQueue;
    int exportTotal = 0;
    juce::File exportFolder;
    juce::StringArray exportFailures;
    juce::String exportingWhat;

    /** One render at a time (each reopens the song for rendering), then the next */
    void runNextExport()
    {
        if (exportQueue.isEmpty())
        {
            exportButton.setEnabled (true);
            if (auto then = std::exchange (afterExports, nullptr)) { then (exportFailures.isEmpty()); return; }
            if (exportFailures.isEmpty()) { status ("Exported " + juce::String (exportTotal) + (exportTotal == 1 ? " file" : " files") + " to " + exportFolder.getFullPathName()); exportFolder.revealToUser(); }
            else status ("Some exports didn't work: " + exportFailures.joinIntoString ("; "));
            return;
        }
        auto job = exportQueue.removeAndReturn (0);
        exportingWhat = job.what + (exportTotal > 1 ? " (" + juce::String (exportTotal - exportQueue.size()) + " of " + juce::String (exportTotal) + ")" : juce::String());
        exportButton.setEnabled (false);
        currentExport = exportSong (engine, songFile, job.dest, job.options, [this, job] (juce::File out, juce::String err)
        {
            currentExport = nullptr;
            if (! out.existsAsFile()) exportFailures.add (job.what + ": " + err);
            else if (job.master) keepMaster (out);
            runNextExport();
        });
        if (currentExport == nullptr && exportQueue.isEmpty()) exportButton.setEnabled (true);
    }
    std::shared_ptr<SongExport> currentExport;
    std::function<void (bool ok)> afterExports;

    /** A finished master of a song in an artist's folder also goes in <Artist>/Masters, so they always have it */
    void keepMaster (const juce::File& master)
    {
        auto artist = library::artistFolderOf (songFile);
        if (artist == juce::File() || ! edit) return;
        auto info = SongInfo::readFrom (*edit, songFile);
        auto kept = library::keepMaster (artist, master, info.album, info.title.isNotEmpty() ? info.title : songTitle());
        studioLog ("master kept: " + kept.getFullPathName());
    }   // instead of revealing the folder, when set

    //==============================================================================
    // Export > Master it > Listen: 30 s rendered twice (mastered, and as it is), played in sync
    mastering::PreviewPlayer previewPlayer;
    bool previewAttached = false;
    int previewRound = 0;
    std::shared_ptr<SongExport> previewMastered, previewOriginal;

    void startMasterPreview (ExportOptions o, juce::Component::SafePointer<ExportComponent> dialog)
    {
        ExportOptions plain = o;
        plain.mastering = {};
        previewPair (songFile, o, songFile, plain,
                     [dialog] (const juce::String& t) { if (dialog) dialog->setPreviewText (t); },
                     [] (float diff)
                     {
                         return "Playing the mastered version (loops). It's " + juce::String (std::abs (diff), 1) + " dB "
                                + (diff >= 0 ? "louder" : "quieter") + " than the original.";
                     });
    }

    /** The same 30 s of two songs (or one song two ways) rendered, then played in sync:
        the first one plays, previewPlayer.original switches to the second */
    void previewPair (juce::File songA, ExportOptions a, juce::File songB, ExportOptions b,
                      std::function<void (const juce::String&)> say, std::function<juce::String (float diffDb)> describe)
    {
        stopMasterPreview();
        if (edit == nullptr) return;
        auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        if (device == nullptr) { say ("There's no audio interface open to listen on (Studio > Audio Setup)."); return; }
        edit->getTransport().stop (false, false);
        const int round = ++previewRound;

        // 30 s from the playhead, or from a third of the way in (often a chorus) when it's at the start
        const double length = edit->getLength().inSeconds();
        const double playhead = edit->getTransport().getPosition().inSeconds();
        const double duration = juce::jmin (30.0, length);
        const double from = juce::jlimit (0.0, juce::jmax (0.0, length - duration), playhead > 0.5 && playhead < length - 2.0 ? playhead : length / 3.0);
        for (auto* o : { &a, &b })
        {
            o->range = te::TimeRange (te::TimePosition::fromSeconds (from), te::TimeDuration::fromSeconds (duration));
            o->sampleRate = device->getCurrentSampleRate();
            o->bitDepth = 32; o->flac = false; o->mp3Kbps = 0; o->tail = false; o->loudness = false; o->trackIndex = -1;
        }

        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory);
        auto fa = tmp.getChildFile ("studio-preview-a.wav"), fb = tmp.getChildFile ("studio-preview-b.wav");
        say ("Getting 30 seconds ready (from " + juce::String ((int) from / 60) + ":" + juce::String ((int) from % 60).paddedLeft ('0', 2) + ")...");
        auto results = std::make_shared<int> (0);
        juce::Component::SafePointer<MainComponent> safe (this);
        auto finished = [safe, round, fa, fb, say, describe, results, rate = a.sampleRate] (juce::File, juce::String err)
        {
            if (safe == nullptr || round != safe->previewRound) return;
            if (err.isNotEmpty()) say ("The preview didn't render: " + err);
            if (++*results < 2) return;
            auto read = [] (const juce::File& f)
            {
                juce::AudioFormatManager formats; formats.registerBasicFormats();
                juce::AudioBuffer<float> buf;
                if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f)); r != nullptr)
                {
                    buf.setSize (juce::jmax (2, (int) r->numChannels), (int) r->lengthInSamples);
                    r->read (&buf, 0, buf.getNumSamples(), 0, true, true);
                }
                return buf;
            };
            auto x = read (fa), y = read (fb);
            fa.deleteFile(); fb.deleteFile();
            if (x.getNumSamples() == 0 || y.getNumSamples() == 0) { say ("The preview didn't render."); return; }
            const int n = juce::jmin (x.getNumSamples(), y.getNumSamples());
            x.setSize (x.getNumChannels(), n, true); y.setSize (y.getNumChannels(), n, true);
            safe->previewPlayer.original = false;
            safe->previewPlayer.load (std::move (x), std::move (y), rate);
            auto& dm = safe->engine.getDeviceManager().deviceManager;
            if (! safe->previewAttached) { dm.addAudioCallback (&safe->previewPlayer); safe->previewAttached = true; }
            safe->previewPlayer.playing = true;
            say (describe (safe->previewPlayer.loudnessDifferenceDb));
        };
        previewMastered = exportSong (engine, songA, fa, a, finished);
        previewOriginal = exportSong (engine, songB, fb, b, finished);
    }

    //==============================================================================
    // File > Versions
    void showVersions()
    {
        if (edit == nullptr) return;
        save();
        auto* c = new versions::VersionsComponent (songFile);
        juce::Component::SafePointer<versions::VersionsComponent> dialog (c);
        juce::Component::SafePointer<MainComponent> safe (this);
        c->onOpen = [safe] (juce::File f) { if (safe) { safe->stopMasterPreview(); safe->save(); safe->loadSong (f); } };
        c->onSaveNew = [safe] { if (safe) safe->saveNewVersion(); };
        c->onHearOther = [safe] (bool other) { if (safe) safe->previewPlayer.original = other; };
        c->onStopComparing = [safe] { if (safe) safe->stopMasterPreview(); };
        c->onCompare = [safe, dialog] (juce::File other)
        {
            if (safe == nullptr) return;
            ExportOptions o;
            const auto otherName = "v" + juce::String (versions::split (other.getFileNameWithoutExtension()).second);
            safe->previewPair (safe->songFile, o, other, o,
                               [dialog] (const juce::String& t) { if (dialog) dialog->comparisonReady (t); },
                               [otherName] (float diff)
                               {
                                   return "Hearing the open version. Tick the box for " + otherName
                                          + (std::abs (diff) < 0.5f ? juce::String (".") : " (it's " + juce::String (std::abs (diff), 1) + " dB "
                                                                                            + (diff >= 0 ? "quieter" : "louder") + ").");
                               });
        };
        showDialog ("Versions", c, this);
    }

    void stopMasterPreview()
    {
        ++previewRound;
        previewPlayer.playing = false;
        previewMastered = previewOriginal = nullptr;
        if (previewAttached) { engine.getDeviceManager().deviceManager.removeAudioCallback (&previewPlayer); previewAttached = false; }
    }

    /** File > Send to Encore */
    void sendToEncore()
    {
        if (! edit) return;
        save();
        auto info = SongInfo::readFrom (*edit, songFile);
        auto* c = new EncoreSendComponent (info, songFile.getParentDirectory());
        c->onSend = [this, info] (EncoreSendComponent::Choice choice)
        {
            const auto title = info.title.isNotEmpty() ? info.title : songTitle();
            // in the artist's Releases folder (Releases/<Album>/<Song>, or Releases/<Single>), else beside the song
            auto artist = library::artistFolderOf (songFile);
            auto dir = artist == juce::File() ? songFile.getParentDirectory().getChildFile ("Encore").getChildFile (juce::File::createLegalFileName (title))
                                              : library::releaseFolder (artist, info.album, title);
            if (artist != juce::File() && info.album.isNotEmpty()) dir = dir.getChildFile (library::folderName (title));
            dir.getChildFile ("stems").deleteRecursively();   // only what the last package made: anything else in the folder stays
            dir.getChildFile ("release.json").deleteFile();
            dir.createDirectory();

            ExportOptions mix;
            mix.sampleRate = info.sampleRate;
            mix.bitDepth = 24;
            mix.loudness = choice.loud;
            mix.masterEffects = choice.masterEffects;
            exportQueue.clear();
            const auto base = juce::File::createLegalFileName (title);
            exportQueue.add ({ mix, dir.getChildFile (base + ".wav"), "the master", true });
            juce::StringArray stemFiles;
            if (choice.stems)
            {
                auto tracks = te::getAudioTracks (*edit);
                for (int i = 0; i < tracks.size(); ++i)
                {
                    if (tracks[i]->getClips().isEmpty() && ! groups::isGroup (*tracks[i])) continue;
                    ExportOptions st = mix;
                    st.trackIndex = i; st.loudness = false; st.masterEffects = false;
                    auto name = "stems/" + juce::File::createLegalFileName (tracks[i]->getName()) + ".wav";
                    stemFiles.add (name);
                    exportQueue.add ({ st, dir.getChildFile (name), tracks[i]->getName() });
                }
                dir.getChildFile ("stems").createDirectory();
            }
            if (choice.cover.existsAsFile()) choice.cover.copyFileTo (dir.getChildFile ("cover" + choice.cover.getFileExtension().toLowerCase()));

            // the song's details, for Encore's release form
            auto* rel = new juce::DynamicObject();
            rel->setProperty ("format", "encore-release-1");
            rel->setProperty ("title", title);
            rel->setProperty ("artist", info.artist);
            rel->setProperty ("album", info.album);
            rel->setProperty ("genre", info.genre);
            rel->setProperty ("key", info.key);
            rel->setProperty ("bpm", info.bpm);
            rel->setProperty ("timeSignature", info.sigText());
            rel->setProperty ("notes", info.notes);
            rel->setProperty ("lengthSeconds", edit->getLength().inSeconds());
            rel->setProperty ("audio", base + ".wav");
            rel->setProperty ("loudness", choice.loud ? "-14 LUFS" : "as mixed");
            juce::Array<juce::var> st;
            for (auto& f : stemFiles) st.add (f);
            rel->setProperty ("stems", st);
            if (choice.cover.existsAsFile()) rel->setProperty ("cover", "cover" + choice.cover.getFileExtension().toLowerCase());
            rel->setProperty ("madeWith", "Encore: Studio " + juce::String (ProjectInfo::versionString));
            rel->setProperty ("madeAt", juce::Time::getCurrentTime().toISO8601 (true));
            dir.getChildFile ("release.json").replaceWithText (juce::JSON::toString (juce::var (rel)));

            exportTotal = exportQueue.size();
            exportFolder = dir;
            exportFailures.clear();
            afterExports = [this, dir] (bool ok)
            {
                if (! ok) { status ("Some of the package didn't export: " + exportFailures.joinIntoString ("; ")); return; }
                dir.revealToUser();
                juce::URL ("https://encoremusic.studio").launchInDefaultBrowser();
                warn ("Ready for Encore: the package is in " + dir.getFullPathName() + ". Upload it on Encore (opened in your browser).");
            };
            runNextExport();
        };
        showDialog ("Send to Encore", c, this);
    }

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
        // playing a drum track from pads or a keyboard: hits go into the beat that's there (merged, not a new
        // clip on top) and snap to 16ths, like a drum machine. Other MIDI is recorded exactly as played.
        bool drums = false;
        for (auto t : te::getAudioTracks (*edit)) drums = drums || (EngineHelpers::isTrackArmed (*t) && drums::isDrumTrack (*t));
        for (auto& dev : engine.getDeviceManager().getMidiInDevices())
        {
            dev->mergeRecordings = true;
            dev->quantisation.setType (drums && props().getBoolValue ("drumRecordSnap", true) ? "1/4 beat" : "(none)");
        }
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
        auto& tempo = edit->tempoSequence.getTempoAt (edit->getTransport().getPosition());   // the tempo where the playhead is
        if (bpm >= 20 && bpm <= 400) tempo.setBpm (bpm);
        SongInfo i;
        i.bpm = tempo.getBpm();
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
    bool waveZoomIs (float z) { return editComponent == nullptr || std::abs (editComponent->getEditViewState().waveformZoom.get() - z) < 0.01f; }

    /** Waveforms drawn taller or shorter (only the picture: the audio doesn't change) */
    void setWaveZoom (float factor)
    {
        auto& z = editComponent->getEditViewState().waveformZoom;
        z = juce::jlimit (1.0f, 32.0f, z.get() * factor);
        status (z.get() <= 1.0f ? juce::String ("Waveforms at true size")
                                : "Waveforms drawn " + juce::String (juce::roundToInt (z.get())) + "x taller (only the picture: the audio is unchanged)");
    }

    bool automationIs (int mode) { return editComponent != nullptr && editComponent->getEditViewState().automationMode.get() == mode; }

    void setAutomation (int mode)
    {
        if (editComponent == nullptr) return;
        if (mixerShown) showMixer (false);
        editComponent->getEditViewState().automationMode = mode;
        const char* names[] = { "Automation hidden", "Volume automation: click a track to add points, drag them, double-click to remove",
                                "Pan automation: click a track to add points, drag them, double-click to remove" };
        status (names[juce::jlimit (0, 2, mode)]);
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
            te::Clip::Ptr keep = w;
            if (w->getAutoTempo())
                m.addItem ("Stop Following the Song's Tempo", [keep]
                {
                    if (auto* c = dynamic_cast<te::WaveAudioClip*> (keep.get())) { c->edit.getUndoManager().beginNewTransaction ("Tempo"); c->setAutoTempo (false); }
                });
            else
                m.addItem ("Follow the Song's Tempo...", [this, keep] { askToFollowTempo (keep); });
        }
        if (auto w = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            auto f = w->getAudioFile().getFile();
            m.addSeparator();
            m.addItem ("Show File in Folder", f.existsAsFile(), false, [f] { f.revealToUser(); });
        }
    }

    /** Asks what tempo a clip's audio is at (from its name, a guess, or the song's), then makes it follow the song */
    void askToFollowTempo (te::Clip::Ptr keep)
    {
        auto* c = dynamic_cast<te::WaveAudioClip*> (keep.get());
        if (c == nullptr) return;
        const auto file = c->getAudioFile().getFile();
        double guess = stretch::tempoInName (file.getFileNameWithoutExtension());
        juce::String where = "from its file name";
        if (guess <= 0 && file.isAChildOf (songFile.getParentDirectory().getChildFile ("Recordings")))
        {
            guess = edit->tempoSequence.getBpmAt (c->getPosition().getStart());
            where = "recorded in this song";
        }
        if (guess <= 0) { guess = edit->tempoSequence.getBpmAt (c->getPosition().getStart()); where = "the song's tempo"; }
        askForText ("Follow the Song's Tempo",
                    "What tempo is this audio at? (" + juce::String (guess, 1) + " is " + where + ")\n"
                    "It then plays in time with the song and follows any tempo change. The pitch stays the same.",
                    juce::String (guess, 1), [this, keep] (juce::String text)
        {
            const double bpm = text.getDoubleValue();
            auto* clip = dynamic_cast<te::WaveAudioClip*> (keep.get());
            if (clip == nullptr || bpm < 20 || bpm > 400) return;
            edit->getUndoManager().beginNewTransaction ("Follow tempo");
            stretch::follow (*clip, bpm);
            status (clip->getName() + " follows the song's tempo now (played as " + juce::String (bpm, 1) + " BPM audio)");
        });
    }

    /** A tuner window on the selected track's input (or the first one) */
    void showTuner()
    {
        juce::String inputName;
        if (auto t = selectedTrack())
            for (auto instance : edit->getAllInputDevices())
                if (te::isOnTargetTrack (*instance, *t, 0)) inputName = instance->getInputDevice().getName();
        if (tunerWindow != nullptr && tunerWindow->isVisible()) { tunerWindow->setVisible (false); return; } // T again closes it
        tunerWindow = std::make_unique<TunerWindow> (std::make_unique<TunerComponent> (engine, inputName));
        tunerWindow->centreAroundComponent (this, tunerWindow->getWidth(), tunerWindow->getHeight());
        tunerWindow->setVisible (true);
        darkTitleBar (*tunerWindow);
    }
    std::unique_ptr<TunerWindow> tunerWindow;

    /** Built-in synth first, then your instrument plugins by maker */
    juce::PopupMenu instrumentMenu()
    {
        juce::PopupMenu m;
        m.addItem ("Built-in synth (4OSC)", [this] { addInstrumentTrack (edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}), "Synth"); });
        m.addSeparator();
        std::map<juce::String, juce::PopupMenu> byMaker;
        for (auto& d : pluginsForMenus (engine))
            if (d.isInstrument)
                byMaker[d.manufacturerName.isEmpty() ? juce::String ("Other") : d.manufacturerName].addItem (d.name, [this, d]
                {
                    addInstrumentTrack (edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, d), d.name);
                });
        for (auto& [maker, menu] : byMaker) m.addSubMenu (maker, menu);
        if (byMaker.empty()) m.addItem ("Your instrument plugins show up here once they're found", false, false, nullptr);
        return m;
    }

    te::AudioTrack* addInstrumentTrack (te::Plugin::Ptr instrument, const juce::String& name)
    {
        auto tracks = te::getAudioTracks (*edit);
        auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, tracks.isEmpty() ? nullptr : tracks.getLast()), &selectionManager);
        if (t == nullptr) return nullptr;
        t->setName (name);
        t->setColour (theme::trackColour (tracks.size()));
        instruments::makeInstrumentTrack (*t, instrument);
        status (instruments::midiInput (*edit) != nullptr ? "Instrument track ready: arm it (R) and play your MIDI keyboard or pads"
                                                         : "Instrument track ready. No MIDI input found: plug in a keyboard, or import a MIDI file");
        return t.get();
    }

    //==============================================================================
    // File > Import > Live Recording: an Encore Live session or a multichannel WAV, a track per input
    void importLiveRecording()
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Import a live recording: an Encore Live session (session.json) or a multichannel WAV",
                                                            juce::File (props().getValue ("importFolder")), "*.json;*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser&)
        {
            auto f = chooser->getResult();
            if (! f.existsAsFile()) return;
            props().setValue ("importFolder", f.getParentDirectory().getFullPathName());
            auto session = liveimport::read (f);
            if (session.problem.isNotEmpty()) { warn (session.problem); return; }
            juce::String about;
            about << session.title << (session.artist.isNotEmpty() ? " by " + session.artist : juce::String()) << "\n"
                  << session.channels << (session.channels == 1 ? " input, " : " inputs, ") << liveimport::clock (session.seconds) << " long"
                  << (session.songs.isEmpty() ? juce::String() : ", " + juce::String (session.songs.size()) + " songs marked") << ".";
            auto* w = new juce::AlertWindow ("Import a Live Recording", about, juce::MessageBoxIconType::NoIcon);
            juce::StringArray modes { "One song: the whole show" + juce::String (session.songs.isEmpty() ? "" : ", a marker at each song") };
            if (! session.songs.isEmpty()) modes.add ("A Encore Studio song for each song in the show (to mix them one by one)");
            w->addComboBox ("mode", modes, "Make");
            w->addComboBox ("silent", { "Leave out inputs with nothing on them", "Bring in every input" }, "Inputs");
            w->addButton ("Import", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<MainComponent> safe (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, session] (int r)
            {
                if (r != 1 || safe == nullptr) return;
                const bool perSong = w->getComboBoxComponent ("mode")->getSelectedItemIndex() == 1;
                const bool leaveOut = w->getComboBoxComponent ("silent")->getSelectedItemIndex() == 0;
                safe->runLiveImport (session, perSong, leaveOut);
            }), true);
        });
    }

    void runLiveImport (liveimport::Session session, bool perSong, bool leaveOutSilent)
    {
        auto shows = library::liveFolder();
        auto root = (shows != juce::File() ? shows : projectsFolder()).getNonexistentChildFile (juce::File::createLegalFileName (session.title), {}, true);
        root.createDirectory();
        auto recordingFolder = root.getChildFile ("Recording");
        auto progress = std::make_shared<std::atomic<float>> (0.0f);
        liveImportProgress = progress;
        warn ("Bringing in " + session.title + ": splitting the recording into a file per input... (the window stays usable)");
        juce::Component::SafePointer<MainComponent> safe (this);
        std::thread ([safe, session, perSong, leaveOutSilent, root, recordingFolder, progress]
        {
            juce::String problem;
            auto inputs = liveimport::split (session, recordingFolder, leaveOutSilent, [progress] (float p) { *progress = p; }, problem);
            juce::MessageManager::callAsync ([safe, session, perSong, root, inputs, problem]
            {
                if (safe == nullptr) return;
                safe->liveImportProgress = nullptr;
                if (problem.isNotEmpty() || inputs.isEmpty())
                {
                    safe->warn ("Couldn't bring in the recording: " + (problem.isNotEmpty() ? problem : juce::String ("every input was silent")));
                    return;
                }
                safe->buildLiveSongs (session, inputs, root, perSong);
            });
        }).detach();
    }

    std::shared_ptr<std::atomic<float>> liveImportProgress;

    void buildLiveSongs (const liveimport::Session& session, const juce::Array<liveimport::Input>& inputs, const juce::File& root, bool perSong)
    {
        struct Part { juce::String name; double from, to; };
        juce::Array<Part> parts;
        if (perSong)
            for (auto& s : session.songs)   // a little room either side of each song
                parts.add ({ s.title.isNotEmpty() ? s.title : "Song " + juce::String (parts.size() + 1),
                             juce::jmax (0.0, s.start - 2.0), juce::jmin (session.seconds, s.end + 3.0) });
        else parts.add ({ session.title, 0.0, session.seconds });

        juce::Array<juce::File> made;
        for (int i = 0; i < parts.size(); ++i)
        {
            const auto& part = parts.getReference (i);
            SongPlan plan;
            plan.info.title = part.name;
            plan.info.artist = session.artist;
            plan.info.album = perSong ? session.title : juce::String();
            plan.info.notes = juce::String ("Live") + (session.venue.isNotEmpty() ? " at " + session.venue : juce::String())
                              + (session.date.isNotEmpty() ? ", " + session.date : juce::String()) + ". Imported from " + session.recording.getFileName() + ".";
            plan.folder = perSong ? root.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (part.name)) : root;
            plan.info.sampleRate = session.sampleRate;
            for (auto& in : inputs) plan.tracks.add ({ in.name, {}, liveimport::groupFor (in.name) });
            auto e = songs::createFromPlan (engine, plan);
            if (e == nullptr) continue;
            auto tracks = te::getAudioTracks (*e);
            int t = 0;
            for (auto* track : tracks)
            {
                if (groups::isGroup (*track) || t >= inputs.size()) continue;
                const auto& in = inputs.getReference (t++);
                track->insertWaveClip (in.name, in.file,
                                       { { 0_tp, te::TimeDuration::fromSeconds (part.to - part.from) }, te::TimeDuration::fromSeconds (part.from) }, false);
            }
            if (! perSong)
                for (auto& s : session.songs)
                    markers::add (*e, te::TimePosition::fromSeconds (s.start), s.title.isNotEmpty() ? s.title : "Song");
            te::EditFileOperations (*e).save (true, true, false);
            made.add (plan.songFile());
        }
        if (made.isEmpty()) { warn ("Couldn't make the songs for " + session.title); return; }
        loadSong (made.getFirst());
        status (perSong ? "Made " + juce::String (made.size()) + " songs from " + session.title + " (in " + root.getFullPathName() + "). This is the first; File > Open for the rest."
                        : "Brought in " + session.title + ": " + juce::String (inputs.size()) + " tracks" + (session.songs.isEmpty() ? juce::String() : ", a marker at each song") + ".");
    }

    /** A MIDI file onto the selected instrument track (or a new one with the built-in synth), at the start of the song */
    void importMidi()
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Import a MIDI file", juce::File (props().getValue ("importFolder")), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser&)
        {
            auto f = chooser->getResult();
            if (! f.existsAsFile() || ! edit) return;
            auto t = selectedTrack();
            if (t == nullptr || ! instruments::isInstrumentTrack (*t))
                t = addInstrumentTrack (edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}), f.getFileNameWithoutExtension());
            if (t == nullptr) return;
            if (auto clip = te::createClipFromFile (f, *t, false))
                status ("Imported " + f.getFileName() + " (" + juce::String (clip->getSequence().getNumNotes()) + " notes) onto " + t->getName());
            else
                status ("Couldn't read " + f.getFileName() + " as a MIDI file");
        });
    }

    void showMixer (bool show)
    {
        mixerShown = show;
        if (editComponent) editComponent->setVisible (! hubShown && ! show);
        if (mixer) mixer->setVisible (! hubShown && show);
        mixerButton.setToggleState (show, juce::dontSendNotification);
        commandManager.commandStatusChanged();
    }

    /** 0 reverb (Encore Verb), 1 delay, 2 empty */
    void addFxTrack (int kind)
    {
        juce::PluginDescription verb;
        for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
            if (d.pluginFormatName == "Encore" && d.name == "Encore Verb") verb = d;
        int n = fx::tracks (*edit).size() + 1;
        juce::String name = kind == 0 ? "Reverb" : kind == 1 ? "Delay" : "FX " + juce::String (n);
        for (auto t : fx::tracks (*edit)) if (t->getName() == name) name << " " << n;
        auto t = kind == 0 ? fx::addTrack (*edit, name, te::ExternalPlugin::xmlTypeName, verb)
               : kind == 1 ? fx::addTrack (*edit, name, te::DelayPlugin::xmlTypeName)
                           : fx::addTrack (*edit, name, {});
        if (t != nullptr)
            status ("Added the FX track \"" + name + "\": turn up its send on any track in the mixer (F3)");
    }

    /** A group track with these tracks playing into it (none: an empty group to fill from its header) */
    void addGroup (const juce::Array<te::AudioTrack*>& members)
    {
        if (! edit) return;
        int n = groups::all (*edit).size() + 1;
        juce::String name = "Group " + juce::String (n);
        auto g = groups::add (*edit, name, members);
        if (g == nullptr) return;
        selectionManager.selectOnly (g);
        status (members.isEmpty() ? "Added \"" + name + "\": click its \"Group\" button to pick the tracks that play into it"
                                  : "Grouped " + juce::String (members.size()) + (members.size() == 1 ? " track" : " tracks") + " into \"" + name + "\": one fader, effects and mute for all of them (double-click the name to rename it)");
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

    //==============================================================================
    // Help > Check for Updates (and once a day by itself, unless that's switched off)
    void checkForUpdates (bool asked)
    {
        if (asked) status ("Checking for a newer Encore Studio...");
        else
        {
            const auto last = juce::Time (props().getValue ("lastUpdateCheck", "0").getLargeIntValue());
            if (! props().getBoolValue ("checkUpdates", true) || juce::Time::getCurrentTime() - last < juce::RelativeTime::hours (20)) return;
        }
        support::latestRelease ([safe = juce::Component::SafePointer<MainComponent> (this), asked] (support::Release r)
        {
            if (safe == nullptr) return;
            auto& self = *safe;
            const juce::String mine = ProjectInfo::versionString;
            studioLog ("updates: " + (r.found ? "newest is " + r.version : r.problem) + ", this is " + mine);
            if (! r.found) { if (asked) self.warn ("Couldn't check for updates: " + r.problem); return; }
            self.props().setValue ("lastUpdateCheck", juce::String (juce::Time::getCurrentTime().toMilliseconds()));
            if (! support::isNewer (r.version, mine))
            {
                if (asked) self.status ("You have the newest Encore Studio (" + mine + ").");
                return;
            }
            // asked about once per new version when it's automatic
            if (! asked && self.props().getValue ("updateDismissed") == r.version) return;
            const auto link = r.download.isNotEmpty() ? r.download : r.page;
            auto notes = r.notes.trim();
            if (notes.length() > 600) notes = notes.substring (0, 600).upToLastOccurrenceOf ("\n", false, false) + "\n...";
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                            .withIconType (juce::MessageBoxIconType::NoIcon)
                                            .withTitle ("Encore Studio " + r.version + " is out")
                                            .withMessage ("You have " + mine + ". Download the new one, unzip it, and run it from its folder "
                                                          "(your songs and settings carry over).\n\n" + notes)
                                            .withButton ("Download", 1)
                                            .withButton ("Not now", 0)
                                            .withAssociatedComponent (safe.getComponent()),
                                          [safe, link, version = r.version] (int button)
                                          {
                                              if (button == 1) juce::URL (link).launchInDefaultBrowser();
                                              else if (safe != nullptr) safe->props().setValue ("updateDismissed", version);
                                          });
        });
    }

    /** What a problem report says about this computer and Studio's setup */
    juce::String systemInfo()
    {
        juce::String s;
        const juce::String nl = juce::newLine;
        s << "Encore Studio " << ProjectInfo::versionString << " (" << studioProfile() << ")" << nl
          << "System: " << juce::SystemStats::getOperatingSystemName() << ", " << juce::SystemStats::getCpuModel()
          << ", " << juce::SystemStats::getNumCpus() << " cores, " << juce::SystemStats::getMemorySizeInMegabytes() << " MB" << nl;
        auto& dm = engine.getDeviceManager().deviceManager;
        auto setup = dm.getAudioDeviceSetup();
        s << "Audio: " << dm.getCurrentAudioDeviceType() << ", in '" << setup.inputDeviceName << "', out '" << setup.outputDeviceName << "'" << nl;
        if (auto* d = dm.getCurrentAudioDevice())
            s << "  " << d->getCurrentSampleRate() << " Hz, " << d->getCurrentBufferSizeSamples() << " samples, latency in "
              << d->getInputLatencyInSamples() << " / out " << d->getOutputLatencyInSamples() << ", inputs "
              << d->getActiveInputChannels().countNumberOfSetBits() << " of " << d->getInputChannelNames().size()
              << " (" << d->getInputChannelNames().joinIntoString (", ") << ")" << nl;
        else s << "  no device open" << nl;
        s << "Audio types available:";
        for (auto* t : dm.getAvailableDeviceTypes()) s << " " << t->getTypeName();
        s << nl << "Plugins known: " << engine.getPluginManager().knownPluginList.getNumTypes() << nl;
        if (edit != nullptr)
            s << "Song: " << songFile.getFullPathName() << ", " << te::getAudioTracks (*edit).size() << " tracks, "
              << juce::String (edit->getLength().inSeconds(), 1) << " s" << nl;
        if (crashedLastSession) s << "The last session didn't close properly." << nl;
        return s;
    }

    /** Help > Report a Problem */
    void showReport()
    {
        auto* c = new support::ReportComponent (edit != nullptr && songFile.existsAsFile(), crashedLastSession);
        c->onMake = [this] (juce::String what, bool includeSong)
        {
            if (includeSong && edit != nullptr) save();
            studioLog ("report: made by the user");
            auto zip = support::makeReport (what, systemInfo(), includeSong ? songFile : juce::File());
            if (zip == juce::File()) { warn ("Couldn't make the report on your Desktop."); return; }
            zip.revealToUser();
            const juce::String body = "Encore Studio " + juce::String (ProjectInfo::versionString) + " on " + juce::SystemStats::getOperatingSystemName()
                                      + "\n\nWhat happened:\n\n\n(Drag " + zip.getFileName() + " from your Desktop in here.)";
            juce::URL ("https://github.com/" + juce::String (support::repo()) + "/issues/new")
                .withParameter ("title", "Problem: ")
                .withParameter ("body", body)
                .launchInDefaultBrowser();
            status ("Made " + zip.getFileName() + " on your Desktop. Drag it into the GitHub issue that just opened.");
        };
        showDialog ("Report a Problem", c, this);
    }

    void showAbout()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Encore: Studio " + juce::String (ProjectInfo::versionString),
            "A free, open-source DAW from Encore Studio.\n\n"
            "Copyright (C) 2026 Encore Studio. Free software under the GNU AGPLv3.\n"
            "Built on Tracktion Engine (GPLv3) and JUCE (AGPLv3).\n\n"
            "github.com/jimmyrlondo/encore-studio");
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
    // it in .autosave, never over the song itself. If Studio didn't close
    // properly last time, opening the song offers the newer copy back.
    juce::File autosaveFile() const
    {
        return songFile.getParentDirectory().getChildFile (".autosave").getChildFile (songFile.getFileName());
    }

    void autosaveIfDue()
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto every = (juce::uint32) juce::jlimit (5, 3600, props().getIntValue ("autosaveSeconds", 120)) * 1000;
        if (now - lastAutosave < every || edit->getTransport().isRecording()) return;
        if (autosaveWriting) return;   // the last copy is still being written
        lastAutosave = now;
        if (! edit->hasChangedSinceSaved()) return;
        auto f = autosaveFile();
        // the song is gathered here (quick), and written to disk on a thread of its own: writing can
        // take seconds on a busy or slow drive, and the app (and a take in progress) shouldn't wait
        edit->flushState();
        std::shared_ptr<juce::XmlElement> xml (edit->state.createXml());
        if (xml == nullptr) return;
        if (autosaveWriter.joinable()) autosaveWriter.join();
        autosaveWriting = true;
        autosaveWriter = std::thread ([this, f, xml]
        {
            f.getParentDirectory().createDirectory();
            if (xml->writeTo (f)) studioLog ("autosaved " + f.getFullPathName());
            else studioLog ("autosave failed: " + f.getFullPathName());
            autosaveWriting = false;
        });
    }

    /** Called when a song opens: if a newer autosave is there from a session that
        didn't end cleanly, offer it */
    void offerAutosave()
    {
        auto f = autosaveFile();
        if (! f.existsAsFile() || f.getLastModificationTime() <= songFile.getLastModificationTime()) return;
        auto when = f.getLastModificationTime().formatted ("%b %d, %H:%M");
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Recover unsaved changes?",
            "Encore Studio didn't close properly last time. There's an automatic copy of \"" + songTitle() + "\" from " + when
            + " that's newer than the last save.\n\nOpen the recovered copy? (The last saved version is kept as \"before recovery\".)",
            "Recover", "Keep the last save", this, juce::ModalCallbackFunction::create ([this, f] (int r)
        {
            if (r != 1 || ! f.existsAsFile()) return;
            auto song = songFile;
            song.copyFileTo (song.getSiblingFile (song.getFileNameWithoutExtension() + " before recovery" + song.getFileExtension()));
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
        const double a = std::abs (pos);
        timeLabel.setText ((pos < 0 ? "-" : "") + juce::String ((int) a / 60) + ":" + juce::String (std::fmod (a, 60.0), 1).paddedLeft ('0', 4), juce::dontSendNotification);
        auto& ts = edit->tempoSequence;
        const auto bb = ts.toBarsAndBeats (tr.getPosition());
        barLabel.setText (pos < 0 ? juce::String ("Count-in")
                                  : "Bar " + juce::String (bb.bars + 1) + "." + juce::String (bb.getWholeBeats() + 1),
                          juce::dontSendNotification);
        playButton.setButtonText (tr.isPlaying() ? "Stop" : "Play");
        if (liveImportProgress) status ("Bringing in the live recording: splitting it into a file per input... " + juce::String (juce::roundToInt (liveImportProgress->load() * 100)) + "%");
        if (currentExport) status ("Exporting " + exportingWhat + "... " + (currentExport->finishing > 0 ? juce::String (currentExport->finishing == 1 ? "mastering" : "making the MP3")
                                                                                                    : juce::String (juce::roundToInt (currentExport->progress() * 100)) + "%"));
        recordButton.setToggleState (tr.isRecording(), juce::dontSendNotification);
        clickButton.setToggleState (edit->clickTrackEnabled.get(), juce::dontSendNotification);
        loopButton.setToggleState (tr.looping.get(), juce::dontSendNotification);
        if (! tempoLabel.isBeingEdited())
        {
            SongInfo i;
            i.bpm = ts.getBpmAt (tr.getPosition());
            auto bpm = i.bpmText();
            if (tempoLabel.getText() != bpm) tempoLabel.setText (bpm, juce::dontSendNotification);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
