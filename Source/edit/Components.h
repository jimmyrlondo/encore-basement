/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Modified for Encore: Studio (2026): Encore colours, clip dragging, a bar
    ruler, wheel zoom/scroll, volume and pan on the track headers. Licensed
    under the GPL v3 (or later) as part of Studio (AGPLv3).
*/

#pragma once

namespace IDs
{
    #define DECLARE_ID(name)  const juce::Identifier name (#name);
    DECLARE_ID (EDITVIEWSTATE)
    DECLARE_ID (showMasterTrack)
    DECLARE_ID (showGlobalTrack)
    DECLARE_ID (showMarkerTrack)
    DECLARE_ID (showChordTrack)
    DECLARE_ID (showMidiDevices)
    DECLARE_ID (showWaveDevices)
    DECLARE_ID (viewX1)
    DECLARE_ID (viewX2)
    DECLARE_ID (viewY)
    DECLARE_ID (drawWaveforms)
    DECLARE_ID (showHeaders)
    DECLARE_ID (showFooters)
    DECLARE_ID (showArranger)
    #undef DECLARE_ID
}

namespace te = tracktion;

/** What the arrange view needs from the app around it */
struct StudioHooks
{
    static inline std::function<void()> findPlugins, showPluginManager, applyRecordOffset;
    static inline std::function<String()> pluginScanStatus;     // empty when no scan is running
    static inline juce::ApplicationCommandManager* commands = nullptr;
    static inline std::function<void (PopupMenu&, te::Clip&)> fillClipMenu;
    static inline std::function<bool (const KeyPress&)> forwardKey;   // shortcuts typed in a plugin window still work
    static inline std::function<void (te::MidiClip&)> openPianoRoll;
    /** Right-click on a track (its header, or its lane at a time) */
    static inline std::function<void (PopupMenu&, te::Track&, std::optional<te::TimePosition>)> fillTrackMenu;
    static inline std::function<void()> showAudioSetup;
};

/** "Input channel 1" (what the engine calls an interface's input) as people say it: "Input 1" */
inline String inputLabel (const String& deviceName)
{
    return deviceName.startsWith ("Input channel ") ? "Input " + deviceName.fromFirstOccurrenceOf ("Input channel ", false, false) : deviceName;
}

//==============================================================================
/** The plugins to offer in menus: an older VST2 copy is left out when the same
    plugin is also installed as VST3 (most makers install both) */
inline Array<PluginDescription> pluginsForMenus (te::Engine& engine)
{
    auto all = engine.getPluginManager().knownPluginList.getTypes();
    auto key = [] (const PluginDescription& d)
    {
        return d.name.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789") + "|"
             + d.manufacturerName.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
    };
    // the VST3 when a plugin is there as VST3 and VST2 too, and the 64-bit one when it's also there in 32-bit
    auto key64 = [&key] (const PluginDescription& d) { auto c = d; c.name = c.name.upToLastOccurrenceOf (" (32-bit)", false, false); return key (c); };
    std::set<String> vst3, native;
    for (auto& d : all) if (d.pluginFormatName == "VST3") vst3.insert (key (d));
    for (auto& d : all) if (d.pluginFormatName == "VST3" || d.pluginFormatName == "VST") native.insert (key (d));
    Array<PluginDescription> out;
    for (auto& d : all)
        if (! (d.pluginFormatName == "VST" && vst3.count (key (d)) > 0)
            && ! (d.pluginFormatName.startsWith ("32-bit") && native.count (key64 (d)) > 0))
            out.add (d);
    return out;
}

//==============================================================================
/** Instrument tracks: an instrument plugin first on the track, fed by MIDI */
namespace instruments
{
    /** The MIDI input to play instruments from: all of them ("All MIDI Ins") if there's that, else the first */
    inline te::InputDeviceInstance* midiInput (te::Edit& e)
    {
        te::InputDeviceInstance* first = nullptr;
        for (auto instance : e.getAllInputDevices())
        {
            auto type = instance->getInputDevice().getDeviceType();
            if (type != te::InputDevice::physicalMidiDevice && type != te::InputDevice::virtualMidiDevice) continue;
            if (instance->getInputDevice().getName().containsIgnoreCase ("All MIDI")) return instance;
            if (first == nullptr) first = instance;
        }
        return first;
    }

    inline bool isInstrumentTrack (te::AudioTrack& t)
    {
        for (auto p : t.pluginList.getPlugins()) if (p->isSynth()) return true;
        return false;
    }

    /** Puts the instrument at the start of the track and plays it from MIDI */
    inline void makeInstrumentTrack (te::AudioTrack& t, te::Plugin::Ptr instrument, bool openWindow = true)
    {
        if (instrument == nullptr) return;
        auto& e = t.edit;
        for (auto p : t.pluginList.getPlugins()) if (p->isSynth()) p->deleteFromParent(); // one instrument per track
        t.pluginList.insertPlugin (instrument, 0, nullptr);
        // swap any audio input for the MIDI input
        for (auto instance : e.getAllInputDevices())
            if (te::isOnTargetTrack (*instance, t, 0)) [[maybe_unused]] auto r = instance->removeTarget (t.itemID, &e.getUndoManager());
        if (auto midi = midiInput (e)) [[maybe_unused]] auto r = midi->setTarget (t.itemID, true, &e.getUndoManager(), 0);
        t.state.setProperty ("basementInput", "midi", nullptr);
        if (openWindow) instrument->showWindowExplicitly();
    }
}

//==============================================================================
/** Group tracks: a track that plays nothing of its own; other tracks send their
    output into it (Tracktion's "output to track"), so a whole drum kit or all the
    guitars share one fader, one set of effects and one mute. */
namespace groups
{
    inline bool isGroup (te::AudioTrack& t) { return t.state.getProperty ("basementRole").toString() == "group"; }

    /** An FX track (plays a shared reverb/delay bus): those aren't grouped */
    inline bool isFxTrack (te::AudioTrack& t)
    {
        for (auto p : t.pluginList.getPlugins()) if (dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr) return true;
        return false;
    }

    /** "-> Drums" after a track's input, when it plays into a group */
    inline juce::String arrow() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")); }

    inline juce::Array<te::AudioTrack*> all (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto t : te::getAudioTracks (e)) if (isGroup (*t)) out.add (t);
        return out;
    }

    /** The group a track plays into, or null for the master */
    inline te::AudioTrack* of (te::AudioTrack& t)
    {
        auto d = t.getOutput().getDestinationTrack();
        return d != nullptr && isGroup (*d) ? d : nullptr;
    }

    inline juce::Array<te::AudioTrack*> members (te::AudioTrack& group)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto t : te::getAudioTracks (group.edit)) if (t != &group && of (*t) == &group) out.add (t);
        return out;
    }

    /** Whether t may play into group (not itself, and not a group that already plays into t) */
    inline bool canRoute (te::AudioTrack& t, te::AudioTrack& group)
    {
        return &t != &group && ! group.getOutput().feedsInto (&t);
    }

    /** Plays t into group, or into the master when group is null */
    inline void route (te::AudioTrack& t, te::AudioTrack* group)
    {
        if (group != nullptr && canRoute (t, *group)) t.getOutput().setOutputToTrack (group);
        else t.getOutput().setOutputToDefaultDevice (false);
    }

    /** A new group track (after the last of its members), with those tracks playing into it */
    inline te::AudioTrack* add (te::Edit& e, const juce::String& name, const juce::Array<te::AudioTrack*>& tracksToGroup)
    {
        auto all = te::getAudioTracks (e);
        te::Track* after = all.isEmpty() ? nullptr : all.getLast();
        if (! tracksToGroup.isEmpty())
        {
            int last = -1;
            for (auto* t : tracksToGroup) last = juce::jmax (last, all.indexOf (t));
            if (last >= 0) after = all[last];
        }
        auto g = e.insertNewAudioTrack (te::TrackInsertPoint (nullptr, after), nullptr);
        if (g == nullptr) return nullptr;
        g->setName (name);
        g->setColour (theme::accent);
        g->state.setProperty ("basementInput", "none", nullptr);
        g->state.setProperty ("basementRole", "group", nullptr);
        for (auto* t : tracksToGroup) if (t != nullptr && t != g.get()) route (*t, g.get());
        return g.get();
    }

    /** Every member back to the master */
    inline void ungroup (te::AudioTrack& group)
    {
        for (auto* t : members (group)) route (*t, nullptr);
    }
}

//==============================================================================
/** Song markers (Intro, Verse, Chorus...): saved in the song as BASEMENTMARKERS,
    each MARKER with a time in seconds and a name. Changes are undoable. */
namespace markers
{
    inline ValueTree list (te::Edit& e)  { return e.state.getOrCreateChildWithName ("BASEMENTMARKERS", nullptr); }

    inline ValueTree add (te::Edit& e, te::TimePosition t, const String& name = {})
    {
        auto l = list (e);
        ValueTree m ("MARKER");
        m.setProperty ("time", t.inSeconds(), nullptr);
        m.setProperty ("name", name.isNotEmpty() ? name : "Marker " + String (l.getNumChildren() + 1), nullptr);
        e.getUndoManager().beginNewTransaction ("Add marker");
        l.appendChild (m, &e.getUndoManager());
        return m;
    }

    inline te::TimePosition timeOf (const ValueTree& m) { return te::TimePosition::fromSeconds ((double) m.getProperty ("time")); }

    /** The marker after (dir 1) or before (dir -1) t, or t itself if there isn't one */
    inline te::TimePosition next (te::Edit& e, te::TimePosition t, int dir)
    {
        std::optional<te::TimePosition> best;
        for (auto m : list (e))
        {
            auto mt = timeOf (m);
            if (dir > 0 ? mt > t + te::TimeDuration::fromSeconds (0.001) : mt < t - te::TimeDuration::fromSeconds (0.001))
                if (! best || (dir > 0 ? mt < *best : mt > *best)) best = mt;
        }
        if (! best && dir < 0) return te::TimePosition();
        return best.value_or (t);
    }

    inline StringArray quickNames() { return { "Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Solo", "Breakdown", "Outro" }; }
}

//==============================================================================
/** Tempo and time-signature changes partway through a song. The first tempo and time signature
    are the song's own (Song Setup, the toolbar); bar numbers and the bar grid follow every change.
    A tempo either jumps at its change or ramps there from the one before (Tracktion's curve on
    the earlier setting: 1 holds it until the change, 0 is a straight ramp). */
namespace tempos
{
    inline te::TimePosition barStart (te::TempoSequence& ts, int bar) { return ts.toTime (te::tempo::BarsAndBeats { jmax (0, bar), {} }); }
    inline int barAt (te::TempoSequence& ts, te::TimePosition t)       { return ts.toBarsAndBeats (t).bars; }

    /** The nearest bar line to t */
    inline te::TimePosition nearestBar (te::TempoSequence& ts, te::TimePosition t)
    {
        const int b = barAt (ts, t);
        auto a = barStart (ts, b), n = barStart (ts, b + 1);
        return (t - a) <= (n - t) ? a : n;
    }

    struct Change
    {
        te::TimePosition time;
        double bpm = 0;           // 0: the tempo carries on
        bool ramp = false;        // ramps to bpm from the tempo before
        int num = 0, den = 0;     // 0: the time signature carries on
        String text() const
        {
            String s;
            if (bpm > 0) { auto b = String (bpm, 1); s << (ramp ? "-> " : "") << (b.endsWith (".0") ? b.dropLastCharacters (2) : b); }
            if (num > 0) s << (s.isEmpty() ? "" : "  ") << num << "/" << den;
            return s;
        }
    };

    /** Every change after the start, in time order (a tempo and a time signature in the same place are one) */
    inline Array<Change> list (te::Edit& e)
    {
        auto& ts = e.tempoSequence;
        std::map<double, Change> at;   // by beat
        const auto& t = ts.getTempos();
        for (int i = 1; i < t.size(); ++i)
        {
            auto& c = at[t[i]->getStartBeat().inBeats()];
            c.time = t[i]->getStartTime();
            c.bpm = t[i]->getBpm();
            c.ramp = t[i - 1]->getCurve() < 0.5f;
        }
        const auto& s = ts.getTimeSigs();
        for (int i = 1; i < s.size(); ++i)
        {
            auto& c = at[s[i]->getStartBeat().inBeats()];
            c.time = ts.toTime (s[i]->getStartBeat());
            c.num = s[i]->numerator; c.den = s[i]->denominator;
        }
        Array<Change> out;
        for (auto& [beat, c] : at) out.add (c);
        return out;
    }

    /** Adds or changes the tempo and/or time signature at t (a time signature lands on the nearest bar line).
        One undo step. */
    inline void set (te::Edit& e, te::TimePosition t, double bpm, bool ramp, int num, int den)
    {
        auto& ts = e.tempoSequence;
        auto* um = &e.getUndoManager();
        um->beginNewTransaction ("Tempo change");
        if (num > 0 && den > 0)
        {
            t = nearestBar (ts, t);
            const auto beat = ts.toBeats (t);
            te::TimeSigSetting::Ptr sig = &ts.getTimeSigAt (beat);
            if (std::abs ((sig->getStartBeat() - beat).inBeats()) > 0.001 || ts.indexOfTimeSig (sig.get()) == 0) sig = ts.insertTimeSig (t);
            sig->numerator = num;
            sig->denominator = den;
        }
        if (bpm > 0)
        {
            const auto beat = te::roundToNearestBeat (ts.toBeats (t));
            te::TempoSetting::Ptr tempo = &ts.getTempoAt (beat);
            if (std::abs ((tempo->getStartBeat() - beat).inBeats()) > 0.001 || ts.indexOfTempo (tempo.get()) == 0)
                tempo = ts.insertTempo (beat, bpm, 1.0f);
            tempo->setBpm (bpm);
            const int i = ts.indexOfTempo (tempo.get());
            if (i > 0) ts.getTempo (i - 1)->setCurve (ramp ? 0.0f : 1.0f);
        }
    }

    /** Takes out the change at t (never the song's first tempo or time signature) */
    inline void remove (te::Edit& e, te::TimePosition t)
    {
        auto& ts = e.tempoSequence;
        e.getUndoManager().beginNewTransaction ("Remove tempo change");
        const auto beat = ts.toBeats (t).inBeats();
        for (int i = ts.getNumTempos(); --i > 0;)
            if (std::abs (ts.getTempo (i)->getStartBeat().inBeats() - beat) < 0.01)
            {
                ts.getTempo (i - 1)->setCurve (1.0f);
                ts.removeTempo (i, false);
            }
        for (int i = ts.getNumTimeSigs(); --i > 0;)
            if (std::abs (ts.getTimeSig (i)->getStartBeat().inBeats() - beat) < 0.01) ts.removeTimeSig (i);
    }
}

//==============================================================================
/** Audio that follows the song's tempo: a loop made at another tempo plays in time, and a
    following clip speeds up or slows down with every tempo change (SoundTouch keeps the pitch). */
namespace stretch
{
    /** A tempo written in a file's name ("Beat 92bpm", "loop_120_BPM", "Drums - 140 bpm"), else 0 */
    inline double tempoInName (const String& name)
    {
        const auto n = name.toLowerCase();
        for (int at = n.indexOf ("bpm"); at >= 0; at = n.indexOf (at + 3, "bpm"))
        {
            int end = at;
            while (end > 0 && (n[end - 1] == ' ' || n[end - 1] == '_' || n[end - 1] == '-')) --end;
            int start = end;
            while (start > 0 && (CharacterFunctions::isDigit (n[start - 1]) || n[start - 1] == '.')) --start;
            const double bpm = n.substring (start, end).getDoubleValue();
            if (bpm >= 40 && bpm <= 300) return bpm;
        }
        return 0;
    }

    /** The clip plays as audio at sourceBpm, kept in time with the song from now on */
    inline void follow (te::WaveAudioClip& c, double sourceBpm)
    {
        if (c.getAutoTempo()) c.setAutoTempo (false);
        const double sourceSeconds = c.getPosition().getLength().inSeconds() * c.getSpeedRatio();   // how much of the audio the clip plays
        auto info = c.getAudioFile().getInfo();
        auto li = c.getLoopInfo();
        li.setBpm (sourceBpm, info);
        c.setLoopInfo (li);
        c.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
        c.setAutoTempo (true);
        // that much audio is sourceSeconds * bpm / 60 beats: the clip lasts that many of the song's beats
        auto& ts = c.edit.tempoSequence;
        const auto start = c.getPosition().getStart();
        const auto end = ts.toTime (ts.toBeats (start) + te::BeatDuration::fromBeats (sourceSeconds * sourceBpm / 60.0));
        c.setLength (end - start, true);
    }
}

//==============================================================================
void drawMidiClip (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>, te::TimeRange);

//==============================================================================
class EditViewState
{
public:
    EditViewState (te::Edit& e, te::SelectionManager& s)
        : edit (e), selectionManager (s)
    {
        state = edit.state.getOrCreateChildWithName (IDs::EDITVIEWSTATE, nullptr);

        UndoManager* um = nullptr; // zooming, scrolling and view options aren't edits: keep them out of undo

        showMasterTrack.referTo (state, IDs::showMasterTrack, um, false);
        showGlobalTrack.referTo (state, IDs::showGlobalTrack, um, false);
        showMarkerTrack.referTo (state, IDs::showMarkerTrack, um, false);
        showChordTrack.referTo (state, IDs::showChordTrack, um, false);
        showArrangerTrack.referTo (state, IDs::showArranger, um, false);
        drawWaveforms.referTo (state, IDs::drawWaveforms, um, true);
        showHeaders.referTo (state, IDs::showHeaders, um, true);
        showFooters.referTo (state, IDs::showFooters, um, true);
        showMidiDevices.referTo (state, IDs::showMidiDevices, um, true);
        showWaveDevices.referTo (state, IDs::showWaveDevices, um, true);

        viewX1.referTo (state, IDs::viewX1, um, 0s);
        viewX2.referTo (state, IDs::viewX2, um, 60s);
        viewY.referTo (state, IDs::viewY, um, 0);
        snapMode.referTo (state, "snapMode", um, 2);
        automationMode.referTo (state, "automationMode", um, 0);
        waveformZoom.referTo (state, "waveformZoom", um, 1.0f);
    }

    /** How tall waveforms are drawn (1 = true size; more to see quiet recordings) */
    CachedValue<float> waveformZoom;

    /** Automation shown on the tracks: 0 none, 1 volume, 2 pan */
    CachedValue<int> automationMode;

    /** Snap: 0 off, 1 bar, 2 beat, 3 half beat, 4 quarter beat */
    CachedValue<int> snapMode;
    static StringArray snapNames()                 { return { "Off", "Bar", "Beat", "1/8", "1/16" }; }

    /** The grid step in beats (a beat when snap is off, for nudging) */
    double gridBeats() const
    {
        const int num = jmax (1, edit.tempoSequence.getTimeSig (0)->numerator.get());
        switch (snapMode.get()) { case 1: return num; case 3: return 0.5; case 4: return 0.25; default: return 1.0; }
    }

    /** t snapped to the grid (unless snap is off or bypassed, e.g. with Shift held) */
    te::TimePosition snap (te::TimePosition t, bool bypass = false) const
    {
        if (bypass || snapMode.get() == 0) return t;
        auto& ts = edit.tempoSequence;
        if (snapMode.get() == 1) { const int b = ts.toBarsAndBeats (t).bars; auto a = ts.toTime (te::tempo::BarsAndBeats { b, {} }), n = ts.toTime (te::tempo::BarsAndBeats { b + 1, {} }); return (t - a) <= (n - t) ? a : n; }
        const double g = gridBeats(), beats = ts.toBeats (t).inBeats();
        return ts.toTime (te::BeatPosition::fromBeats (jmax (0.0, std::round (beats / g) * g)));
    }

    int timeToX (te::TimePosition time, int width) const
    {
        return roundToInt (((time - viewX1) * width) / (viewX2 - viewX1));
    }

    te::TimePosition xToTime (int x, int width) const
    {
        return toPosition ((viewX2 - viewX1) * (double (x) / width)) + toDuration (viewX1.get());
    }

    te::TimePosition beatToTime (te::BeatPosition b) const
    {
        auto& ts = edit.tempoSequence;
        return ts.toTime (b);
    }

    te::Edit& edit;
    te::SelectionManager& selectionManager;

    CachedValue<bool> showMasterTrack, showGlobalTrack, showMarkerTrack, showChordTrack, showArrangerTrack,
                      drawWaveforms, showHeaders, showFooters, showMidiDevices, showWaveDevices;

    CachedValue<te::TimePosition> viewX1, viewX2;
    CachedValue<double> viewY;

    ValueTree state;
};

//==============================================================================
class ClipComponent : public Component
{
public:
    ClipComponent (EditViewState&, te::Clip::Ptr);

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseExit (const MouseEvent& e) override;

    te::Clip& getClip() { return *clip; }

protected:
    EditViewState& editViewState;
    te::Clip::Ptr clip;
    te::TimePosition dragStart;
    bool dragging = false;

    // what a drag does, decided where the mouse went down
    enum class Zone { body, trimStart, trimEnd, fadeIn, fadeOut };
    Zone zoneAt (Point<int>) const;
    Zone dragZone = Zone::body;
    te::ClipPosition posAtDragStart;
    te::TimeDuration fadeAtDragStart;
    std::vector<std::pair<te::Clip::Ptr, te::TimePosition>> groupAtDragStart;   // every selected clip and where it started
    te::AudioTrack* dropTrack = nullptr;
    bool hovering = false;
};

//==============================================================================
class AudioClipComponent : public ClipComponent
{
public:
    AudioClipComponent (EditViewState&, te::Clip::Ptr);

    te::WaveAudioClip* getWaveAudioClip() { return dynamic_cast<te::WaveAudioClip*> (clip.get()); }

    void paint (Graphics& g) override;

private:
    void updateThumbnail();
    void drawWaveform (Graphics& g, te::AudioClipBase& c, te::SmartThumbnail& thumb, Colour colour,
                       int left, int right, int y, int h, int xOffset);
    void drawChannels (Graphics& g, te::SmartThumbnail& thumb, Rectangle<int> area,
                       te::TimeRange time, bool useLeft, bool useRight,
                       float leftGain, float rightGain);

    std::unique_ptr<te::SmartThumbnail> thumbnail;
};

//==============================================================================
class MidiClipComponent : public ClipComponent
{
public:
    MidiClipComponent (EditViewState&, te::Clip::Ptr);

    te::MidiClip* getMidiClip() { return dynamic_cast<te::MidiClip*> (clip.get()); }

    void paint (Graphics& g) override;
    void mouseDoubleClick (const MouseEvent&) override;   // opens the piano roll
};

//==============================================================================
class RecordingClipComponent : public Component,
                               private Timer
{
public:
    RecordingClipComponent (te::Track::Ptr t, EditViewState&);

    void paint (Graphics& g) override;

private:
    void timerCallback() override;
    void updatePosition();
    void initialiseThumbnailAndPunchTime();
    void drawThumbnail (Graphics& g, Colour waveformColour) const;
    bool getBoundsAndTime (juce::Rectangle<int>& bounds, tracktion::TimeRange& times) const;

    te::Track::Ptr track;
    EditViewState& editViewState;

    te::RecordingThumbnailManager::Thumbnail::Ptr thumbnail;
    te::TimePosition punchInTime = -1.0s;
};

//==============================================================================
class TrackHeaderComponent : public Component,
                             private te::ValueTreeAllEventListener,
                             private Timer
{
public:
    TrackHeaderComponent (EditViewState&, te::Track::Ptr);
    ~TrackHeaderComponent() override;

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void resized() override;

    te::Track* getTrack() const { return track.get(); }

private:
    void valueTreeChanged() override {}
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    EditViewState& editViewState;
    te::Track::Ptr track;

    void showInputMenu();
    void selectFromClick (const MouseEvent&);
    void logMonitoring (te::AudioTrack&, bool on);

    // drag a track up or down to move it
    bool reordering = false;
    static inline Component::SafePointer<TrackHeaderComponent> dropTarget;
    static inline bool dropAbove = false;
    TrackHeaderComponent* headerAt (Point<int> screenPos) const;

    ValueTree inputsState;
    Label trackName;
    TextButton armButton {"R"}, muteButton {"M"}, soloButton {"S"}, inputButton {"In"};

    LevelMeter meter;
    void updateMeterSource();

    /** Input monitoring: hear what's coming into the track's input (headphones icon) */
    struct MonitorButton : public Button
    {
        MonitorButton() : Button ("Monitor") {}
        void paintButton (Graphics&, bool over, bool down) override;
    } monitorButton;
    void updateMonitor();

    // drag the bottom edge to change the track's height
    bool resizing = false;
    int heightAtDragStart = 0;
    bool overResizeEdge (const MouseEvent& e) const { return e.y >= getHeight() - 6; }
    Slider volume { Slider::LinearHorizontal, Slider::NoTextBox }, pan { Slider::LinearHorizontal, Slider::NoTextBox };
    void updateInputName();
    void timerCallback() override;
};

//==============================================================================
class PluginComponent : public TextButton
{
public:
    PluginComponent (EditViewState&, te::Plugin::Ptr);
    ~PluginComponent() override;

    using TextButton::clicked;
    void clicked (const ModifierKeys& modifiers) override;
    void paintButton (Graphics&, bool over, bool down) override;

private:
    EditViewState& editViewState;
    te::Plugin::Ptr plugin;
};

//==============================================================================
class TrackFooterComponent : public Component,
                             private FlaggedAsyncUpdater,
                             private te::ValueTreeAllEventListener
{
public:
    TrackFooterComponent (EditViewState&, te::Track::Ptr);
    ~TrackFooterComponent() override;

    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void resized() override;

private:
    void valueTreeChanged() override {}
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

    void handleAsyncUpdate() override;

    void buildPlugins();

    EditViewState& editViewState;
    te::Track::Ptr track;

    TextButton addButton {"+"};
    OwnedArray<PluginComponent> plugins;

    bool updatePlugins = false;
};

//==============================================================================
class TrackComponent : public Component,
                       private te::ValueTreeAllEventListener,
                       private FlaggedAsyncUpdater,
                       private ChangeListener
{
public:
    TrackComponent (EditViewState&, te::Track::Ptr);
    ~TrackComponent() override;

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseDoubleClick (const MouseEvent& e) override;
    void paintOverChildren (Graphics&) override;
    void resized() override;
    te::Track* getTrack() const     { return track.get(); }

    /** The parameter being drawn on this track (volume or pan), if automation is shown */
    te::AutomatableParameter* automationParam() const;
    void updateClipInterception();
    const OwnedArray<ClipComponent>& getClipComponents() const { return clips; }

private:
    void changeListenerCallback (ChangeBroadcaster*) override;

    void valueTreeChanged() override {}

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

    void handleAsyncUpdate() override;

    void buildClips();
    void buildRecordClips();

    EditViewState& editViewState;
    te::Track::Ptr track;

    OwnedArray<ClipComponent> clips;
    int draggingPoint = -1;
    float valueAtY (int y) const;
    int yForValue (float v) const;
    int pointNear (Point<int>) const;
    std::unique_ptr<RecordingClipComponent> recordingClip;

    bool updateClips = false, updatePositions = false, updateRecordClips = false;
};

//==============================================================================
class PlayheadComponent : public Component,
                          private Timer
{
public:
    PlayheadComponent (te::Edit&, EditViewState&);

    void paint (Graphics& g) override;
    bool hitTest (int x, int y) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;

private:
    void timerCallback() override;

    te::Edit& edit;
    EditViewState& editViewState;

    int xPosition = 0;
    bool firstTimer = true;
};

//==============================================================================
/** Bars along the top of the timeline; click or drag to move the playhead */
class TimeRulerComponent : public Component,
                           private Timer
{
public:
    TimeRulerComponent (te::Edit& e, EditViewState& evs) : edit (e), editViewState (evs) { startTimerHz (10); }
    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent&) override       { draggedMarker = {}; }
    void mouseDoubleClick (const MouseEvent& e) override;
    static constexpr int markerLaneH = 18, tempoTagOffset = 22;   // tempo tags sit after the bar number
private:
    void timerCallback() override { repaint(); }
    ValueTree markerAt (int x) const;
    void renameMarker (ValueTree m);
public:
    void editTempoChange (te::TimePosition t);
private:
    std::optional<tempos::Change> tempoChangeAt (int x) const;
    te::Edit& edit;
    EditViewState& editViewState;
    ValueTree draggedMarker;
    bool movedMarker = false;
};

//==============================================================================
class EditComponent : public Component,
                      private te::ValueTreeAllEventListener,
                      private FlaggedAsyncUpdater,
                      private ChangeListener
{
public:
    EditComponent (te::Edit&, te::SelectionManager&);
    ~EditComponent() override;

    EditViewState& getEditViewState()   { return editViewState; }
    TimeRulerComponent& getRuler()      { return ruler; }

    /** Track heights: each track keeps its own (dragged on its header, or set
        for all from Window > Track Height) */
    static int heightOf (const te::Track& t)    { return jlimit (minTrackHeight, maxTrackHeight, (int) t.state.getProperty ("basementHeight", trackHeight)); }
    static constexpr int minTrackHeight = 40, maxTrackHeight = 400;
    void relayout()                             { resized(); repaint(); }

    /** Box select: drag across empty lanes to select every clip it touches (Shift adds) */
    void lassoDrag (Rectangle<int> areaInThis, bool add);
    void lassoEnd()                             { lasso = {}; repaint(); }
    void paintOverChildren (Graphics&) override;

    /** The audio track under a point in this component, if any */
    te::AudioTrack* audioTrackAt (Point<int>) const;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
    void paint (Graphics&) override;

    static constexpr int rulerHeight = 46, trackHeight = 88, trackGap = 1, headerWidth = 250, footerWidth = 170;

private:
    void valueTreeChanged() override {}

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

    void handleAsyncUpdate() override;
    void resized() override;

    void changeListenerCallback (ChangeBroadcaster*) override { repaint(); }


    void buildTracks();

    te::Edit& edit;

    EditViewState editViewState;

    PlayheadComponent playhead {edit, editViewState};
    Rectangle<int> lasso;
    TimeRulerComponent ruler {edit, editViewState};
    OwnedArray<TrackComponent> tracks;
    OwnedArray<TrackHeaderComponent> headers;
    OwnedArray<TrackFooterComponent> footers;

    bool updateTracks = false, updateZoom = false;
};

#include "Components.cpp"
