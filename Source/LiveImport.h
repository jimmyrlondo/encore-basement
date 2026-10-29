#pragma once

// File > Import > Live Recording: an Encore Live session (session.json beside
// the raw recording), or any multichannel WAV from a live recorder or a mixing
// desk's card. Every input becomes its own track; Encore Live's song marks
// become markers, or each song becomes its own Basement song to mix.
//
// session.json ("encore-live-session", written by Encore Live's Save for Basement):
//   { "kind": "encore-live-session", "version": 1,
//     "recording": { "file": "recording.wav", "channelNames": ["Kick", "Snare", ...] },
//     "show": { "title": "...", "artist": "...", "venue": "...", "date": "2026-10-28" },
//     "songs": [ { "title": "Opener", "start": 12.4, "end": 251.0 }, ... ] }

namespace liveimport
{
    struct Song { juce::String title; double start = 0, end = 0; };

    struct Session
    {
        juce::File recording;          // one WAV with every input in it
        juce::StringArray names;       // a name per input (from Encore Live), or "Input 1"...
        juce::String title, artist, venue, date;
        juce::Array<Song> songs;
        int channels = 0;
        double sampleRate = 0, seconds = 0;
        juce::String problem;
    };

    inline Session read (const juce::File& file)
    {
        Session s;
        if (file.hasFileExtension ("json"))
        {
            const auto json = juce::JSON::parse (file);
            if (json["kind"].toString() != "encore-live-session") { s.problem = file.getFileName() + " isn't an Encore Live session."; return s; }
            const auto rec = json["recording"];
            s.recording = file.getSiblingFile (rec["file"].toString());
            if (auto* names = rec["channelNames"].getArray()) for (auto& n : *names) s.names.add (n.toString());
            const auto show = json["show"];
            s.title = show["title"].toString(); s.artist = show["artist"].toString();
            s.venue = show["venue"].toString(); s.date = show["date"].toString();
            if (auto* songs = json["songs"].getArray())
                for (auto& x : *songs)
                    if ((double) x["end"] > (double) x["start"]) s.songs.add ({ x["title"].toString(), (double) x["start"], (double) x["end"] });
        }
        else s.recording = file;

        if (! s.recording.existsAsFile()) { s.problem = "Couldn't find the recording (" + s.recording.getFileName() + ") next to the session."; return s; }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (s.recording));
        if (r == nullptr) { s.problem = "Couldn't read " + s.recording.getFileName() + " as audio."; return s; }
        s.channels = (int) r->numChannels;
        s.sampleRate = r->sampleRate;
        s.seconds = r->lengthInSamples / r->sampleRate;
        for (int c = s.names.size(); c < s.channels; ++c) s.names.add ("Input " + juce::String (c + 1));
        s.names.removeRange (s.channels, s.names.size());
        if (s.title.isEmpty()) s.title = s.recording.getFileNameWithoutExtension();
        return s;
    }

    struct Input { juce::File file; juce::String name; float peak = 0; };

    /** Writes every input of the recording to its own mono file in folder ("03 Snare.wav"),
        keeping the recording's format (32-bit float stays float). Inputs that never get above
        -60 dB are left out when asked. Safe off the message thread; progress goes 0..1. */
    inline juce::Array<Input> split (const Session& s, const juce::File& folder, bool leaveOutSilent,
                                     std::function<void (float)> progress, juce::String& problem)
    {
        juce::Array<Input> out;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (s.recording));
        if (r == nullptr) { problem = "couldn't read the recording"; return out; }
        folder.createDirectory();
        const int channels = (int) r->numChannels;
        const bool isFloat = r->usesFloatingPointData;
        const int bits = isFloat ? 32 : juce::jmax (16, (int) r->bitsPerSample);
        juce::WavAudioFormat wav;
        std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
        for (int c = 0; c < channels; ++c)
        {
            Input in;
            in.name = s.names[c];
            in.file = folder.getChildFile (juce::String (c + 1).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (in.name) + ".wav");
            in.file.deleteFile();
            std::unique_ptr<juce::OutputStream> os (in.file.createOutputStream().release());
            auto w = os != nullptr ? wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (r->sampleRate).withNumChannels (1).withBitsPerSample (bits)
                                                                .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                                           : juce::AudioFormatWriterOptions::SampleFormat::integral))
                                   : nullptr;
            if (w == nullptr) { problem = "couldn't write " + in.file.getFullPathName(); return {}; }
            writers.push_back (std::move (w));
            out.add (in);
        }
        const int block = 1 << 16;
        juce::AudioBuffer<float> buf (channels, block);
        for (juce::int64 at = 0; at < r->lengthInSamples; at += block)
        {
            const int n = (int) juce::jmin ((juce::int64) block, r->lengthInSamples - at);
            r->read (&buf, 0, n, at, true, true);
            for (int c = 0; c < channels; ++c)
            {
                const float* p = buf.getReadPointer (c);
                out.getReference (c).peak = juce::jmax (out.getReference (c).peak, buf.getMagnitude (c, 0, n));
                writers[(size_t) c]->writeFromFloatArrays (&p, 1, n);
            }
            if (progress) progress ((float) ((at + n) / (double) r->lengthInSamples));
        }
        writers.clear();
        if (leaveOutSilent)
            for (int c = out.size(); --c >= 0;)
                if (out[c].peak < juce::Decibels::decibelsToGain (-60.0f)) { out[c].file.deleteFile(); out.remove (c); }
        return out;
    }

    /** A group for an input, guessed from its name ("Kick" -> Drums), or none */
    inline juce::String groupFor (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        for (auto w : { "kick", "snare", "tom", "hat", "overhead", "oh ", "room", "cymbal", "ride", "drum" }) if (n.contains (w) || n == "oh") return "Drums";
        for (auto w : { "vocal", "vox", "voice", "bgv", "bv" }) if (n.contains (w)) return "Vocals";
        for (auto w : { "guitar", "gtr" }) if (n.contains (w)) return "Guitars";
        return {};
    }

    /** "1:02:03" or "4:05" */
    inline juce::String clock (double seconds)
    {
        const int t = (int) seconds, h = t / 3600, m = (t / 60) % 60, sec = t % 60;
        return (h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) : juce::String (m)) + ":" + juce::String (sec).paddedLeft ('0', 2);
    }
}
