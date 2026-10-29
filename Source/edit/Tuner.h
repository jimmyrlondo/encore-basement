// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Window > Tuner (T): a chromatic tuner on any input, for guitars, basses,
// voices, anything with a pitch. It listens to the interface directly (so it
// works whether or not the song is playing) and reads 30 Hz to 1.5 kHz, low
// enough for a five-string bass's low B.
#pragma once

/** YIN pitch estimate on a block of samples (de Cheveigne & Kawahara) */
struct TunerPitch
{
    static float estimate (const float* x, int n, double sampleRate, float minHz = 30.0f, float maxHz = 1500.0f)
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
    TunerComponent (te::Engine& e, const String& preferredInput) : engine (e)
    {
        auto& dm = engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto w = dm.getWaveInDevice (i))
                if (! w->isTrackDevice() && w->getChannels().size() > 0)
                {
                    inputs.add ({ w->getName(), w->getChannels()[0].indexInDevice });
                    input.addItem (w->getName(), inputs.size());
                }
        int pick = 1;
        for (int i = 0; i < inputs.size(); ++i) if (inputs[i].name == preferredInput) pick = i + 1;
        input.setSelectedId (pick, dontSendNotification);
        input.onChange = [this] { channel = juce::isPositiveAndBelow (input.getSelectedId() - 1, inputs.size()) ? inputs[input.getSelectedId() - 1].channel : -1; };
        input.onChange();
        input.setColour (ComboBox::textColourId, theme::paper);
        addAndMakeVisible (input);

        reference.setRange (432.0, 446.0, 1.0);
        reference.setValue (440.0, dontSendNotification);
        reference.setTextValueSuffix (" Hz");
        reference.setSliderStyle (Slider::IncDecButtons);
        reference.setTextBoxStyle (Slider::TextBoxLeft, false, 64, 24);
        reference.setTooltip ("Concert pitch: what A4 is tuned to");
        addAndMakeVisible (reference);

        buffer.resize (16384, 0.0f);
        dm.deviceManager.addAudioCallback (this);
        startTimerHz (15);
        setSize (420, 300);
    }

    ~TunerComponent() override
    {
        engine.getDeviceManager().deviceManager.removeAudioCallback (this);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::surface);
        auto r = getLocalBounds().reduced (20).withTrimmedTop (44).withTrimmedBottom (44);
        const bool have = hz > 0.0f;
        const bool inTune = have && std::abs (cents) <= 3.0f;
        auto colour = ! have ? theme::muted : inTune ? Colour (0xff22c55e) : std::abs (cents) <= 15.0f ? theme::solo : theme::record;

        // note name
        g.setColour (have ? theme::paper : theme::muted);
        g.setFont (FontOptions (64.0f, Font::bold));
        g.drawText (have ? noteName : String ("--"), r.removeFromTop (76), Justification::centred);

        // cents scale with a needle
        auto scale = r.removeFromTop (44).reduced (10, 8);
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
            const float x = scale.getX() + (jlimit (-50.0f, 50.0f, cents) + 50.0f) / 100.0f * scale.getWidth();
            g.setColour (colour);
            g.fillRoundedRectangle (x - 4.0f, (float) scale.getY() - 4, 8.0f, (float) scale.getHeight() + 8, 3.0f);
        }

        g.setColour (colour);
        g.setFont (FontOptions (15.0f, Font::bold));
        g.drawText (! have ? "Play a note" : inTune ? "In tune" : (cents < 0 ? "Flat " : "Sharp ") + String (std::abs (cents), 0) + " cents",
                    r.removeFromTop (24), Justification::centred);
        g.setColour (theme::muted);
        g.setFont (FontOptions (12.0f));
        g.drawText (have ? String (hz, 1) + " Hz" : String(), r.removeFromTop (18), Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 14);
        auto top = r.removeFromTop (30);
        input.setBounds (top.removeFromLeft (220));
        reference.setBounds (r.removeFromBottom (28).withSizeKeepingCentre (150, 28));
    }

private:
    te::Engine& engine;
    struct In { String name; int channel; };
    Array<In> inputs;
    ComboBox input;
    Slider reference;
    std::atomic<int> channel { -1 };
    std::vector<float> buffer;
    std::atomic<int> writePos { 0 };
    double sampleRate = 48000.0;
    float hz = 0, cents = 0, smoothedHz = 0;
    String noteName;
    int quietFrames = 0;

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
        // the latest ~85 ms, averaged down to about 24 kHz (plenty for 30 Hz to 1.5 kHz, and cheap)
        const int stride = juce::jmax (1, (int) std::round (sampleRate / 24000.0)), n = 2048;
        std::vector<float> x ((size_t) n);
        const int size = (int) buffer.size(), end = writePos.load();
        for (int i = 0; i < n; ++i)
        {
            float s = 0;
            for (int k = 0; k < stride; ++k) s += buffer[(size_t) ((end - n * stride + i * stride + k + size * 4) % size)];
            x[(size_t) i] = s / (float) stride;
        }
        const float f = TunerPitch::estimate (x.data(), n, sampleRate / stride);
        if (f > 0)
        {
            quietFrames = 0;
            smoothedHz = (smoothedHz > 0 && std::abs (f / smoothedHz - 1.0f) < 0.03f) ? smoothedHz * 0.6f + f * 0.4f : f;
            hz = smoothedHz;
            const double a4 = reference.getValue();
            const double midi = 69.0 + 12.0 * std::log2 (hz / a4);
            const int nearest = (int) std::round (midi);
            cents = (float) ((midi - nearest) * 100.0);
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            noteName = String (names[((nearest % 12) + 12) % 12]) + String (nearest / 12 - 1);
        }
        else if (++quietFrames > 8) { hz = 0; smoothedHz = 0; }
        repaint();
    }
};
