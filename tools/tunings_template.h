// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The tuner's instruments, tunings, temperaments and reference pitches.
// Made by tools/tunings.py (edit the list there and run it, not this file).
#pragma once

namespace tunings
{
    struct Tuning     { const char* name; std::vector<int> strings; const char* note; };   // strings: MIDI notes (60 = C4), low side first
    struct Instrument { const char* name; int transpose; std::vector<Tuning> tunings; };     // transpose: written = concert + this
    struct Family     { const char* name; std::vector<Instrument> instruments; };
    struct Temperament { const char* name; std::array<float, 12> cents; };                  // offsets from equal temperament, built on C
    struct Reference  { const char* name; int hz; };

    inline juce::String text (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }

    inline const std::vector<Family>& families()
    {
        static const std::vector<Family> list {
@CATALOG@
        };
        return list;
    }

    inline const std::vector<Temperament>& temperaments()
    {
        static const std::vector<Temperament> list {
@TEMPERAMENTS@
        };
        return list;
    }

    inline const std::vector<Reference>& references()
    {
        static const std::vector<Reference> list { @REFERENCES@ };
        return list;
    }
}
