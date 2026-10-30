// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Window > Tuner (T): pick your instrument and tuning (82 instruments, from
// guitar and bass to bouzouki, oud, lute, viols and transposing horns; see
// tools/tunings.py) and it shows which string you're playing and how far off
// it is, or use it chromatically for anything with a pitch. A needle or a
// strobe, any reference pitch (A = 415 to 466), and historical temperaments
// (meantone, Werckmeister, just...) for early music. It listens to the
// interface directly, so it works whether or not the song is playing, and
// reads 28 Hz to 2.1 kHz: a five-string bass's low B to a piccolo's top.
#pragma once

#include "Tunings.h"

/** YIN pitch estimate on a block of samples (de Cheveigne & Kawahara) */
struct TunerPitch
{
    static float estimate (const float* x, int n, double sampleRate, float minHz = 28.0f, float maxHz = 2100.0f)
    {
        const int tauMin = juce::jmax (2, (int) (sampleRate / maxHz)), tauMax = juce::jmin (n / 2, (int) (sampleRate / minHz));
        const int w = n - tauMax;
        if (w <= 0) return 0.0f;
        double energy = 0;
        for (int i = 0; i < w; ++i) energy += (double) x[i] * x[i];
        if (energy / w < 1.0e-6) return 0.0f; // quieter than about -60 dB: no note
        std::vector<float> d ((size_t) tauMax + 1, 0.0f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0;
            for (int i = 0; i < w; ++i) { const double diff = x[i] - x[i + tau]; s += diff * diff; }
            d[(size_t) tau] = (float) s;
        }
        // cumulative mean normalised difference
        double running = 0;
        d[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[(size_t) tau];
            d[(size_t) tau] = running > 0 ? (float) (d[(size_t) tau] * tau / running) : 1.0f;
        }
        // the first dip under the threshold, followed down to its bottom (else the deepest dip, if it's clear enough)
        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[(size_t) tau] < 0.12f)
            {
                while (tau + 1 <= tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            int lowest = tauMin;
            for (int tau = tauMin; tau <= tauMax; ++tau) if (d[(size_t) tau] < d[(size_t) lowest]) lowest = tau;
            if (d[(size_t) lowest] < 0.3f) best = lowest;
        }
        if (best < 0) return 0.0f;
        // parabolic interpolation around the dip
        float t = (float) best;
        if (best > 1 && best < tauMax)
        {
            const float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const float denom = a - 2 * b + c;
            if (std::abs (denom) > 1.0e-9f) t += 0.5f * (a - c) / denom;
        }
        return (float) (sampleRate / t);
    }
};

/** What the tuner works out from a pitch: the note (or string) it's nearest, and how far off */
struct TunerReading
{
    int note = -1;          // the target, as a MIDI note (concert pitch)
    int string = -1;        // which string of the tuning, or -1 when it's chromatic
    float cents = 0;        // + sharp, - flat, from the target (in the chosen temperament)
    float targetHz = 0;

    /** hz: what was heard. strings: the tuning's notes (empty: chromatic). offsets: the temperament on C, rotated to root */
    static TunerReading of (float hz, double a4, const std::vector<int>& strings, const std::array<float, 12>& offsets, int root)
    {
        TunerReading r;
        if (hz <= 0) return r;
        const double m = 69.0 + 12.0 * std::log2 (hz / a4);
        auto target = [&] (int n) { return n + offsets[(size_t) (((n - root) % 12 + 12) % 12)] / 100.0; };
        if (! strings.empty())
        {
            double best = 1.0e9;
            for (int i = 0; i < (int) strings.size(); ++i)
            {
                const double c = (m - target (strings[(size_t) i])) * 100.0;
                if (std::abs (c) < std::abs (best)) { best = c; r.string = i; }
            }
            if (std::abs (best) <= 350.0) { r.note = strings[(size_t) r.string]; r.cents = (float) best; }
            else r.string = -1;
        }
        if (r.string < 0)
        {
            int n = (int) std::round (m);
            for (int k : { n - 1, n + 1 }) if (std::abs (m - target (k)) < std::abs (m - target (n))) n = k;
            r.note = n;
            r.cents = (float) ((m - target (n)) * 100.0);
        }
        r.targetHz = (float) (a4 * std::pow (2.0, (target (r.note) - 69.0) / 12.0));
        return r;
    }

    static juce::String name (int midiNote, bool withOctave = true)
    {
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        if (midiNote < 0) return "--";
        return juce::String (names[midiNote % 12]) + (withOctave ? juce::String (midiNote / 12 - 1) : juce::String());
    }
};

class TunerComponent;

/** The tuner stays open beside the song (not a dialog), so you can tune between takes */
class TunerWindow : public DocumentWindow
{
public:
    TunerWindow (std::unique_ptr<Component> content)
        : DocumentWindow ("Tuner", theme::surface, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setIcon (encoreIconImage (64));
        setContentOwned (content.release(), true);
        setAlwaysOnTop (true);
    }
    void closeButtonPressed() override { setVisible (false); }
};

class TunerComponent : public Component,
                       private juce::AudioIODeviceCallback,
                       private Timer
{
public:
    TunerComponent (te::Engine& e, PropertiesFile& p, const String& preferredInput) : engine (e), props (p)
    {
        auto& dm = engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto w = dm.getWaveInDevice (i))
                if (! w->isTrackDevice() && w->getChannels().size() > 0)
                {
                    inputs.add ({ w->getName(), w->getChannels()[0].indexInDevice });
                    input.addItem (inputLabel (w->getName()), inputs.size());
                }
        int pick = 1;
        for (int i = 0; i < inputs.size(); ++i) if (inputs[i].name == preferredInput) pick = i + 1;
        input.setSelectedId (pick, dontSendNotification);
        input.onChange = [this] { channel = juce::isPositiveAndBelow (input.getSelectedId() - 1, inputs.size()) ? inputs[input.getSelectedId() - 1].channel : -1; };
        input.onChange();
        input.setTooltip ("The input your instrument is plugged into");

        // the instrument (a menu by family), and its tunings
        instrumentButton.onClick = [this] { chooseInstrument(); };
        instrumentButton.setTooltip ("Pick your instrument");
        tuning.onChange = [this] { tuningChanged(); };

        // reference pitch: type it, step it, or pick a common one
        reference.setRange (400.0, 480.0, 1.0);
        reference.setValue (props.getDoubleValue ("tunerA4", 440.0), dontSendNotification);
        reference.setTextValueSuffix (" Hz");
        reference.setSliderStyle (Slider::IncDecButtons);
        reference.setTextBoxStyle (Slider::TextBoxLeft, false, 70, 24);
        reference.setTooltip ("Concert pitch: what A above middle C is tuned to");
        reference.onValueChange = [this] { props.setValue ("tunerA4", reference.getValue()); };
        referenceMenu.setTooltip ("Common reference pitches");
        referenceMenu.onClick = [this]
        {
            PopupMenu m;
            for (auto& r : tunings::references())
                m.addItem (r.name, true, (int) reference.getValue() == r.hz, [this, hz = r.hz] { reference.setValue (hz); });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&referenceMenu));
        };

        for (auto& t : tunings::temperaments()) temperament.addItem (t.name, temperament.getNumItems() + 1);
        temperament.setSelectedItemIndex (juce::jlimit (0, temperament.getNumItems() - 1, props.getIntValue ("tunerTemperament", 0)), dontSendNotification);
        temperament.onChange = [this] { props.setValue ("tunerTemperament", temperament.getSelectedItemIndex()); root.setEnabled (temperament.getSelectedItemIndex() > 0); };
        temperament.setTooltip ("How the notes are spaced. Equal temperament is modern tuning; the others are for early music and just intervals");
        for (int k = 0; k < 12; ++k) root.addItem ("in " + TunerReading::name (k, false), k + 1);
        root.setSelectedItemIndex (props.getIntValue ("tunerRoot", 0), dontSendNotification);
        root.onChange = [this] { props.setValue ("tunerRoot", root.getSelectedItemIndex()); };
        root.setEnabled (temperament.getSelectedItemIndex() > 0);
        root.setTooltip ("The key the temperament is built on");

        strobe.setToggleState (props.getBoolValue ("tunerStrobe", false), dontSendNotification);
        strobe.onClick = [this] { props.setValue ("tunerStrobe", strobe.getToggleState()); };
        strobe.setTooltip ("A strobe: the stripes drift left when you're flat, right when sharp, and stand still in tune");
        strobe.setColour (ToggleButton::textColourId, theme::paper);
        strobe.setColour (ToggleButton::tickColourId, theme::accent);

        for (auto* c : std::initializer_list<Component*> { &input, &instrumentButton, &tuning, &reference, &referenceMenu, &temperament, &root, &strobe, &noteLabel })
            addAndMakeVisible (c);
        for (auto* c : { &input, &tuning, &temperament, &root }) c->setColour (ComboBox::textColourId, theme::paper);
        noteLabel.setColour (Label::textColourId, theme::muted);
        noteLabel.setFont (FontOptions (12.5f));
        noteLabel.setJustificationType (Justification::centred);

        // the instrument from last time
        const auto saved = props.getValue ("tunerInstrument", "Guitar (6-string)");
        for (auto& f : tunings::families())
            for (auto& i : f.instruments)
                if (saved == tunings::text (i.name)) instrument = &i;
        if (instrument == nullptr) instrument = &tunings::families()[0].instruments[0];
        setInstrument (*instrument, props.getIntValue ("tunerTuning", 0));

        buffer.resize (16384, 0.0f);
        dm.deviceManager.addAudioCallback (this);
        startTimerHz (30);
        setSize (700, 470);
    }

    ~TunerComponent() override
    {
        engine.getDeviceManager().deviceManager.removeAudioCallback (this);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::surface);
        g.setColour (theme::strong);
        g.fillRect (getLocalBounds().removeFromTop (controlsH).removeFromBottom (1));

        auto r = display;
        const bool have = reading.note >= 0;
        const bool inTune = have && std::abs (reading.cents) <= 3.0f;
        auto colour = ! have ? theme::muted : inTune ? Colour (0xff22c55e) : std::abs (reading.cents) <= 15.0f ? theme::solo : theme::record;

        // the strings of the tuning, the one you're playing lit up (a tick once it's been in tune)
        if (! strings.empty())
        {
            auto row = r.removeFromTop (48);
            const int n = (int) strings.size(), pillW = jmin (72, (row.getWidth() - (n - 1) * 6) / n);
            auto pills = row.withSizeKeepingCentre (n * pillW + (n - 1) * 6, 36);
            for (int i = 0; i < n; ++i)
            {
                auto p = pills.removeFromLeft (pillW).toFloat();
                pills.removeFromLeft (6);
                const bool active = reading.string == i;
                g.setColour (active ? colour.withAlpha (0.25f) : theme::raised);
                g.fillRoundedRectangle (p, 8.0f);
                g.setColour (active ? colour : tuned[(size_t) i] ? Colour (0xff22c55e).withAlpha (0.7f) : theme::strong.brighter (0.2f));
                g.drawRoundedRectangle (p.reduced (0.5f), 8.0f, active ? 2.0f : 1.0f);
                g.setColour (active ? theme::paper : theme::text2);
                g.setFont (FontOptions (pillW < 44 ? 12.0f : 15.0f, Font::bold));
                g.drawText (TunerReading::name (strings[(size_t) i] + transpose()), p.toNearestInt(), Justification::centred);
                if (tuned[(size_t) i])
                {
                    g.setColour (Colour (0xff22c55e));
                    g.fillEllipse (p.getRight() - 11, p.getY() + 4, 7, 7);
                }
            }
            r.removeFromTop (6);
        }

        // the note (written, for a transposing instrument), big
        auto big = r.removeFromTop (92);
        g.setColour (have ? theme::paper : theme::muted);
        g.setFont (FontOptions (78.0f, Font::bold));
        g.drawText (have ? TunerReading::name (reading.note + transpose(), false) : String ("--"), big, Justification::centred);
        if (have)
        {
            g.setColour (theme::text2);
            g.setFont (FontOptions (16.0f));
            g.drawText (String (((reading.note + transpose()) / 12) - 1), big.withTrimmedLeft (big.getWidth() / 2 + 34).withTrimmedTop (44), Justification::topLeft);
        }
        if (have && transpose() != 0)
        {
            g.setColour (theme::muted);
            g.setFont (FontOptions (12.5f));
            g.drawText ("sounds " + TunerReading::name (reading.note) + " (concert)", r.removeFromTop (16), Justification::centred);
        }

        // a needle on a cents scale, or a strobe
        auto scale = r.removeFromTop (52).reduced (24, 8);
        if (strobe.getToggleState())
        {
            Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (scale);
            g.setColour (theme::strong);
            g.fillRect (scale);
            const float stripe = 18.0f;
            g.setColour (have ? colour.withAlpha (0.85f) : theme::muted.withAlpha (0.4f));
            for (float x = scale.getX() - stripe * 2 + std::fmod (strobePhase, stripe * 2); x < scale.getRight(); x += stripe * 2)
                g.fillRect (x, (float) scale.getY(), stripe, (float) scale.getHeight());
        }
        else
        {
            g.setColour (theme::strong);
            g.fillRoundedRectangle (scale.toFloat(), 6.0f);
            for (int c = -50; c <= 50; c += 10)
            {
                const float x = scale.getX() + (c + 50) / 100.0f * scale.getWidth();
                g.setColour (c == 0 ? theme::paper : theme::muted);
                g.fillRect (x - (c == 0 ? 1.0f : 0.5f), (float) scale.getY() + 4, c == 0 ? 2.0f : 1.0f, (float) scale.getHeight() - 8);
            }
            if (have)
            {
                const float x = scale.getX() + (jlimit (-50.0f, 50.0f, reading.cents) + 50.0f) / 100.0f * scale.getWidth();
                g.setColour (colour);
                g.fillRoundedRectangle (x - 4.0f, (float) scale.getY() - 4, 8.0f, (float) scale.getHeight() + 8, 3.0f);
            }
        }
        g.setColour (theme::muted);
        g.setFont (FontOptions (11.0f));
        g.drawText ("flat", scale.translated (0, scale.getHeight() + 2).withHeight (14), Justification::left);
        g.drawText ("sharp", scale.translated (0, scale.getHeight() + 2).withHeight (14), Justification::right);
        r.removeFromTop (14);

        g.setColour (colour);
        g.setFont (FontOptions (17.0f, Font::bold));
        g.drawText (! have ? (strings.empty() ? "Play a note" : "Play a string") : inTune ? "In tune"
                        : (reading.cents < 0 ? "Flat " : "Sharp ") + String (std::abs (reading.cents), 0) + " cents"
                          + (reading.string >= 0 ? "  (string " + String ((int) strings.size() - reading.string) + ")" : String()),
                    r.removeFromTop (26), Justification::centred);
        g.setColour (theme::muted);
        g.setFont (FontOptions (12.5f));
        g.drawText (have ? String (hz, 1) + " Hz   (target " + String (reading.targetHz, 1) + " Hz)" : String(), r.removeFromTop (18), Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        auto controls = r.removeFromTop (controlsH - 12);
        auto row1 = controls.removeFromTop (28);
        input.setBounds (row1.removeFromLeft (170)); row1.removeFromLeft (8);
        instrumentButton.setBounds (row1.removeFromLeft (250)); row1.removeFromLeft (8);
        tuning.setBounds (row1);
        controls.removeFromTop (8);
        auto row2 = controls.removeFromTop (28);
        reference.setBounds (row2.removeFromLeft (150)); row2.removeFromLeft (2);
        referenceMenu.setBounds (row2.removeFromLeft (28)); row2.removeFromLeft (12);
        strobe.setBounds (row2.removeFromRight (90));
        row2.removeFromRight (8);
        root.setBounds (row2.removeFromRight (80)); row2.removeFromRight (6);
        temperament.setBounds (row2);
        r.removeFromTop (12);
        noteLabel.setBounds (r.removeFromBottom (20));
        display = r;
    }

private:
    te::Engine& engine;
    PropertiesFile& props;
    struct In { String name; int channel; };
    Array<In> inputs;
    ComboBox input, tuning, temperament, root;
    TextButton instrumentButton, referenceMenu { "v" };
    ToggleButton strobe { "Strobe" };
    Slider reference;
    Label noteLabel;
    juce::Rectangle<int> display;
    static constexpr int controlsH = 90;

    const tunings::Instrument* instrument = nullptr;
    std::vector<int> strings;          // the chosen tuning's notes (empty: chromatic)
    std::vector<bool> tuned;           // strings that have been in tune
    int inTuneFrames = 0, lastString = -1;

    std::atomic<int> channel { -1 };
    std::vector<float> buffer;
    std::atomic<int> writePos { 0 };
    double sampleRate = 48000.0;
    float hz = 0, smoothedHz = 0, strobePhase = 0;
    TunerReading reading;
    int quietFrames = 0;

    int transpose() const { return instrument != nullptr ? instrument->transpose : 0; }

    void chooseInstrument()
    {
        PopupMenu m;
        for (auto& f : tunings::families())
        {
            PopupMenu sub;
            for (auto& i : f.instruments)
                sub.addItem (tunings::text (i.name), true, &i == instrument, [this, ins = &i] { setInstrument (*ins, 0); });
            m.addSubMenu (tunings::text (f.name), sub);
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&instrumentButton));
    }

    void setInstrument (const tunings::Instrument& i, int tuningIndex)
    {
        instrument = &i;
        props.setValue ("tunerInstrument", tunings::text (i.name));
        instrumentButton.setButtonText (tunings::text (i.name));
        tuning.clear (dontSendNotification);
        for (auto& t : i.tunings)
        {
            String text = tunings::text (t.name);
            if (! t.strings.empty())
            {
                StringArray notes;
                for (auto n : t.strings) notes.add (TunerReading::name (n, false));
                text << "   (" << notes.joinIntoString (" ") << ")";
            }
            tuning.addItem (text, tuning.getNumItems() + 1);
        }
        tuning.setSelectedItemIndex (jlimit (0, tuning.getNumItems() - 1, tuningIndex), dontSendNotification);
        tuning.setEnabled (tuning.getNumItems() > 1);
        tuningChanged();
    }

    void tuningChanged()
    {
        const int t = jmax (0, tuning.getSelectedItemIndex());
        props.setValue ("tunerTuning", t);
        if (instrument == nullptr || t >= (int) instrument->tunings.size()) return;
        auto& chosen = instrument->tunings[(size_t) t];
        strings = chosen.strings;
        tuned.assign (strings.size(), false);
        String note = tunings::text (chosen.note);
        if (transpose() != 0) note = "Shows the note you read: concert pitch " + String (transpose() > 0 ? "-" : "+") + String (std::abs (transpose())) + " semitones. " + note;
        noteLabel.setText (note, dontSendNotification);
        repaint();
    }

    void audioDeviceAboutToStart (AudioIODevice* d) override { sampleRate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                           const AudioIODeviceCallbackContext&) override
    {
        for (int i = 0; i < numOut; ++i) if (out[i] != nullptr) FloatVectorOperations::clear (out[i], numSamples);
        const int ch = channel.load();
        auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        if (ch < 0 || device == nullptr) return;
        // the callback only has the active channels, in order: find where ours is
        auto active = device->getActiveInputChannels();
        if (! active[ch]) return;
        int slot = 0;
        for (int i = 0; i < ch; ++i) if (active[i]) ++slot;
        if (slot >= numIn || in[slot] == nullptr) return;
        int w = writePos.load();
        const int size = (int) buffer.size();
        for (int i = 0; i < numSamples; ++i) { buffer[(size_t) w] = in[slot][i]; w = (w + 1) % size; }
        writePos.store (w);
    }

    void timerCallback() override
    {
        // twice as often as a new estimate: the strobe moves smoothly in between
        if (++frame % 2 == 0)
        {
            // the latest ~170 ms, averaged down to about 24 kHz (plenty for 28 Hz to 2.1 kHz, and cheap)
            const int stride = jmax (1, (int) std::round (sampleRate / 24000.0)), n = 4096;
            std::vector<float> x ((size_t) n);
            const int size = (int) buffer.size(), end = writePos.load();
            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                for (int k = 0; k < stride; ++k) s += buffer[(size_t) ((end - n * stride + i * stride + k + size * 8) % size)];
                x[(size_t) i] = s / (float) stride;
            }
            const float f = TunerPitch::estimate (x.data(), n, sampleRate / stride);
            if (f > 0)
            {
                quietFrames = 0;
                smoothedHz = (smoothedHz > 0 && std::abs (f / smoothedHz - 1.0f) < 0.03f) ? smoothedHz * 0.6f + f * 0.4f : f;
                hz = smoothedHz;
                auto& temper = tunings::temperaments()[(size_t) jmax (0, temperament.getSelectedItemIndex())];
                reading = TunerReading::of (hz, reference.getValue(), strings, temper.cents, jmax (0, root.getSelectedItemIndex()));
                // a string counts as tuned after a moment within 3 cents
                if (reading.string >= 0 && std::abs (reading.cents) <= 3.0f && reading.string == lastString)
                {
                    if (++inTuneFrames >= 6) tuned[(size_t) reading.string] = true;
                }
                else inTuneFrames = 0;
                lastString = reading.string;
            }
            else if (++quietFrames > 8) { hz = 0; smoothedHz = 0; reading = {}; }
        }
        if (reading.note >= 0) strobePhase += reading.cents * 0.35f;
        repaint();
    }
    int frame = 0;
};
