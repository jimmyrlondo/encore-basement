#pragma once

// Master on export: the rendered mix goes through Encore Master's chain (one of
// its styles, a loudness target and how hard it works) on the way out, so the
// song itself never changes. Plus a short before/after preview: two
// renders of the same 30 s, played in sync so you can flip between them.

#include "dsp/MasterChain.h"

namespace mastering
{
    struct Settings
    {
        int style = -1;            // Encore Master's style (Balanced, Heavy...), or -1 for no mastering
        float intensity = 0.5f;    // 0..1, 0.5 = the style as fitted
        int target = 0;            // 0 Streaming -14, 1 Balanced -11, 2 Loud -9
        bool on() const { return style >= 0; }
    };

    inline juce::StringArray styleNames()  { return { "Balanced", "Heavy", "Bright", "Warm" }; }
    inline juce::StringArray styleBlurbs()
    {
        return { "Balanced tone and solid glue for any mix", "Huge lows, scooped mids, bite up top",
                 "Open and airy: lows and highs up, mids clear", "Smooth upper mids, sparkle on top, warm glue" };
    }
    inline juce::StringArray targetNames() { return { "Streaming (-14 LUFS)", "Balanced (-11 LUFS)", "Loud (-9 LUFS)" }; }

    inline bool isEncoreMaster (te::Plugin& p)
    {
        auto* ep = dynamic_cast<te::ExternalPlugin*> (&p);
        auto* entry = ep != nullptr ? EncorePluginFormat::find (ep->desc.fileOrIdentifier) : nullptr;
        return entry != nullptr && juce::String (entry->id) == "encore:master";
    }

    /** Masters a rendered mix in place, the way Encore Master does when a song plays through it:
        it learns the mix on its loudest 30 s (level, tone, then the drive that hits the loudness
        target), then the whole song goes through with what it learned locked. */
    inline void masterBuffer (juce::AudioBuffer<float>& audio, double fs, const Settings& m)
    {
        using MC = vs::MasterChain;
        const int len = audio.getNumSamples(), block = 1024;
        if (len == 0 || audio.getNumChannels() < 1) return;
        if (audio.getNumChannels() < 2) { audio.setSize (2, len, true); audio.copyFrom (1, 0, audio, 0, 0, len); }

        MC::Settings s;
        s.style = juce::jlimit (0, MC::numStyles - 1, m.style);
        s.intensity = juce::jlimit (0.0f, 1.0f, m.intensity);
        const float targets[] { -14.0f, -11.0f, -9.0f };
        s.targetLufs = targets[juce::jlimit (0, 2, m.target)];

        // the loudest 30 s (by 1 s RMS), where the mastering is judged
        const int sec = (int) fs, window = juce::jmin (len, (int) (fs * 30.0));
        std::vector<double> power;
        for (int at = 0; at < len; at += sec)
        {
            const int n = juce::jmin (sec, len - at);
            double p = 0;
            for (int c = 0; c < 2; ++c) { const float r = audio.getRMSLevel (c, at, n); p += (double) r * r; }
            power.push_back (p);
        }
        const int secs = (int) power.size(), wSecs = juce::jmax (1, window / juce::jmax (1, sec));
        int best = 0; double bestSum = -1, run = 0;
        for (int i = 0; i < secs; ++i)
        {
            run += power[(size_t) i];
            if (i >= wSecs) run -= power[(size_t) (i - wSecs)];
            if (i >= wSecs - 1 && run > bestSum) { bestSum = run; best = i - wSecs + 1; }
        }
        const int from = juce::jlimit (0, juce::jmax (0, len - window), best * sec);

        // learn: the loudest stretch, looped, until the chain locks (it needs ~20 s of music)
        auto learner = std::make_unique<MC>();
        learner->prepare (fs, block);
        learner->setSettings (s);
        learner->startLearning();
        juce::AudioBuffer<float> tmp (2, block);
        for (int done = 0; learner->isLearning() && done < (int) (fs * 90.0); done += block)
        {
            const int at = from + (done % juce::jmax (block, window - block));
            const int n = juce::jmin (block, from + window - at);
            if (n <= 0) continue;
            for (int c = 0; c < 2; ++c) tmp.copyFrom (c, 0, audio, c, at, n);
            learner->process (tmp.getWritePointer (0), tmp.getWritePointer (1), n);
        }
        learner->finishLearningNow();
        const auto learned = learner->getLearned();
        learner = nullptr;

        // then the whole song, with that locked (and the chain's latency taken back out)
        auto chain = std::make_unique<MC>();
        chain->prepare (fs, block);
        chain->setSettings (s);
        chain->lockLearned (learned);
        const int latency = chain->getLatency();
        juce::AudioBuffer<float> out (2, len);
        int written = -latency;
        for (int at = 0; written < len; at += block)
        {
            const int n = block;
            tmp.clear();
            if (at < len)
                for (int c = 0; c < 2; ++c) tmp.copyFrom (c, 0, audio, c, at, juce::jmin (n, len - at));
            chain->process (tmp.getWritePointer (0), tmp.getWritePointer (1), n);
            for (int i = 0; i < n; ++i, ++written)
                if (written >= 0 && written < len)
                    for (int c = 0; c < 2; ++c) out.setSample (c, written, tmp.getSample (c, i));
        }
        audio = std::move (out);
    }

    /** Reads a rendered mix, masters it, and writes it out (WAV at the bit depth asked for, or FLAC).
        Safe to run off the message thread. Returns an empty string, or what went wrong. */
    inline juce::String masterFile (const juce::File& source, const juce::File& dest, const Settings& m, int bitDepth, bool flac)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
        if (reader == nullptr) return "couldn't read the rendered mix back";
        const double fs = reader->sampleRate;
        juce::AudioBuffer<float> audio (juce::jmax (2, (int) reader->numChannels), (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        reader = nullptr;
        masterBuffer (audio, fs, m);

        dest.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (dest.createOutputStream().release());
        if (stream == nullptr) return "couldn't write " + dest.getFullPathName();
        juce::WavAudioFormat wav;
        juce::FlacAudioFormat flacFormat;
        juce::AudioFormat& format = flac ? (juce::AudioFormat&) flacFormat : (juce::AudioFormat&) wav;
        auto options = juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2)
                           .withBitsPerSample (flac ? juce::jmin (24, bitDepth) : bitDepth)
                           .withSampleFormat (! flac && bitDepth == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                         : juce::AudioFormatWriterOptions::SampleFormat::integral);
        auto writer = format.createWriterFor (stream, options);
        if (writer == nullptr) return "couldn't write that format";
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()) ? juce::String() : "the write didn't finish";
    }

    //==============================================================================
    /** Plays two equal-length renders in sync through the audio interface (alongside
        Tracktion, which is stopped meanwhile), switching between them without a gap */
    class PreviewPlayer : public juce::AudioIODeviceCallback
    {
    public:
        void load (juce::AudioBuffer<float> mastered, juce::AudioBuffer<float> original, double rate)
        {
            const juce::ScopedLock sl (lock);
            a = std::move (mastered);
            b = std::move (original);
            fileRate = rate;
            pos = 0;
            // how much louder the mastered one is (RMS), for a fair "same loudness" comparison
            auto rms = [] (const juce::AudioBuffer<float>& x)
            {
                double s = 0;
                for (int c = 0; c < x.getNumChannels(); ++c) s += x.getRMSLevel (c, 0, x.getNumSamples()) * (double) x.getRMSLevel (c, 0, x.getNumSamples());
                return std::sqrt (s / juce::jmax (1, x.getNumChannels()));
            };
            const double ra = rms (a), rb = rms (b);
            matchGain = ra > 1.0e-6 && rb > 1.0e-6 ? (float) (rb / ra) : 1.0f;
            loudnessDifferenceDb = juce::Decibels::gainToDecibels ((float) (ra / juce::jmax (1.0e-9, rb)));
        }

        bool hasAudio() const { const juce::ScopedLock sl (lock); return a.getNumSamples() > 0; }
        float loudnessDifferenceDb = 0.0f;
        std::atomic<bool> playing { false }, original { false }, sameLoudness { false };

        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                               const juce::AudioIODeviceCallbackContext&) override
        {
            // JUCE mixes this into Tracktion's output, so write every sample (silence when not previewing)
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
            const juce::ScopedTryLock sl (lock);
            if (! sl.isLocked() || ! playing || a.getNumSamples() == 0 || std::abs (fileRate - deviceRate) > 1.0) return;
            const auto& src = original ? b : a;
            const float g = (! original && sameLoudness) ? matchGain : 1.0f;
            for (int i = 0; i < n; ++i)
            {
                if (pos >= src.getNumSamples()) pos = 0;   // loops
                for (int c = 0; c < juce::jmin (2, numOut); ++c)   // the main pair (outputs 1 and 2)
                    if (out[c] != nullptr) out[c][i] = g * src.getSample (juce::jmin (c, src.getNumChannels() - 1), pos);
                ++pos;
            }
        }
        void audioDeviceAboutToStart (juce::AudioIODevice* d) override { deviceRate = d->getCurrentSampleRate(); }
        void audioDeviceStopped() override {}

    private:
        juce::CriticalSection lock;
        juce::AudioBuffer<float> a, b;
        double fileRate = 0, deviceRate = 0;
        int pos = 0;
        float matchGain = 1.0f;
    };
}
