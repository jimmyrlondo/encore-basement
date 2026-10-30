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
            { "Guitars", {
                { "Guitar (6-string)", 0, { { "Standard", { 40, 45, 50, 55, 59, 64 }, "" }, { "Half step down (Eb)", { 39, 44, 49, 54, 58, 63 }, "" }, { "Whole step down (D)", { 38, 43, 48, 53, 57, 62 }, "" }, { "C standard", { 36, 41, 46, 51, 55, 60 }, "" }, { "B standard", { 35, 40, 45, 50, 54, 59 }, "" }, { "Drop D", { 38, 45, 50, 55, 59, 64 }, "" }, { "Drop C#", { 37, 44, 49, 54, 58, 63 }, "" }, { "Drop C", { 36, 43, 48, 53, 57, 62 }, "" }, { "Drop B", { 35, 42, 47, 52, 56, 61 }, "" }, { "Drop A", { 33, 40, 45, 50, 54, 59 }, "" }, { "Double drop D", { 38, 45, 50, 55, 59, 62 }, "" }, { "DADGAD", { 38, 45, 50, 55, 57, 62 }, "" }, { "Open G", { 38, 43, 50, 55, 59, 62 }, "" }, { "Open D", { 38, 45, 50, 54, 57, 62 }, "" }, { "Open E", { 40, 47, 52, 56, 59, 64 }, "" }, { "Open A", { 40, 45, 52, 57, 61, 64 }, "" }, { "Open C", { 36, 43, 48, 55, 60, 64 }, "" }, { "Open D minor", { 38, 45, 50, 53, 57, 62 }, "" }, { "Open G minor", { 38, 43, 50, 55, 58, 62 }, "" }, { "Open C6", { 36, 45, 48, 55, 60, 64 }, "" }, { "All fourths", { 40, 45, 50, 55, 60, 65 }, "" }, { "New standard (Fripp)", { 36, 43, 50, 57, 64, 67 }, "" }, { "Nashville (high strung)", { 52, 57, 62, 67, 59, 64 }, "The four low strings an octave up, from a 12-string set" }, { "Open G (Keith Richards 5-string)", { 43, 50, 55, 59, 62 }, "" } } },
                { "Guitar (12-string)", 0, { { "Standard", { 40, 45, 50, 55, 59, 64 }, "Tune the main string of each pair; the octave strings sound an octave up" }, { "Half step down (Eb)", { 39, 44, 49, 54, 58, 63 }, "" }, { "Drop D", { 38, 45, 50, 55, 59, 64 }, "" }, { "Open G", { 38, 43, 50, 55, 59, 62 }, "" } } },
                { "Guitar (7-string)", 0, { { "Standard", { 35, 40, 45, 50, 55, 59, 64 }, "" }, { "Drop A", { 33, 40, 45, 50, 55, 59, 64 }, "" }, { "Half step down", { 34, 39, 44, 49, 54, 58, 63 }, "" }, { "Russian (7-string)", { 38, 43, 47, 50, 55, 59, 62 }, "Open G, the Russian guitar's own tuning" } } },
                { "Guitar (8-string)", 0, { { "Standard", { 30, 35, 40, 45, 50, 55, 59, 64 }, "" }, { "Drop E", { 28, 35, 40, 45, 50, 55, 59, 64 }, "" } } },
                { "Baritone guitar", 0, { { "B standard", { 35, 40, 45, 50, 54, 59 }, "" }, { "A standard", { 33, 38, 43, 48, 52, 57 }, "" } } },
                { "Tenor guitar", 0, { { "Standard (fifths)", { 48, 55, 62, 69 }, "" }, { "Chicago", { 50, 55, 59, 64 }, "" } } },
                { "Guitalele", 0, { { "Standard", { 45, 50, 55, 60, 64, 69 }, "" } } },
                { "Requinto guitar", 0, { { "Standard", { 45, 50, 55, 60, 64, 69 }, "" } } },
                { "Resonator / Dobro", 0, { { "Open G (high bass)", { 43, 47, 50, 55, 59, 62 }, "" }, { "Open D", { 38, 45, 50, 54, 57, 62 }, "" } } },
                { "Lap steel", 0, { { "C6", { 48, 52, 55, 57, 60, 64 }, "" }, { "A6", { 49, 52, 54, 57, 61, 64 }, "" }, { "Open E", { 40, 47, 52, 56, 59, 64 }, "" }, { "Open G", { 38, 43, 50, 55, 59, 62 }, "" } } },
                { "Pedal steel (10-string)", 0, { { "E9", { 47, 50, 52, 54, 56, 59, 64, 68, 63, 66 }, "Strings 10 to 1, open (no pedals)" } } },
            } },
            { "Basses", {
                { "Bass (4-string)", 0, { { "Standard", { 28, 33, 38, 43 }, "" }, { "Drop D", { 26, 33, 38, 43 }, "" }, { "Half step down", { 27, 32, 37, 42 }, "" }, { "D standard", { 26, 31, 36, 41 }, "" }, { "Drop C", { 24, 31, 36, 41 }, "" }, { "BEAD", { 23, 28, 33, 38 }, "" }, { "Fifths (cello-style)", { 24, 31, 38, 45 }, "" }, { "Piccolo bass", { 40, 45, 50, 55 }, "" } } },
                { "Bass (5-string)", 0, { { "Standard (low B)", { 23, 28, 33, 38, 43 }, "" }, { "High C", { 28, 33, 38, 43, 48 }, "" }, { "Drop A", { 21, 28, 33, 38, 43 }, "" } } },
                { "Bass (6-string)", 0, { { "Standard", { 23, 28, 33, 38, 43, 48 }, "" } } },
                { "Upright / double bass", 0, { { "Orchestral (fourths)", { 28, 33, 38, 43 }, "" }, { "Solo tuning", { 30, 35, 40, 45 }, "A whole step up, for solo playing with solo strings" }, { "Fifths", { 24, 31, 38, 45 }, "" } } },
                { "Acoustic bass guitar", 0, { { "Standard", { 28, 33, 38, 43 }, "" } } },
                { "Guitarron (Mexican)", 0, { { "Standard", { 33, 38, 43, 48, 40, 45 }, "Re-entrant: the 5th and 6th strings drop back down" } } },
                { "Bajo sexto", 0, { { "Standard", { 40, 45, 50, 55, 60, 65 }, "" } } },
            } },
            { "Ukuleles", {
                { "Ukulele (soprano, concert, tenor)", 0, { { "Standard (high G)", { 67, 60, 64, 69 }, "Re-entrant: the G string is higher than the C" }, { "Low G", { 55, 60, 64, 69 }, "" }, { "D tuning (old-style)", { 69, 62, 66, 71 }, "" }, { "Slack key (open C)", { 67, 60, 64, 67 }, "" } } },
                { "Baritone ukulele", 0, { { "Standard", { 50, 55, 59, 64 }, "" } } },
                { "Bass ukulele", 0, { { "Standard", { 28, 33, 38, 43 }, "" } } },
                { "Banjolele", 0, { { "Standard", { 67, 60, 64, 69 }, "" } } },
            } },
            { "Mandolins", {
                { "Mandolin", 0, { { "Standard", { 55, 62, 69, 76 }, "" }, { "Cross A (AEAE)", { 57, 64, 69, 76 }, "" }, { "Open G (GDGD)", { 55, 62, 67, 74 }, "" }, { "Cross G (GDGB)", { 55, 62, 67, 71 }, "" } } },
                { "Mandola", 0, { { "Standard", { 48, 55, 62, 69 }, "" } } },
                { "Octave mandolin / mandocello", 0, { { "Octave mandolin", { 43, 50, 57, 64 }, "" }, { "Mandocello", { 36, 43, 50, 57 }, "" } } },
            } },
            { "Banjos", {
                { "Banjo (5-string)", 0, { { "Open G", { 67, 50, 55, 59, 62 }, "The short 5th string comes first" }, { "Double C", { 67, 48, 55, 60, 62 }, "" }, { "Standard C", { 67, 48, 55, 59, 62 }, "" }, { "Sawmill / modal (gDGCD)", { 67, 50, 55, 60, 62 }, "" }, { "Open D", { 66, 50, 54, 57, 62 }, "" }, { "Graveyard / open D minor", { 65, 50, 53, 57, 62 }, "" }, { "Open A", { 69, 52, 57, 61, 64 }, "" } } },
                { "Tenor banjo", 0, { { "Standard (fifths)", { 48, 55, 62, 69 }, "" }, { "Irish", { 43, 50, 57, 64 }, "" } } },
                { "Plectrum banjo", 0, { { "Standard", { 48, 55, 59, 62 }, "" } } },
                { "Banjo (6-string)", 0, { { "Standard (guitar)", { 40, 45, 50, 55, 59, 64 }, "" } } },
            } },
            { "Orchestral strings", {
                { "Violin / fiddle", 0, { { "Standard", { 55, 62, 69, 76 }, "" }, { "Cross A (AEAE)", { 57, 64, 69, 76 }, "" }, { "Cross G (GDGD)", { 55, 62, 67, 74 }, "" }, { "ADAE", { 57, 62, 69, 76 }, "" }, { "Calico (AEAC#)", { 57, 64, 69, 73 }, "" }, { "DDAD", { 50, 62, 69, 74 }, "" }, { "Baroque (A = 415)", { 55, 62, 69, 76 }, "Set the reference to 415 Hz" } } },
                { "Viola", 0, { { "Standard", { 48, 55, 62, 69 }, "" } } },
                { "Cello", 0, { { "Standard", { 36, 43, 50, 57 }, "" }, { "Bach suite 5 (scordatura)", { 36, 43, 50, 55 }, "" } } },
                { "Hardanger fiddle", 0, { { "Standard (ADAE)", { 57, 62, 69, 76 }, "Melody strings; tune the sympathetic strings by ear to the piece" } } },
                { "Nyckelharpa", 0, { { "Standard", { 48, 55, 60, 69 }, "The melody strings" } } },
            } },
            { "Early music", {
                { "Renaissance lute (6-course)", 0, { { "In G", { 43, 48, 53, 57, 62, 67 }, "" }, { "In A", { 45, 50, 55, 59, 64, 69 }, "" } } },
                { "Renaissance lute (8-course)", 0, { { "In G", { 38, 41, 43, 48, 53, 57, 62, 67 }, "" } } },
                { "Baroque lute (13-course)", 0, { { "D minor", { 33, 35, 36, 38, 40, 41, 43, 45, 50, 53, 57, 62, 65 }, "The basses are often retuned to the key of the piece" } } },
                { "Viola da gamba", 0, { { "Bass viol", { 38, 43, 48, 52, 57, 62 }, "" }, { "Tenor viol", { 43, 48, 53, 57, 62, 67 }, "" }, { "Treble viol", { 50, 55, 60, 64, 69, 74 }, "" }, { "Violone in G", { 31, 36, 41, 45, 50, 55 }, "" } } },
                { "Vihuela (Spanish)", 0, { { "Standard", { 43, 48, 53, 57, 62, 67 }, "" } } },
                { "Baroque guitar (5-course)", 0, { { "Re-entrant", { 57, 62, 55, 59, 64 }, "The 4th and 5th courses tuned high" } } },
                { "Hurdy-gurdy", 0, { { "G/C", { 48, 55, 67, 67 }, "Drones C and G, two chanters on G" } } },
            } },
            { "Folk and world strings", {
                { "Bouzouki (Irish)", 0, { { "GDAD", { 43, 50, 57, 62 }, "" }, { "GDAE", { 43, 50, 57, 64 }, "" }, { "ADAD", { 45, 50, 57, 62 }, "" } } },
                { "Bouzouki (Greek)", 0, { { "Tetrachordo (4-course)", { 48, 53, 57, 62 }, "" }, { "Trichordo (3-course)", { 50, 57, 62 }, "" } } },
                { "Oud", 0, { { "Arabic", { 36, 41, 45, 50, 55, 60 }, "" }, { "Turkish", { 40, 45, 47, 52, 57, 62 }, "" } } },
                { "Appalachian dulcimer", 0, { { "DAd (Ionian)", { 50, 57, 62 }, "" }, { "DAA (Mixolydian)", { 50, 57, 57 }, "" }, { "DGD", { 50, 55, 62 }, "" }, { "CGc", { 48, 55, 60 }, "" } } },
                { "Balalaika (prima)", 0, { { "Standard", { 64, 64, 69 }, "" } } },
                { "Domra (4-string)", 0, { { "Standard", { 55, 62, 69, 76 }, "" } } },
                { "Charango", 0, { { "Standard", { 67, 72, 76, 69, 76 }, "Re-entrant: the middle course is the lowest" } } },
                { "Cavaquinho", 0, { { "Standard (DGBD)", { 62, 67, 71, 74 }, "" } } },
                { "Cuatro (Venezuelan)", 0, { { "Standard", { 57, 62, 66, 59 }, "Re-entrant: the 1st string is lower than the 2nd" } } },
                { "Cuatro (Puerto Rican)", 0, { { "Standard", { 47, 52, 57, 62, 67 }, "" } } },
                { "Tres cubano", 0, { { "Standard", { 67, 60, 64 }, "" } } },
                { "Vihuela (Mexican)", 0, { { "Standard", { 57, 62, 67, 59, 64 }, "Re-entrant" } } },
                { "Tiple (Colombian)", 0, { { "Standard", { 62, 67, 71, 76 }, "" } } },
                { "Bandurria", 0, { { "Standard", { 56, 61, 66, 71, 76, 81 }, "" } } },
                { "Laud (Spanish)", 0, { { "Standard", { 44, 49, 54, 59, 64, 69 }, "" } } },
                { "Kantele (5-string)", 0, { { "Standard", { 62, 64, 65, 67, 69 }, "" } } },
                { "Pipa", 0, { { "Standard", { 45, 50, 52, 57 }, "" } } },
                { "Zhongruan", 0, { { "Standard", { 43, 50, 55, 62 }, "" } } },
                { "Erhu", 0, { { "Standard", { 62, 69 }, "" } } },
                { "Shamisen", 0, { { "Honchoshi", { 48, 53, 60 }, "Usually tuned to the singer: move it all up or down together" }, { "Niagari", { 48, 55, 60 }, "" }, { "Sansagari", { 48, 53, 58 }, "" } } },
                { "Koto (13-string)", 0, { { "Hirajoshi", { 50, 55, 57, 58, 62, 63, 67, 69, 70, 74, 75, 79, 81 }, "" } } },
            } },
            { "Winds and brass", {
                { "Flute", 0, { { "Concert pitch", {  }, "Shows the note you read" } } },
                { "Piccolo", -12, { { "Concert pitch (sounds an octave up)", {  }, "Shows the note you read" } } },
                { "Oboe", 0, { { "Concert pitch", {  }, "" } } },
                { "English horn (F)", 7, { { "In F", {  }, "" } } },
                { "Clarinet (Bb)", 2, { { "In Bb", {  }, "" } } },
                { "Clarinet (A)", 3, { { "In A", {  }, "" } } },
                { "Bass clarinet (Bb)", 14, { { "In Bb", {  }, "" } } },
                { "Bassoon", 0, { { "Concert pitch", {  }, "" } } },
                { "Soprano saxophone (Bb)", 2, { { "In Bb", {  }, "" } } },
                { "Alto saxophone (Eb)", 9, { { "In Eb", {  }, "" } } },
                { "Tenor saxophone (Bb)", 14, { { "In Bb", {  }, "" } } },
                { "Baritone saxophone (Eb)", 21, { { "In Eb", {  }, "" } } },
                { "Trumpet / cornet (Bb)", 2, { { "In Bb", {  }, "" } } },
                { "Flugelhorn (Bb)", 2, { { "In Bb", {  }, "" } } },
                { "French horn (F)", 7, { { "In F", {  }, "" } } },
                { "Mellophone (F)", 7, { { "In F", {  }, "" } } },
                { "Trombone", 0, { { "Concert pitch", {  }, "" } } },
                { "Euphonium / baritone horn", 0, { { "Concert pitch (bass clef)", {  }, "" } } },
                { "Tuba", 0, { { "Concert pitch", {  }, "" } } },
            } },
            { "Anything else", {
                { "Chromatic (any instrument)", 0, { { "Any note", {  }, "Voice, piano, harp, drums, handpan, didgeridoo, bagpipes..." } } },
            } },
        };
        return list;
    }

    inline const std::vector<Temperament>& temperaments()
    {
        static const std::vector<Temperament> list {
            { "Equal temperament", { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            { "Just intonation (major)", { 0.0f, 11.7f, 3.9f, 15.6f, -13.7f, -2.0f, -9.8f, 2.0f, 13.7f, -15.6f, 17.6f, -11.7f } },
            { "Pythagorean", { 0.0f, 13.7f, 3.9f, -5.9f, 7.8f, -2.0f, 11.7f, 2.0f, 15.6f, 5.9f, -3.9f, 9.8f } },
            { "Quarter-comma meantone", { 0.0f, -24.0f, -6.8f, 10.3f, -13.7f, 3.4f, -20.5f, -3.4f, -27.4f, -10.3f, 6.8f, -17.1f } },
            { "Werckmeister III", { 0.0f, -9.8f, -7.8f, -5.9f, -9.8f, -2.0f, -11.7f, -3.9f, -7.8f, -11.7f, -3.9f, -7.8f } },
            { "Kirnberger III", { 0.0f, -9.8f, -6.8f, -5.9f, -13.7f, -2.0f, -9.8f, -3.4f, -7.8f, -10.3f, -3.9f, -11.7f } },
            { "Vallotti", { 0.0f, -3.9f, -2.0f, 0.0f, -5.9f, 2.0f, -5.9f, -2.0f, -2.0f, -3.9f, 2.0f, -5.9f } }
        };
        return list;
    }

    inline const std::vector<Reference>& references()
    {
        static const std::vector<Reference> list { { "440 Hz (modern)", 440 }, { "442 Hz (European orchestras)", 442 }, { "443 Hz", 443 }, { "441 Hz", 441 }, { "432 Hz", 432 }, { "430 Hz (classical era)", 430 }, { "415 Hz (baroque)", 415 }, { "466 Hz (Chorton)", 466 } };
        return list;
    }
}
