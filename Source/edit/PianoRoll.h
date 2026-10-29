// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The piano roll: edit the notes of a MIDI clip in a pane under the arrange
// view (double-click a MIDI clip, or MIDI > Open Piano Roll, Ctrl+E).
//
//   click an empty spot      adds a note (on the grid)
//   drag an empty spot       selects the notes inside the box
//   drag a note              moves it (and the other selected notes) in time and pitch
//   drag a note's right end  makes it longer or shorter
//   double-click a note      deletes it (Delete deletes the selection)
//   up / down                transposes the selection (Shift: an octave)
//   left / right             moves it by one grid step
//   Q                        quantizes the selection (or every note) to the grid
//   wheel                    scrolls up and down, Shift+wheel sideways, Ctrl+wheel zooms
//   the strip at the bottom  sets velocities (drag across the bars)
//
// Every change goes through the song's undo, so Ctrl+Z works as everywhere else.
#pragma once

/** Note edits shared by the piano roll and the MIDI menu */
namespace midiedit
{
    /** Grid sizes in beats (quarter notes) */
    struct Grid { const char* name; double beats; };
    inline const std::array<Grid, 6>& grids()
    {
        static const std::array<Grid, 6> g { { { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 },
                                               { "1/8 triplet", 1.0 / 3.0 }, { "1/16 triplet", 1.0 / 6.0 } } };
        return g;
    }

    /** A note's start as a beat of the song (the clip's notes are counted from its content start) */
    inline double songBeat (te::MidiClip& c, const te::MidiNote& n) { return c.getContentStartBeat().inBeats() + n.getStartBeat().inBeats(); }

    inline void quantize (te::MidiClip& c, const juce::Array<te::MidiNote*>& notes, double grid)
    {
        auto* um = &c.edit.getUndoManager();
        const double content = c.getContentStartBeat().inBeats();
        for (auto* n : notes)
        {
            const double snapped = std::round ((content + n->getStartBeat().inBeats()) / grid) * grid;
            n->setStartAndLength (te::BeatPosition::fromBeats (snapped - content), n->getLengthBeats(), um);
        }
    }

    inline void transpose (te::MidiClip& c, const juce::Array<te::MidiNote*>& notes, int semitones)
    {
        auto* um = &c.edit.getUndoManager();
        for (auto* n : notes) n->setNoteNumber (juce::jlimit (0, 127, n->getNoteNumber() + semitones), um);
    }

    /** to >= 0 sets every velocity to it; otherwise each one changes by delta */
    inline void velocity (te::MidiClip& c, const juce::Array<te::MidiNote*>& notes, int to, int delta = 0)
    {
        auto* um = &c.edit.getUndoManager();
        for (auto* n : notes) n->setVelocity (juce::jlimit (1, 127, to >= 0 ? to : n->getVelocity() + delta), um);
    }

    inline juce::Array<te::MidiNote*> allNotes (te::MidiClip& c)
    {
        juce::Array<te::MidiNote*> out;
        for (auto* n : c.getSequence().getNotes()) out.add (n);
        return out;
    }

    inline juce::String noteName (int n)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
    }

    inline bool isBlackKey (int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
}

//==============================================================================
/** The pane under the arrange view that edits a MIDI clip: the piano roll, or for drum tracks the drum grid */
class NoteEditor : public juce::Component
{
public:
    std::function<void()> onClose;
    std::function<void()> onSwitchView;   // piano roll <-> drum grid
    virtual te::MidiClip* getClip() const = 0;
    /** The notes the MIDI menu should act on */
    virtual juce::Array<te::MidiNote*> targetNotes() = 0;
    virtual double gridBeats() const = 0;
};

class PianoRoll : public NoteEditor,
                  private juce::Timer
{
public:
    PianoRoll (te::MidiClip& c) : clip (&c)
    {
        setWantsKeyboardFocus (true);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::paper);
        addAndMakeVisible (title);

        for (int i = 0; i < (int) midiedit::grids().size(); ++i) gridBox.addItem (midiedit::grids()[(size_t) i].name, i + 1);
        gridBox.setSelectedId (3, juce::dontSendNotification);   // 1/16
        gridBox.setTooltip ("Grid: where notes snap, and what Quantize lines them up to");
        gridBox.onChange = [this] { repaint(); };
        addAndMakeVisible (gridBox);

        quantizeButton.setTooltip ("Line the selected notes (or all of them) up with the grid (Q)");
        quantizeButton.onClick = [this] { quantizeSelection(); };
        addAndMakeVisible (quantizeButton);

        drumsButton.setTooltip ("Edit these notes in the drum grid instead");
        drumsButton.onClick = [this] { if (onSwitchView) onSwitchView(); };
        if (auto* at = dynamic_cast<te::AudioTrack*> (c.getTrack()); at != nullptr && at->state.getProperty ("basementRole").toString() == "drums")
            addAndMakeVisible (drumsButton);
        drumsButton.setMouseClickGrabsKeyboardFocus (false);

        closeButton.setTooltip ("Close the piano roll (Esc)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        for (auto* b : std::initializer_list<juce::Component*> { &gridBox, &quantizeButton, &closeButton })
            b->setMouseClickGrabsKeyboardFocus (false);

        fitToClip();
        startTimerHz (20);
    }

    ~PianoRoll() override { stopAudition(); }

    te::MidiClip* getClip() const override { return clip.get(); }

    /** The notes the MIDI menu should act on: the selection, or all of them */
    juce::Array<te::MidiNote*> targetNotes() override
    {
        pruneSelection();
        return selection.isEmpty() && clip != nullptr ? midiedit::allNotes (*clip) : selection;
    }

    double gridBeats() const override { return midiedit::grids()[(size_t) juce::jlimit (0, 5, gridBox.getSelectedId() - 1)].beats; }

    void quantizeSelection()
    {
        if (clip == nullptr) return;
        auto notes = targetNotes();
        clip->edit.getUndoManager().beginNewTransaction ("Quantize notes");
        midiedit::quantize (*clip, notes, gridBeats());
        repaint();
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::base);
        g.setColour (theme::surface);
        g.fillRect (getLocalBounds().removeFromTop (headerH));
        g.setColour (theme::strong);
        g.fillRect (getLocalBounds().removeFromTop (1));
        if (clip == nullptr) return;

        paintKeyboard (g);
        paintGrid (g);
        paintNotes (g);
        paintVelocity (g);
        paintPlayhead (g);
    }

    void resized() override
    {
        if (! fitted && notesArea().getHeight() > rowH * 4) { fitted = true; centreOnNotes(); }
        auto r = getLocalBounds().removeFromTop (headerH).reduced (10, 5);
        closeButton.setBounds (r.removeFromRight (28));
        r.removeFromRight (10);
        quantizeButton.setBounds (r.removeFromRight (84));
        r.removeFromRight (6);
        if (drumsButton.isVisible()) { drumsButton.setBounds (r.removeFromRight (90)); r.removeFromRight (6); }
        gridBox.setBounds (r.removeFromRight (120));
        r.removeFromRight (10);
        title.setBounds (r);
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (clip == nullptr) return;
        grabKeyboardFocus();
        const auto p = e.getPosition();
        dragMode = DragMode::none;
        dragStart = p;
        if (keyboardArea().contains (p)) { audition (noteAtY (p.y)); return; }
        if (velocityArea().contains (p)) { dragMode = DragMode::velocity; setVelocityAt (p, true); return; }
        if (! notesArea().contains (p)) return;

        pruneSelection();
        if (auto* n = noteAt (p))
        {
            if (e.mods.isPopupMenu()) { noteMenu (n); return; }
            if (e.mods.isShiftDown() || e.mods.isCommandDown())
            {
                if (selection.contains (n)) selection.removeFirstMatchingValue (n);
                else selection.add (n);
            }
            else if (! selection.contains (n)) selection = { n };
            dragMode = nearRightEdge (*n, p) ? DragMode::resize : DragMode::move;
            clip->edit.getUndoManager().beginNewTransaction (dragMode == DragMode::resize ? "Resize notes" : "Move notes");
            original.clear();
            for (auto* s : selection) original.push_back ({ s, s->getStartBeat().inBeats(), s->getLengthBeats().inBeats(), s->getNoteNumber() });
            audition (n->getNoteNumber(), n->getVelocity());
        }
        else
        {
            if (! (e.mods.isShiftDown() || e.mods.isCommandDown())) selection.clear();
            dragMode = DragMode::lasso;
            lasso = {};
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (clip == nullptr) return;
        const auto p = e.getPosition();
        auto* um = &clip->edit.getUndoManager();
        const double grid = gridBeats();
        const double content = clip->getContentStartBeat().inBeats();
        switch (dragMode)
        {
            case DragMode::move:
            {
                // time moves in grid steps (Alt: freely), pitch in semitones
                double deltaBeats = (p.x - dragStart.x) / pxPerBeat;
                if (! e.mods.isAltDown() && ! original.empty())
                {
                    const double firstStart = content + original.front().start;
                    deltaBeats = std::round ((firstStart + deltaBeats) / grid) * grid - firstStart;
                }
                const int deltaPitch = noteAtY (p.y) - noteAtY (dragStart.y);
                for (auto& o : original)
                {
                    o.note->setStartAndLength (te::BeatPosition::fromBeats (juce::jmax (0.0, o.start + deltaBeats)), te::BeatDuration::fromBeats (o.length), um);
                    o.note->setNoteNumber (juce::jlimit (0, 127, o.pitch + deltaPitch), um);
                }
                if (! original.empty() && original.front().note->getNoteNumber() != lastAuditioned)
                    audition (original.front().note->getNoteNumber(), original.front().note->getVelocity());
                break;
            }
            case DragMode::resize:
            {
                const double deltaBeats = (p.x - dragStart.x) / pxPerBeat;
                for (auto& o : original)
                {
                    double end = content + o.start + o.length + deltaBeats;
                    if (! e.mods.isAltDown()) end = std::round (end / grid) * grid;
                    const double length = juce::jmax (e.mods.isAltDown() ? 1.0 / 64.0 : grid, end - content - o.start);
                    o.note->setStartAndLength (te::BeatPosition::fromBeats (o.start), te::BeatDuration::fromBeats (length), um);
                    lastLength = length;
                }
                break;
            }
            case DragMode::lasso:
            {
                lasso = juce::Rectangle<int> (dragStart, p).getIntersection (notesArea());
                selection.clear();
                for (auto* n : clip->getSequence().getNotes())
                    if (noteBounds (*n).intersects (lasso)) selection.add (n);
                break;
            }
            case DragMode::velocity: setVelocityAt (p, false); break;
            case DragMode::none: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (clip == nullptr) return;
        // a click (not a drag) on an empty spot adds a note there
        if (dragMode == DragMode::lasso && e.getDistanceFromDragStart() < 3 && ! e.mods.isPopupMenu() && notesArea().contains (e.getPosition()))
            addNoteAt (e.getPosition());
        if (dragMode == DragMode::lasso) lasso = {};
        dragMode = DragMode::none;
        original.clear();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (clip == nullptr) return;
        if (auto* n = noteAt (e.getPosition()))
        {
            clip->edit.getUndoManager().beginNewTransaction ("Delete note");
            selection.removeFirstMatchingValue (n);
            clip->getSequence().removeNote (*n, &clip->edit.getUndoManager());
            repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto* n = noteAt (e.getPosition());
        setMouseCursor (n != nullptr && nearRightEdge (*n, e.getPosition()) ? juce::MouseCursor::LeftRightResizeCursor
                        : n != nullptr ? juce::MouseCursor::DraggingHandCursor
                        : notesArea().contains (e.getPosition()) ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown())
        {
            // zoom around the mouse
            const double beatAtMouse = beatAtX (e.x);
            pxPerBeat = juce::jlimit (8.0, 600.0, pxPerBeat * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
            viewStartBeat = beatAtMouse - (e.x - notesArea().getX()) / pxPerBeat;
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
            viewStartBeat -= (std::abs (w.deltaX) > 0 ? w.deltaX : w.deltaY) * 8.0 * 60.0 / pxPerBeat;
        else
            topNote = juce::jlimit (visibleRows(), 127, topNote + (w.deltaY > 0 ? 3 : -3));
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (clip == nullptr) return false;
        pruneSelection();
        auto* um = &clip->edit.getUndoManager();
        const auto code = k.getKeyCode();
        if (code == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && ! selection.isEmpty())
        {
            um->beginNewTransaction ("Delete notes");
            for (auto* n : selection) clip->getSequence().removeNote (*n, um);
            selection.clear();
            repaint();
            return true;
        }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
        {
            selection = midiedit::allNotes (*clip);
            repaint();
            return true;
        }
        if ((code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) && ! selection.isEmpty())
        {
            um->beginNewTransaction ("Transpose notes");
            midiedit::transpose (*clip, selection, (code == juce::KeyPress::upKey ? 1 : -1) * (k.getModifiers().isShiftDown() ? 12 : 1));
            audition (selection.getFirst()->getNoteNumber(), selection.getFirst()->getVelocity());
            repaint();
            return true;
        }
        if ((code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) && ! selection.isEmpty())
        {
            um->beginNewTransaction ("Move notes");
            const double step = (code == juce::KeyPress::rightKey ? 1.0 : -1.0) * gridBeats();
            for (auto* n : selection)
                n->setStartAndLength (te::BeatPosition::fromBeats (juce::jmax (0.0, n->getStartBeat().inBeats() + step)), n->getLengthBeats(), um);
            repaint();
            return true;
        }
        if (k == juce::KeyPress ('q', juce::ModifierKeys(), 0)) { quantizeSelection(); return true; }
        return false;   // everything else (Space, Ctrl+Z...) goes to the app
    }

private:
    static constexpr int headerH = 34, keyboardW = 58, velocityH = 64;
    te::MidiClip::Ptr clip;
    juce::Label title;
    juce::ComboBox gridBox;
    juce::TextButton quantizeButton { "Quantize" }, closeButton { "X" };
    juce::TextButton drumsButton { "Drum grid" };

    double pxPerBeat = 60.0, viewStartBeat = 0.0, lastLength = 1.0;
    int topNote = 84, rowH = 14;
    bool fitted = false;   // centred on the notes once the pane has a size
    juce::Array<te::MidiNote*> selection;

    enum class DragMode { none, move, resize, lasso, velocity };
    DragMode dragMode = DragMode::none;
    juce::Point<int> dragStart;
    juce::Rectangle<int> lasso;
    struct Original { te::MidiNote* note; double start, length; int pitch; };
    std::vector<Original> original;
    juce::String lastSignature;

    // auditioning: the clicked note plays on the track's instrument for a moment
    te::MPESourceID auditionSource = te::createUniqueMPESourceID();
    int lastAuditioned = -1;
    juce::uint32 auditionUntil = 0;

    //==============================================================================
    juce::Rectangle<int> body() const          { return getLocalBounds().withTrimmedTop (headerH); }
    juce::Rectangle<int> keyboardArea() const  { return body().withTrimmedBottom (velocityH).removeFromLeft (keyboardW); }
    juce::Rectangle<int> notesArea() const     { return body().withTrimmedBottom (velocityH).withTrimmedLeft (keyboardW); }
    juce::Rectangle<int> velocityArea() const  { return body().removeFromBottom (velocityH).withTrimmedLeft (keyboardW); }
    int visibleRows() const                    { return juce::jmax (1, notesArea().getHeight() / rowH); }

    double beatAtX (int x) const { return viewStartBeat + (x - notesArea().getX()) / pxPerBeat; }
    float xOfBeat (double songBeat) const { return (float) (notesArea().getX() + (songBeat - viewStartBeat) * pxPerBeat); }
    int noteAtY (int y) const { return juce::jlimit (0, 127, topNote - (y - notesArea().getY()) / rowH); }
    int yOfNote (int n) const { return notesArea().getY() + (topNote - n) * rowH; }

    juce::Rectangle<int> noteBounds (const te::MidiNote& n) const
    {
        const double start = midiedit::songBeat (*clip, n);
        const float x1 = xOfBeat (start), x2 = xOfBeat (start + n.getLengthBeats().inBeats());
        return { (int) x1, yOfNote (n.getNoteNumber()) + 1, juce::jmax (3, (int) (x2 - x1)), rowH - 1 };
    }

    te::MidiNote* noteAt (juce::Point<int> p) const
    {
        if (clip == nullptr || ! notesArea().contains (p)) return nullptr;
        auto& notes = clip->getSequence().getNotes();
        for (int i = notes.size(); --i >= 0;)   // the topmost (last drawn) first
            if (noteBounds (*notes[i]).contains (p)) return notes[i];
        return nullptr;
    }

    bool nearRightEdge (const te::MidiNote& n, juce::Point<int> p) const
    {
        auto b = noteBounds (n);
        return p.x >= b.getRight() - juce::jmin (8, b.getWidth() / 3);
    }

    /** Notes deleted elsewhere (undo, the arrange view) drop out of the selection */
    void pruneSelection()
    {
        if (clip == nullptr) { selection.clear(); return; }
        auto& notes = clip->getSequence().getNotes();
        for (int i = selection.size(); --i >= 0;)
            if (! notes.contains (selection[i])) selection.remove (i);
    }

    void addNoteAt (juce::Point<int> p)
    {
        const double grid = gridBeats();
        const double content = clip->getContentStartBeat().inBeats();
        const double start = std::floor (beatAtX (p.x) / grid) * grid;
        if (start < clip->getStartBeat().inBeats() - 0.0001 || start >= clip->getEndBeat().inBeats()) return;   // inside the clip only
        const int pitch = noteAtY (p.y);
        const int vel = 100;
        clip->edit.getUndoManager().beginNewTransaction ("Add note");
        if (auto* n = clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (start - content),
                                                   te::BeatDuration::fromBeats (juce::jmax (grid, lastLength)), vel, 0, &clip->edit.getUndoManager()))
            selection = { n };
        audition (pitch, vel);
    }

    void setVelocityAt (juce::Point<int> p, bool startOfDrag)
    {
        auto area = velocityArea();
        const int vel = juce::jlimit (1, 127, (int) std::round ((area.getBottom() - p.y) * 127.0 / juce::jmax (1, area.getHeight() - 4)));
        if (startOfDrag) clip->edit.getUndoManager().beginNewTransaction ("Note velocity");
        // the notes under the mouse; when some are selected, only those
        for (auto* n : clip->getSequence().getNotes())
        {
            auto b = noteBounds (*n);
            if (p.x >= b.getX() && p.x <= b.getRight() && (selection.isEmpty() || selection.contains (n)))
                n->setVelocity (vel, &clip->edit.getUndoManager());
        }
    }

    void noteMenu (te::MidiNote* n)
    {
        if (! selection.contains (n)) selection = { n };
        juce::PopupMenu m;
        auto act = [this] (const juce::String& what, std::function<void (juce::Array<te::MidiNote*>)> f)
        {
            return [this, what, f]
            {
                pruneSelection();
                if (clip == nullptr || selection.isEmpty()) return;
                clip->edit.getUndoManager().beginNewTransaction (what);
                f (selection);
                repaint();
            };
        };
        m.addItem ("Delete", act ("Delete notes", [this] (auto notes) { for (auto* x : notes) clip->getSequence().removeNote (*x, &clip->edit.getUndoManager()); selection.clear(); }));
        m.addItem ("Quantize", act ("Quantize notes", [this] (auto notes) { midiedit::quantize (*clip, notes, gridBeats()); }));
        juce::PopupMenu vel;
        for (int v : { 127, 100, 80, 64, 40 })
            vel.addItem (juce::String (v), act ("Note velocity", [this, v] (auto notes) { midiedit::velocity (*clip, notes, v); }));
        m.addSubMenu ("Velocity", vel);
        m.showMenuAsync ({});
    }

    //==============================================================================
    void audition (int note, int velocity = 100)
    {
        auto* at = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
        if (at == nullptr || ! instruments::isInstrumentTrack (*at)) return;
        stopAudition();
        at->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity)), auditionSource);
        lastAuditioned = note;
        auditionUntil = juce::Time::getMillisecondCounter() + 300;
    }

    void stopAudition()
    {
        if (lastAuditioned < 0) return;
        if (auto* at = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr)
            at->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, lastAuditioned), auditionSource);
        lastAuditioned = -1;
        auditionUntil = 0;
    }

    void fitToClip()
    {
        if (clip == nullptr) return;
        viewStartBeat = clip->getStartBeat().inBeats() - 0.5;
        const double len = juce::jmax (4.0, clip->getLengthInBeats().inBeats() + 1.0);
        pxPerBeat = juce::jlimit (12.0, 200.0, 900.0 / len);
        auto range = clip->getSequence().getNoteNumberRange();
        topNote = clip->getSequence().getNumNotes() == 0 ? 84 : juce::jlimit (24, 127, range.getEnd() + 6);
    }

    /** The clip's notes in the middle of the pane (or around middle C when it's empty) */
    void centreOnNotes()
    {
        if (clip == nullptr) return;
        auto range = clip->getSequence().getNoteNumberRange();
        // rows a little shorter (down to 9 px) when that fits every note in
        const int needed = range.getLength() + 4;
        if (clip->getSequence().getNumNotes() > 0 && needed > visibleRows())
            rowH = juce::jlimit (9, 14, notesArea().getHeight() / needed);
        const int middle = clip->getSequence().getNumNotes() == 0 ? 60 : (range.getStart() + range.getEnd() + 1) / 2;
        topNote = juce::jlimit (visibleRows(), 127, middle + visibleRows() / 2);
    }

    void timerCallback() override
    {
        if (clip == nullptr || ! clip->state.getParent().isValid())   // the clip was deleted
        {
            if (onClose) juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PianoRoll> (this)] { if (safe && safe->onClose) safe->onClose(); });
            stopTimer();
            return;
        }
        if (auditionUntil != 0 && juce::Time::getMillisecondCounter() > auditionUntil && dragMode == DragMode::none) stopAudition();
        auto* track = clip->getTrack();
        auto text = "Piano roll:  " + clip->getName() + (track != nullptr ? "  on  " + track->getName() : juce::String())
                  + "     " + juce::String (clip->getSequence().getNumNotes()) + " notes" + (selection.isEmpty() ? juce::String() : ", " + juce::String (selection.size()) + " selected");
        if (title.getText() != text) title.setText (text, juce::dontSendNotification);
        repaint();   // the playhead, and notes changed elsewhere (undo, recording)
    }

    //==============================================================================
    void paintKeyboard (juce::Graphics& g)
    {
        auto area = keyboardArea();
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (area);
        for (int n = topNote; n >= 0 && yOfNote (n) < area.getBottom(); --n)
        {
            juce::Rectangle<int> row (area.getX(), yOfNote (n), area.getWidth(), rowH);
            const bool black = midiedit::isBlackKey (n);
            g.setColour (black ? juce::Colour (0xff1a1816) : juce::Colour (0xffe9e5dc));
            g.fillRect (black ? row.withTrimmedRight (18) : row);
            if (black) { g.setColour (juce::Colour (0xffe9e5dc)); g.fillRect (row.removeFromRight (18)); }
            g.setColour (juce::Colour (0x33000000));
            g.drawHorizontalLine (row.getBottom() - 1, (float) area.getX(), (float) area.getRight());
            if (n % 12 == 0)
            {
                g.setColour (juce::Colour (0xff3a3632));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (midiedit::noteName (n), row.withTrimmedRight (4), juce::Justification::centredRight);
            }
        }
        g.setColour (theme::strong);
        g.fillRect (area.removeFromRight (1));
    }

    void paintGrid (juce::Graphics& g)
    {
        auto area = notesArea();
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (area);
        // rows: black keys darker, a line under every C
        for (int n = topNote; n >= 0 && yOfNote (n) < area.getBottom(); --n)
        {
            juce::Rectangle<int> row (area.getX(), yOfNote (n), area.getWidth(), rowH);
            g.setColour (midiedit::isBlackKey (n) ? theme::base : theme::surface);
            g.fillRect (row);
            if (n % 12 == 0) { g.setColour (theme::strong); g.drawHorizontalLine (row.getBottom() - 1, (float) area.getX(), (float) area.getRight()); }
        }
        // outside the clip is dimmed
        const float cx1 = xOfBeat (clip->getStartBeat().inBeats()), cx2 = xOfBeat (clip->getEndBeat().inBeats());
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), (float) area.getY(), juce::jmax (0.0f, cx1 - area.getX()), (float) area.getHeight()));
        g.fillRect (juce::Rectangle<float> (cx2, (float) area.getY(), juce::jmax (0.0f, (float) area.getRight() - cx2), (float) area.getHeight()));
        // vertical lines: grid, beats, bars
        const int beatsPerBar = juce::jmax (1, clip->edit.tempoSequence.getTimeSig (0)->numerator.get());
        const double grid = gridBeats();
        const double first = std::floor (beatAtX (area.getX()) / grid) * grid;
        for (double b = first; xOfBeat (b) < area.getRight(); b += grid)
        {
            const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
            const bool bar = beat && ((int) std::round (b)) % beatsPerBar == 0;
            g.setColour (bar ? theme::muted.withAlpha (0.8f) : beat ? theme::strong.brighter (0.15f) : theme::strong.withAlpha (0.6f));
            g.drawVerticalLine ((int) xOfBeat (b), (float) area.getY(), (float) area.getBottom());
        }
    }

    void paintNotes (juce::Graphics& g)
    {
        auto area = notesArea();
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (area);
        auto colour = clip->getTrack() != nullptr ? clip->getTrack()->getColour() : theme::accent;
        for (auto* n : clip->getSequence().getNotes())
        {
            auto b = noteBounds (*n).toFloat();
            if (! b.intersects (area.toFloat())) continue;
            const bool sel = selection.contains (n);
            const float shade = 0.45f + 0.55f * (float) n->getVelocity() / 127.0f;
            g.setColour (colour.withMultipliedBrightness (shade).withAlpha (n->isMute() ? 0.4f : 1.0f));
            g.fillRoundedRectangle (b, 2.5f);
            g.setColour (sel ? theme::paper : colour.darker (0.6f));
            g.drawRoundedRectangle (b.reduced (0.5f), 2.5f, sel ? 1.6f : 1.0f);
            if (b.getWidth() > 34 && rowH >= 12)
            {
                g.setColour (theme::base.withAlpha (0.85f));
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (midiedit::noteName (n->getNoteNumber()), b.reduced (4, 0), juce::Justification::centredLeft, false);
            }
        }
        if (! lasso.isEmpty())
        {
            g.setColour (theme::paper.withAlpha (0.12f));
            g.fillRect (lasso);
            g.setColour (theme::paper.withAlpha (0.6f));
            g.drawRect (lasso);
        }
    }

    void paintVelocity (juce::Graphics& g)
    {
        auto area = velocityArea();
        g.setColour (theme::surface);
        g.fillRect (area);
        g.setColour (theme::strong);
        g.fillRect (area.removeFromTop (1));
        g.setColour (theme::muted);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("VEL", body().removeFromBottom (velocityH).removeFromLeft (keyboardW).reduced (6, 4), juce::Justification::topLeft);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (area);
        auto colour = clip->getTrack() != nullptr ? clip->getTrack()->getColour() : theme::accent;
        for (auto* n : clip->getSequence().getNotes())
        {
            auto nb = noteBounds (*n);
            const int h = (int) std::round ((area.getHeight() - 4) * n->getVelocity() / 127.0);
            juce::Rectangle<int> bar (nb.getX(), area.getBottom() - h, 3, h);
            g.setColour (selection.contains (n) ? theme::paper : colour);
            g.fillRect (bar);
            g.fillEllipse (bar.getCentreX() - 3.0f, (float) bar.getY() - 3.0f, 6.0f, 6.0f);
        }
    }

    void paintPlayhead (juce::Graphics& g)
    {
        auto& edit = clip->edit;
        const double beat = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats();
        const float x = xOfBeat (beat);
        auto area = notesArea().getUnion (velocityArea());
        if (x < area.getX() || x > area.getRight()) return;
        g.setColour (theme::paper);
        g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
    }
};
