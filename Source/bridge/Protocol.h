// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// How Studio (64-bit) and its 32-bit plugin bridge talk.
//
//   audio, MIDI and parameter moves  shared memory, once per block: Studio fills
//                                    it in, signals "go", the bridge runs the plugin
//                                    and signals "done"
//   everything else                  messages on a named pipe (load, prepare, the
//                                    plugin's saved state, its window)
//
// The shared block uses fixed-size types only, so it has the same layout in a
// 32-bit and a 64-bit program.
#pragma once

#include <cstdint>

namespace bridge
{
    constexpr int maxChannels = 16, maxBlock = 8192, maxMidi = 1024, maxParamChanges = 512;

    struct MidiEvent   { int32_t offset, size; uint8_t data[4]; };
    struct ParamChange { int32_t index; float value; };

   #pragma pack (push, 8)
    struct Shared
    {
        // where the song is (the plugin's playhead)
        double sampleRate, bpm, ppq, timeSeconds, ppqBarStart, loopStartPpq, loopEndPpq;
        int32_t playing, recording, looping, sigNum, sigDen;
        // this block
        int32_t numSamples, numIn, numOut;
        int32_t numMidiIn, numMidiOut, numParamsIn, numParamsOut;
        int32_t latencySamples, pad;
        MidiEvent midiIn[maxMidi], midiOut[maxMidi];
        ParamChange paramsIn[maxParamChanges], paramsOut[maxParamChanges];
        float audio[maxChannels][maxBlock];   // in, then replaced by out
    };
   #pragma pack (pop)

    static_assert (sizeof (Shared) % 8 == 0, "same size in 32 and 64-bit");

    /** The names of one bridged plugin's shared memory and events ("Local\EncoreBridge-<pid>-<n>-mem"...) */
    inline std::wstring objectName (const juce::String& base, const char* what)
    {
        return (juce::String ("Local\\") + base + "-" + what).toWideCharPointer();
    }

    //==============================================================================
    // Pipe messages: a ValueTree each (its type says what it is), "id" pairs a request with its REPLY
    inline juce::MemoryBlock pack (const juce::ValueTree& v)
    {
        juce::MemoryOutputStream out;
        v.writeToStream (out);
        return out.getMemoryBlock();
    }

    inline juce::ValueTree unpack (const juce::MemoryBlock& m)
    {
        return juce::ValueTree::readFromData (m.getData(), m.getSize());
    }
}
