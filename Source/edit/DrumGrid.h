// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The drum grid: a step editor for a drum track's MIDI clip, in the same pane
// as the piano roll (double-click the clip; the header switches between them).
//
//   click a step          puts a hit there (or takes it away)
//   drag across steps     paints hits (or erases, if the first one was a hit)
//   Shift+click           an accent (louder hit), or back to normal
//   right-click           velocity, and clearing or filling a whole row
//   click a drum's name   plays it
//
// Drum tracks play the Basement Kit: drum sounds Basement makes itself the first
// time (no samples to download), copied into the song folder so the song is
// complete on its own. Rows follow General MIDI, so any MIDI drum part or drum
// plugin lines up too.
#pragma once

namespace drums
{
    struct Lane { int note; const char* name; };

    /** The kit's rows, top to bottom (General MIDI notes) */
    inline const std::array<Lane, 10>& lanes()
    {
        static const std::array<Lane, 10> l { { { 36, "Kick" }, { 38, "Snare" }, { 39, "Clap" }, { 42, "Closed hat" }, { 46, "Open hat" },
                                                { 41, "Low tom" }, { 45, "Mid tom" }, { 48, "High tom" }, { 49, "Crash" }, { 51, "Ride" } } };
        return l;
    }

    /** Names for the other General MIDI drum notes (an imported part can use them) */
    inline juce::String gmName (int note)
    {
        for (auto& l : lanes()) if (l.note == note) return l.name;
        static const std::map<int, const char*> more { { 35, "Kick 2" }, { 37, "Side stick" }, { 40, "Snare 2" }, { 43, "Floor tom 2" },
                                                       { 44, "Pedal hat" }, { 47, "Low-mid tom" }, { 50, "High tom 2" }, { 52, "China" },
                                                       { 53, "Ride bell" }, { 54, "Tambourine" }, { 55, "Splash" }, { 56, "Cowbell" },
                                                       { 57, "Crash 2" }, { 59, "Ride 2" }, { 60, "Hi bongo" }, { 61, "Low bongo" } };
        if (auto it = more.find (note); it != more.end()) return it->second;
        return "Note " + juce::String (note);
    }

    inline bool isDrumTrack (te::AudioTrack& t) { return t.state.getProperty ("basementRole").toString() == "drums"; }

    //==============================================================================
    // The Basement Kit, synthesised: short, punchy, mix-ready one-shots.
    namespace kit
    {
        constexpr double fs = 48000.0;

        inline float noise (juce::Random& r) { return r.nextFloat() * 2.0f - 1.0f; }

        /** A bank of detuned square waves: the metallic part of hats and cymbals (the classic six-oscillator recipe) */
        inline float metal (double t)
        {
            static const double f[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            float s = 0;
            for (double x : f) s += std::fmod (t * x * 1.7, 1.0) < 0.5 ? 1.0f : -1.0f;
            return s / 6.0f;
        }

        inline juce::AudioBuffer<float> make (int note)
        {
            juce::Random rnd (note * 7919);
            auto seconds = [note]
            {
                switch (note) { case 36: return 0.6; case 38: return 0.4; case 39: return 0.45; case 42: return 0.18; case 46: return 0.7;
                                case 49: return 2.2; case 51: return 1.6; default: return 0.7; }
            }();
            const int n = (int) (fs * seconds);
            juce::AudioBuffer<float> b (1, n);
            auto* out = b.getWritePointer (0);
            juce::IIRFilter hp, bp, hp2;
            double phase = 0;
            switch (note)
            {
                case 36: // kick: a sine falling from 150 to 48 Hz, with a click on the front
                    for (int i = 0; i < n; ++i)
                    {
                        const double t = i / fs;
                        const double f = 48.0 + 102.0 * std::exp (-t / 0.035);
                        phase += f / fs;
                        out[i] = (float) (std::sin (juce::MathConstants<double>::twoPi * phase) * std::exp (-t / 0.32))
                               + (t < 0.004 ? 0.5f * noise (rnd) * (float) (1.0 - t / 0.004) : 0.0f);
                    }
                    break;
                case 38: // snare: two drum tones and a crack of filtered noise
                    hp.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 1200.0));
                    for (int i = 0; i < n; ++i)
                    {
                        const double t = i / fs;
                        const float tone = (float) ((std::sin (juce::MathConstants<double>::twoPi * 185.0 * t) * 0.6
                                                     + std::sin (juce::MathConstants<double>::twoPi * 330.0 * t) * 0.3) * std::exp (-t / 0.06));
                        out[i] = tone + 0.8f * hp.processSingleSampleRaw (noise (rnd)) * (float) std::exp (-t / 0.12);
                    }
                    break;
                case 39: // clap: three quick bursts, then the tail
                    bp.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 1100.0, 1.2));
                    for (int i = 0; i < n; ++i)
                    {
                        const double t = i / fs;
                        double env = std::exp (-juce::jmax (0.0, t - 0.022) / 0.13);
                        for (double s : { 0.0, 0.009, 0.018 }) if (t >= s && t < s + 0.009) env = juce::jmax (env, std::exp (-(t - s) / 0.003));
                        out[i] = 1.6f * bp.processSingleSampleRaw (noise (rnd)) * (float) env;
                    }
                    break;
                case 42: case 46: case 49: case 51: // hats and cymbals: metal plus air
                {
                    const double decay = note == 42 ? 0.035 : note == 46 ? 0.25 : note == 49 ? 0.75 : 0.5;
                    hp.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, note == 51 ? 5000.0 : 7000.0));
                    hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 3000.0));
                    const float air = note == 49 ? 0.7f : note == 51 ? 0.25f : 0.5f;
                    for (int i = 0; i < n; ++i)
                    {
                        const double t = i / fs;
                        const float m = hp.processSingleSampleRaw (metal (t) + air * noise (rnd));
                        float s = hp2.processSingleSampleRaw (m) * (float) std::exp (-t / decay);
                        if (note == 51) s += 0.15f * (float) (std::sin (juce::MathConstants<double>::twoPi * 2600.0 * t) * std::exp (-t / 0.4)); // the bell
                        out[i] = s * (note == 49 ? 0.9f : 0.7f);
                    }
                    break;
                }
                default: // toms: a falling sine, a little skin noise
                {
                    const double f0 = note == 41 ? 92.0 : note == 45 ? 124.0 : 165.0;
                    for (int i = 0; i < n; ++i)
                    {
                        const double t = i / fs;
                        phase += (f0 * (1.0 + 0.6 * std::exp (-t / 0.05))) / fs;
                        out[i] = (float) (std::sin (juce::MathConstants<double>::twoPi * phase) * std::exp (-t / 0.28))
                               + 0.05f * noise (rnd) * (float) std::exp (-t / 0.02);
                    }
                }
            }
            // a few ms fade at the end, then peak at -1 dB
            for (int i = 0; i < juce::jmin (n, 240); ++i) out[n - 1 - i] *= i / 240.0f;
            const float peak = b.getMagnitude (0, n);
            if (peak > 0) b.applyGain (0.89f / peak);
            return b;
        }

        /** The kit's WAVs in a folder (made if they aren't there) */
        inline juce::File ensure (const juce::File& folder)
        {
            folder.createDirectory();
            for (auto& l : lanes())
            {
                auto f = folder.getChildFile (juce::String (l.name) + ".wav");
                if (f.existsAsFile()) continue;
                auto audio = make (l.note);
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
                if (os == nullptr) continue;
                auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (1).withBitsPerSample (24));
                if (w != nullptr) w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
            return folder;
        }

        /** Loads the kit into a Sampler: each sound on its own key, played to the end of the sample */
        inline void load (te::SamplerPlugin& sampler, const juce::File& songFolder)
        {
            auto folder = ensure (songFolder.getChildFile ("Drum kit"));
            while (sampler.getNumSounds() > 0) sampler.removeSound (0);
            for (auto& l : lanes())
            {
                auto f = folder.getChildFile (juce::String (l.name) + ".wav");
                if (sampler.addSound (f.getFullPathName(), l.name, 0.0, 0.0, 0.0f).isNotEmpty()) continue;
                const int i = sampler.getNumSounds() - 1;
                sampler.setSoundParams (i, l.note, l.note, l.note);
                sampler.setSoundOpenEnded (i, true);
                const float gain = l.note == 36 ? 0.0f : l.note == 38 ? -1.0f : (l.note == 42 || l.note == 46) ? -7.0f : l.note >= 49 ? -9.0f : -3.0f;
                const float pan = l.note == 42 || l.note == 46 ? 0.25f : l.note == 41 ? -0.3f : l.note == 48 ? 0.3f : l.note == 51 ? 0.35f : l.note == 49 ? -0.35f : 0.0f;
                sampler.setSoundGains (i, gain, pan);
            }
            sampler.flushPendingUpdates();
        }
    }
}

//==============================================================================
class DrumGrid : public NoteEditor,
                 private juce::Timer
{
public:
    DrumGrid (te::MidiClip& c) : clip (&c)
    {
        setWantsKeyboardFocus (true);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::paper);
        addAndMakeVisible (title);
        for (int i : { 0, 1, 2, 3, 5 }) gridBox.addItem (midiedit::grids()[(size_t) i].name, i + 1);
        gridBox.setSelectedId (3, juce::dontSendNotification);   // 1/16
        gridBox.setTooltip ("Step size");
        gridBox.onChange = [this] { repaint(); };
        addAndMakeVisible (gridBox);
        pianoButton.setTooltip ("Edit these notes in the piano roll instead");
        pianoButton.onClick = [this] { if (onSwitchView) onSwitchView(); };
        addAndMakeVisible (pianoButton);
        closeButton.setTooltip ("Close (Esc)");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
        for (auto* b : std::initializer_list<juce::Component*> { &gridBox, &pianoButton, &closeButton }) b->setMouseClickGrabsKeyboardFocus (false);
        updateRows();
        startTimerHz (20);
    }

    ~DrumGrid() override { stopAudition(); }

    te::MidiClip* getClip() const override { return clip.get(); }
    juce::Array<te::MidiNote*> targetNotes() override { return clip != nullptr ? midiedit::allNotes (*clip) : juce::Array<te::MidiNote*>(); }
    double gridBeats() const override { return midiedit::grids()[(size_t) juce::jlimit (0, 5, gridBox.getSelectedId() - 1)].beats; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::base);
        g.setColour (theme::surface);
        g.fillRect (getLocalBounds().removeFromTop (headerH));
        g.setColour (theme::strong);
        g.fillRect (getLocalBounds().removeFromTop (1));
        if (clip == nullptr) return;

        const auto area = gridArea();
        const int steps = numSteps();
        const double step = gridBeats();
        const int perBeat = juce::jmax (1, juce::roundToInt (1.0 / step));
        const int perBar = perBeat * juce::jmax (1, beatsPerBar());
        for (int row = 0; row < rows.size(); ++row)
        {
            auto y = area.getY() + row * rowH;
            // the drum's name
            g.setColour (row % 2 ? theme::subtle : theme::surface);
            g.fillRect (0, y, nameW, rowH);
            g.setColour (rows[row].note == lastAuditioned ? theme::accent : theme::text2);
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (rows[row].name, 10, y, nameW - 14, rowH, juce::Justification::centredLeft, true);
            // its steps
            for (int s = firstStep; s < steps; ++s)
            {
                auto cell = cellBounds (row, s);
                if (cell.getX() > area.getRight()) break;
                const bool beatGroup = (s / perBeat) % 2 == 0;
                g.setColour (beatGroup ? theme::raised : theme::strong.interpolatedWith (theme::raised, 0.5f));
                g.fillRoundedRectangle (cell.reduced (1.5f), 3.0f);
                if (auto* n = noteAt (rows[row].note, s))
                {
                    const float v = n->getVelocity() / 127.0f;
                    g.setColour (theme::accent.withAlpha (0.35f + 0.65f * v));
                    g.fillRoundedRectangle (cell.reduced (2.5f), 3.0f);
                    if (n->getVelocity() >= 120) { g.setColour (theme::paper); g.fillEllipse (cell.getCentreX() - 2.5f, cell.getCentreY() - 2.5f, 5.0f, 5.0f); }
                }
            }
        }
        // bar lines and numbers
        g.setFont (juce::FontOptions (11.0f));
        for (int s = firstStep; s <= steps; ++s)
        {
            const float x = cellX (s);
            if (x > area.getRight()) break;
            if (s % perBar == 0)
            {
                g.setColour (theme::muted.withAlpha (0.6f));
                g.fillRect (x - 1.0f, (float) area.getY(), 1.5f, (float) area.getHeight());
                g.setColour (theme::muted);
                if (s < steps) g.drawText ("Bar " + juce::String (s / perBar + 1), (int) x + 3, headerH + 2, 60, 14, juce::Justification::centredLeft);
            }
        }
        // the playhead
        auto& transport = clip->edit.getTransport();
        const double beat = clip->edit.tempoSequence.toBeats (transport.getPosition()).inBeats() - clip->getStartBeat().inBeats();   // from the clip's start
        if (beat >= 0 && beat < clip->getLengthInBeats().inBeats())
        {
            const float x = cellX (firstStep) + (float) ((beat / step - firstStep) * cellW);
            if (x >= area.getX())
            {
                g.setColour (theme::paper.withAlpha (0.8f));
                g.fillRect (x, (float) area.getY(), 1.5f, (float) area.getHeight());
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().removeFromTop (headerH).reduced (10, 5);
        closeButton.setBounds (r.removeFromRight (28));
        r.removeFromRight (10);
        pianoButton.setBounds (r.removeFromRight (90));
        r.removeFromRight (6);
        gridBox.setBounds (r.removeFromRight (90));
        r.removeFromRight (10);
        title.setBounds (r);
        // cells as wide as fit (the whole clip when it can), never narrower than 14 px
        const int avail = gridArea().getWidth();
        cellW = juce::jlimit (14.0f, 44.0f, avail / (float) juce::jmax (1, numSteps()));
        rowH = juce::jlimit (16, 26, gridArea().getHeight() / juce::jmax (1, rows.size()));
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (clip == nullptr) return;
        grabKeyboardFocus();
        const auto p = e.getPosition();
        const int row = rowAt (p.y);
        if (row < 0) return;
        if (p.x < nameW) { audition (rows[row].note, 110); if (e.mods.isPopupMenu()) rowMenu (row); return; }
        const int s = stepAt (p.x);
        if (s < 0) return;
        if (e.mods.isPopupMenu()) { cellMenu (row, s); return; }
        clip->edit.getUndoManager().beginNewTransaction ("Drum hits");
        auto* existing = noteAt (rows[row].note, s);
        if (e.mods.isShiftDown())
        {
            painting = Paint::none;
            if (existing != nullptr) existing->setVelocity (existing->getVelocity() >= 120 ? 100 : 127, &clip->edit.getUndoManager());
            else addHit (row, s, 127);
            audition (rows[row].note, 127);
        }
        else if (existing != nullptr) { painting = Paint::erase; removeHit (row, s); }
        else { painting = Paint::add; addHit (row, s, 100); audition (rows[row].note, 100); }
        lastCell = { row, s };
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (painting == Paint::none || clip == nullptr) return;
        const int row = rowAt (e.y), s = stepAt (e.x);
        if (row < 0 || s < 0 || juce::Point<int> (row, s) == lastCell) return;
        lastCell = { row, s };
        if (painting == Paint::add && noteAt (rows[row].note, s) == nullptr) { addHit (row, s, 100); audition (rows[row].note, 100); }
        if (painting == Paint::erase) removeHit (row, s);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { painting = Paint::none; }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int visible = (int) (gridArea().getWidth() / cellW);
        firstStep = juce::jlimit (0, juce::jmax (0, numSteps() - visible), firstStep - juce::roundToInt ((w.deltaX != 0 ? w.deltaX : w.deltaY) * 16.0f));
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        return false;   // everything else (Space, R...) goes to the app as usual
    }

private:
    te::SafeSelectable<te::MidiClip> clip;
    juce::Label title;
    juce::ComboBox gridBox;
    juce::TextButton pianoButton { "Piano roll" }, closeButton { "X" };
    struct Row { int note; juce::String name; };
    juce::Array<Row> rows;
    enum class Paint { none, add, erase } painting = Paint::none;
    juce::Point<int> lastCell { -1, -1 };
    int firstStep = 0, rowH = 22;
    float cellW = 24.0f;
    static constexpr int headerH = 34, nameW = 110, rulerH = 18;

    te::MPESourceID auditionSource = te::createUniqueMPESourceID();
    int lastAuditioned = -1;
    juce::uint32 auditionUntil = 0;

    juce::Rectangle<int> gridArea() const { return getLocalBounds().withTrimmedTop (headerH + rulerH).withTrimmedLeft (nameW).withTrimmedBottom (4); }
    int beatsPerBar() const { return clip != nullptr ? clip->edit.tempoSequence.getTimeSig (0)->numerator.get() : 4; }
    int numSteps() const { return clip != nullptr ? juce::jmax (1, (int) std::ceil (clip->getLengthInBeats().inBeats() / gridBeats() - 1.0e-6)) : 16; }
    float cellX (int s) const { return gridArea().getX() + (s - firstStep) * cellW; }
    juce::Rectangle<float> cellBounds (int row, int s) const { return { cellX (s), (float) (gridArea().getY() + row * rowH), cellW, (float) rowH }; }
    int rowAt (int y) const { const int r = (y - gridArea().getY()) / rowH; return y >= gridArea().getY() && r < rows.size() ? r : -1; }
    int stepAt (int x) const
    {
        if (x < gridArea().getX()) return -1;
        const int s = firstStep + (int) ((x - gridArea().getX()) / cellW);
        return s < numSteps() ? s : -1;
    }

    /** Steps are counted from the start of the clip; notes from its content start */
    double stepBeat (int s) const { return s * gridBeats() + (clip->getStartBeat() - clip->getContentStartBeat()).inBeats(); }

    te::MidiNote* noteAt (int pitch, int s) const
    {
        const double from = stepBeat (s), to = from + gridBeats();
        for (auto* n : clip->getSequence().getNotes())
            if (n->getNoteNumber() == pitch && n->getStartBeat().inBeats() >= from - 1.0e-4 && n->getStartBeat().inBeats() < to - 1.0e-4) return n;
        return nullptr;
    }

    void addHit (int row, int s, int velocity)
    {
        clip->getSequence().addNote (rows[row].note, te::BeatPosition::fromBeats (stepBeat (s)), te::BeatDuration::fromBeats (gridBeats() * 0.9),
                                     velocity, 0, &clip->edit.getUndoManager());
    }

    void removeHit (int row, int s)
    {
        while (auto* n = noteAt (rows[row].note, s)) clip->getSequence().removeNote (*n, &clip->edit.getUndoManager());
    }

    /** The kit's rows, plus a row for any other drum note the clip has */
    void updateRows()
    {
        rows.clear();
        for (auto& l : drums::lanes()) rows.add ({ l.note, l.name });
        if (clip == nullptr) return;
        std::set<int> extra;
        for (auto* n : clip->getSequence().getNotes())
        {
            bool known = false;
            for (auto& l : rows) known = known || l.note == n->getNoteNumber();
            if (! known) extra.insert (n->getNoteNumber());
        }
        for (int note : extra) rows.add ({ note, drums::gmName (note) });
    }

    void rowMenu (int row)
    {
        juce::PopupMenu m;
        const int perBeat = juce::jmax (1, juce::roundToInt (1.0 / gridBeats()));
        auto fill = [this, row] (int every, const juce::String& what)
        {
            return [this, row, every, what]
            {
                clip->edit.getUndoManager().beginNewTransaction (what);
                for (int s = 0; s < numSteps(); ++s) { removeHit (row, s); if (every > 0 && s % every == 0) addHit (row, s, 100); }
                repaint();
            };
        };
        m.addSectionHeader (rows[row].name);
        m.addItem ("Clear this row", fill (0, "Clear drum row"));
        m.addItem ("A hit every beat", fill (perBeat, "Fill drum row"));
        if (perBeat >= 2) m.addItem ("A hit every 1/8", fill (perBeat / 2, "Fill drum row"));
        if (perBeat >= 4) m.addItem ("A hit every 1/16", fill (perBeat / 4, "Fill drum row"));
        m.showMenuAsync ({});
    }

    void cellMenu (int row, int s)
    {
        juce::PopupMenu m;
        auto* n = noteAt (rows[row].note, s);
        for (auto [v, name] : std::initializer_list<std::pair<int, const char*>> { { 127, "Accent (127)" }, { 100, "Normal (100)" }, { 70, "Soft (70)" }, { 40, "Ghost (40)" } })
            m.addItem (name, true, n != nullptr && n->getVelocity() == v, [this, row, s, v = v]
            {
                clip->edit.getUndoManager().beginNewTransaction ("Drum hit velocity");
                if (auto* x = noteAt (rows[row].note, s)) x->setVelocity (v, &clip->edit.getUndoManager());
                else addHit (row, s, v);
                audition (rows[row].note, v);
                repaint();
            });
        if (n != nullptr) m.addItem ("Remove", [this, row, s] { clip->edit.getUndoManager().beginNewTransaction ("Drum hits"); removeHit (row, s); repaint(); });
        m.showMenuAsync ({});
    }

    void audition (int note, int velocity)
    {
        auto* at = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
        if (at == nullptr || ! instruments::isInstrumentTrack (*at)) return;
        stopAudition();
        at->injectLiveMidiMessage (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, velocity)), auditionSource);
        lastAuditioned = note;
        auditionUntil = juce::Time::getMillisecondCounter() + 250;
        repaint();
    }

    void stopAudition()
    {
        if (lastAuditioned < 0) return;
        if (auto* at = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr)
            at->injectLiveMidiMessage (juce::MidiMessage::noteOff (10, lastAuditioned), auditionSource);
        lastAuditioned = -1;
        auditionUntil = 0;
    }

    void timerCallback() override
    {
        if (clip == nullptr || ! clip->state.getParent().isValid())
        {
            if (onClose) juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DrumGrid> (this)] { if (safe && safe->onClose) safe->onClose(); });
            stopTimer();
            return;
        }
        if (auditionUntil != 0 && juce::Time::getMillisecondCounter() > auditionUntil) stopAudition();
        auto* track = clip->getTrack();
        int hits = clip->getSequence().getNumNotes();
        auto text = "Drums:  " + clip->getName() + (track != nullptr ? "  on  " + track->getName() : juce::String()) + "     " + juce::String (hits) + (hits == 1 ? " hit" : " hits");
        if (title.getText() != text) title.setText (text, juce::dontSendNotification);
        repaint();
    }
};
