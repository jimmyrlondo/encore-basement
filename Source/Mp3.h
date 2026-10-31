#pragma once

// MP3 export with LAME (external/lame, LGPL). Tracktion renders the mix to a
// temporary float WAV as usual (so loudness and the progress bar work the
// same), then this turns that file into an MP3.

#include <lame.h>

namespace mp3
{
    /** The rates an MP3 can carry that are closest to the song's (44.1 kHz family or 48 kHz family) */
    inline double rateFor (double songRate)
    {
        return std::fmod (songRate, 48000.0) == 0.0 || songRate == 32000.0 ? 48000.0 : 44100.0;
    }

    /** "TIT2" etc. as UTF-16 with a byte order mark, which is what LAME wants for non-English titles */
    inline void setText (lame_t gf, const char* frame, const juce::String& text)
    {
        if (text.isEmpty()) return;
        std::vector<unsigned short> utf16 { 0xfeff };
        for (auto p = text.toUTF16(); ! p.isEmpty(); ++p) utf16.push_back ((unsigned short) *p.getAddress());
        utf16.push_back (0);
        id3tag_set_textinfo_utf16 (gf, frame, utf16.data());
    }

    struct Tags { juce::String title, artist, album, genre; };

    /** Encodes an audio file to a constant-bitrate MP3. Safe to run off the message thread.
        Returns an empty string, or what went wrong. */
    inline juce::String encode (const juce::File& source, const juce::File& dest, int kbps, const Tags& tags)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
        if (reader == nullptr) return "couldn't read the rendered mix back";

        const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
        std::unique_ptr<lame_global_flags, decltype (&lame_close)> gf (lame_init(), &lame_close);
        if (gf == nullptr) return "the MP3 encoder didn't start";
        lame_set_num_channels (gf.get(), channels);
        lame_set_in_samplerate (gf.get(), (int) reader->sampleRate);
        lame_set_out_samplerate (gf.get(), (int) rateFor (reader->sampleRate));
        lame_set_mode (gf.get(), channels == 1 ? MONO : JOINT_STEREO);
        lame_set_VBR (gf.get(), vbr_off);
        lame_set_brate (gf.get(), kbps);
        lame_set_quality (gf.get(), 2);   // 2 = near-best, still quick
        lame_set_write_id3tag_automatic (gf.get(), 0);
        id3tag_init (gf.get());
        id3tag_v2_only (gf.get());
        setText (gf.get(), "TIT2", tags.title);
        setText (gf.get(), "TPE1", tags.artist);
        setText (gf.get(), "TALB", tags.album);
        setText (gf.get(), "TCON", tags.genre);
        setText (gf.get(), "TSSE", "Encore: Basement Studio (LAME 3.100)");
        if (lame_init_params (gf.get()) < 0) return "the MP3 encoder didn't accept the settings";

        dest.deleteFile();
        juce::FileOutputStream out (dest);
        if (! out.openedOk()) return "couldn't write " + dest.getFullPathName();

        std::vector<unsigned char> buffer (1 << 16);
        const auto id3Size = lame_get_id3v2_tag (gf.get(), buffer.data(), buffer.size());
        out.write (buffer.data(), id3Size);
        const auto firstFrame = out.getPosition();   // where the LAME info frame goes at the end

        const int block = 8192;
        juce::AudioBuffer<float> pcm (channels, block);
        buffer.resize ((size_t) (block * 5 / 4 + 7200));
        for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += block)
        {
            const int n = (int) juce::jmin ((juce::int64) block, reader->lengthInSamples - pos);
            reader->read (&pcm, 0, n, pos, true, channels > 1);
            const int bytes = lame_encode_buffer_ieee_float (gf.get(), pcm.getReadPointer (0), pcm.getReadPointer (channels > 1 ? 1 : 0),
                                                             n, buffer.data(), (int) buffer.size());
            if (bytes < 0) return "the MP3 encoder stopped (" + juce::String (bytes) + ")";
            out.write (buffer.data(), (size_t) bytes);
        }
        const int tail = lame_encode_flush (gf.get(), buffer.data(), (int) buffer.size());
        if (tail > 0) out.write (buffer.data(), (size_t) tail);

        // the info frame tells players the exact length and gapless padding
        const auto infoSize = lame_get_lametag_frame (gf.get(), buffer.data(), buffer.size());
        if (infoSize > 0 && infoSize <= buffer.size() && out.setPosition (firstFrame))
            out.write (buffer.data(), infoSize);
        out.flush();
        return out.getStatus().wasOk() ? juce::String() : out.getStatus().getErrorMessage();
    }
}
