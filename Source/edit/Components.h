/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Modified for Encore: Basement (2026): Encore colours, clip dragging, a bar
    ruler, wheel zoom/scroll, volume and pan on the track headers. Licensed
    under the GPL v3 (or later) as part of Basement (AGPLv3).
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
struct BasementHooks
{
    static inline std::function<void()> findPlugins, showPluginManager, applyRecordOffset;
    static inline std::function<String()> pluginScanStatus;     // empty when no scan is running
    static inline juce::ApplicationCommandManager* commands = nullptr;
    static inline std::function<void (PopupMenu&, te::Clip&)> fillClipMenu;
    static inline std::function<bool (const KeyPress&)> forwardKey;   // shortcuts typed in a plugin window still work
};

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
    inline void makeInstrumentTrack (te::AudioTrack& t, te::Plugin::Ptr instrument)
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
        instrument->showWindowExplicitly();
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

private:
    void valueTreeChanged() override {}
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    EditViewState& editViewState;
    te::Track::Ptr track;

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
    static constexpr int markerLaneH = 18;
private:
    void timerCallback() override { repaint(); }
    ValueTree markerAt (int x) const;
    void renameMarker (ValueTree m);
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
