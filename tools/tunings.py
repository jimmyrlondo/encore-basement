# Encore: Studio
# Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
#
# The tuner's instruments and tunings, in one place. Running this writes
# Source/edit/Tunings.h (Studio's tuner) and tools/tunings.json (the web tuner),
# so both always offer the same list.
#
#   python tools/tunings.py
#
# Strings are written in the order you see them holding the instrument, low side
# first (so a 5-string banjo's short 5th string comes first). Pitches use
# scientific names (A4 = 440 Hz). "transpose" is for horns and reeds: the written
# note is the concert note plus that many semitones.

import json, os

HERE = os.path.dirname(os.path.abspath(__file__))

def T(name, notes, note=""):
    return {"name": name, "notes": notes.split(), "note": note}

CATALOG = [
    ("Guitars", [
        ("Guitar (6-string)", [
            T("Standard", "E2 A2 D3 G3 B3 E4"),
            T("Half step down (Eb)", "Eb2 Ab2 Db3 Gb3 Bb3 Eb4"),
            T("Whole step down (D)", "D2 G2 C3 F3 A3 D4"),
            T("C standard", "C2 F2 Bb2 Eb3 G3 C4"),
            T("B standard", "B1 E2 A2 D3 F#3 B3"),
            T("Drop D", "D2 A2 D3 G3 B3 E4"),
            T("Drop C#", "C#2 G#2 C#3 F#3 A#3 D#4"),
            T("Drop C", "C2 G2 C3 F3 A3 D4"),
            T("Drop B", "B1 F#2 B2 E3 G#3 C#4"),
            T("Drop A", "A1 E2 A2 D3 F#3 B3"),
            T("Double drop D", "D2 A2 D3 G3 B3 D4"),
            T("DADGAD", "D2 A2 D3 G3 A3 D4"),
            T("Open G", "D2 G2 D3 G3 B3 D4"),
            T("Open D", "D2 A2 D3 F#3 A3 D4"),
            T("Open E", "E2 B2 E3 G#3 B3 E4"),
            T("Open A", "E2 A2 E3 A3 C#4 E4"),
            T("Open C", "C2 G2 C3 G3 C4 E4"),
            T("Open D minor", "D2 A2 D3 F3 A3 D4"),
            T("Open G minor", "D2 G2 D3 G3 Bb3 D4"),
            T("Open C6", "C2 A2 C3 G3 C4 E4"),
            T("All fourths", "E2 A2 D3 G3 C4 F4"),
            T("New standard (Fripp)", "C2 G2 D3 A3 E4 G4"),
            T("Nashville (high strung)", "E3 A3 D4 G4 B3 E4", "The four low strings an octave up, from a 12-string set"),
            T("Open G (Keith Richards 5-string)", "G2 D3 G3 B3 D4"),
        ]),
        ("Guitar (12-string)", [
            T("Standard", "E2 A2 D3 G3 B3 E4", "Tune the main string of each pair; the octave strings sound an octave up"),
            T("Half step down (Eb)", "Eb2 Ab2 Db3 Gb3 Bb3 Eb4"),
            T("Drop D", "D2 A2 D3 G3 B3 E4"),
            T("Open G", "D2 G2 D3 G3 B3 D4"),
        ]),
        ("Guitar (7-string)", [
            T("Standard", "B1 E2 A2 D3 G3 B3 E4"),
            T("Drop A", "A1 E2 A2 D3 G3 B3 E4"),
            T("Half step down", "Bb1 Eb2 Ab2 Db3 Gb3 Bb3 Eb4"),
            T("Russian (7-string)", "D2 G2 B2 D3 G3 B3 D4", "Open G, the Russian guitar's own tuning"),
        ]),
        ("Guitar (8-string)", [
            T("Standard", "F#1 B1 E2 A2 D3 G3 B3 E4"),
            T("Drop E", "E1 B1 E2 A2 D3 G3 B3 E4"),
        ]),
        ("Baritone guitar", [
            T("B standard", "B1 E2 A2 D3 F#3 B3"),
            T("A standard", "A1 D2 G2 C3 E3 A3"),
        ]),
        ("Tenor guitar", [
            T("Standard (fifths)", "C3 G3 D4 A4"),
            T("Chicago", "D3 G3 B3 E4"),
        ]),
        ("Guitalele", [T("Standard", "A2 D3 G3 C4 E4 A4")]),
        ("Requinto guitar", [T("Standard", "A2 D3 G3 C4 E4 A4")]),
        ("Resonator / Dobro", [
            T("Open G (high bass)", "G2 B2 D3 G3 B3 D4"),
            T("Open D", "D2 A2 D3 F#3 A3 D4"),
        ]),
        ("Lap steel", [
            T("C6", "C3 E3 G3 A3 C4 E4"),
            T("A6", "C#3 E3 F#3 A3 C#4 E4"),
            T("Open E", "E2 B2 E3 G#3 B3 E4"),
            T("Open G", "D2 G2 D3 G3 B3 D4"),
        ]),
        ("Pedal steel (10-string)", [T("E9", "B2 D3 E3 F#3 G#3 B3 E4 G#4 D#4 F#4", "Strings 10 to 1, open (no pedals)")]),
    ]),
    ("Basses", [
        ("Bass (4-string)", [
            T("Standard", "E1 A1 D2 G2"),
            T("Drop D", "D1 A1 D2 G2"),
            T("Half step down", "Eb1 Ab1 Db2 Gb2"),
            T("D standard", "D1 G1 C2 F2"),
            T("Drop C", "C1 G1 C2 F2"),
            T("BEAD", "B0 E1 A1 D2"),
            T("Fifths (cello-style)", "C1 G1 D2 A2"),
            T("Piccolo bass", "E2 A2 D3 G3"),
        ]),
        ("Bass (5-string)", [
            T("Standard (low B)", "B0 E1 A1 D2 G2"),
            T("High C", "E1 A1 D2 G2 C3"),
            T("Drop A", "A0 E1 A1 D2 G2"),
        ]),
        ("Bass (6-string)", [T("Standard", "B0 E1 A1 D2 G2 C3")]),
        ("Upright / double bass", [
            T("Orchestral (fourths)", "E1 A1 D2 G2"),
            T("Solo tuning", "F#1 B1 E2 A2", "A whole step up, for solo playing with solo strings"),
            T("Fifths", "C1 G1 D2 A2"),
        ]),
        ("Acoustic bass guitar", [T("Standard", "E1 A1 D2 G2")]),
        ("Guitarron (Mexican)", [T("Standard", "A1 D2 G2 C3 E2 A2", "Re-entrant: the 5th and 6th strings drop back down")]),
        ("Bajo sexto", [T("Standard", "E2 A2 D3 G3 C4 F4")]),
    ]),
    ("Ukuleles", [
        ("Ukulele (soprano, concert, tenor)", [
            T("Standard (high G)", "G4 C4 E4 A4", "Re-entrant: the G string is higher than the C"),
            T("Low G", "G3 C4 E4 A4"),
            T("D tuning (old-style)", "A4 D4 F#4 B4"),
            T("Slack key (open C)", "G4 C4 E4 G4"),
        ]),
        ("Baritone ukulele", [T("Standard", "D3 G3 B3 E4")]),
        ("Bass ukulele", [T("Standard", "E1 A1 D2 G2")]),
        ("Banjolele", [T("Standard", "G4 C4 E4 A4")]),
    ]),
    ("Mandolins", [
        ("Mandolin", [
            T("Standard", "G3 D4 A4 E5"),
            T("Cross A (AEAE)", "A3 E4 A4 E5"),
            T("Open G (GDGD)", "G3 D4 G4 D5"),
            T("Cross G (GDGB)", "G3 D4 G4 B4"),
        ]),
        ("Mandola", [T("Standard", "C3 G3 D4 A4")]),
        ("Octave mandolin / mandocello", [
            T("Octave mandolin", "G2 D3 A3 E4"),
            T("Mandocello", "C2 G2 D3 A3"),
        ]),
    ]),
    ("Banjos", [
        ("Banjo (5-string)", [
            T("Open G", "G4 D3 G3 B3 D4", "The short 5th string comes first"),
            T("Double C", "G4 C3 G3 C4 D4"),
            T("Standard C", "G4 C3 G3 B3 D4"),
            T("Sawmill / modal (gDGCD)", "G4 D3 G3 C4 D4"),
            T("Open D", "F#4 D3 F#3 A3 D4"),
            T("Graveyard / open D minor", "F4 D3 F3 A3 D4"),
            T("Open A", "A4 E3 A3 C#4 E4"),
        ]),
        ("Tenor banjo", [
            T("Standard (fifths)", "C3 G3 D4 A4"),
            T("Irish", "G2 D3 A3 E4"),
        ]),
        ("Plectrum banjo", [T("Standard", "C3 G3 B3 D4")]),
        ("Banjo (6-string)", [T("Standard (guitar)", "E2 A2 D3 G3 B3 E4")]),
    ]),
    ("Orchestral strings", [
        ("Violin / fiddle", [
            T("Standard", "G3 D4 A4 E5"),
            T("Cross A (AEAE)", "A3 E4 A4 E5"),
            T("Cross G (GDGD)", "G3 D4 G4 D5"),
            T("ADAE", "A3 D4 A4 E5"),
            T("Calico (AEAC#)", "A3 E4 A4 C#5"),
            T("DDAD", "D3 D4 A4 D5"),
            T("Baroque (A = 415)", "G3 D4 A4 E5", "Set the reference to 415 Hz"),
        ]),
        ("Viola", [T("Standard", "C3 G3 D4 A4")]),
        ("Cello", [
            T("Standard", "C2 G2 D3 A3"),
            T("Bach suite 5 (scordatura)", "C2 G2 D3 G3"),
        ]),
        ("Hardanger fiddle", [T("Standard (ADAE)", "A3 D4 A4 E5", "Melody strings; tune the sympathetic strings by ear to the piece")]),
        ("Nyckelharpa", [T("Standard", "C3 G3 C4 A4", "The melody strings")]),
    ]),
    ("Early music", [
        ("Renaissance lute (6-course)", [
            T("In G", "G2 C3 F3 A3 D4 G4"),
            T("In A", "A2 D3 G3 B3 E4 A4"),
        ]),
        ("Renaissance lute (8-course)", [T("In G", "D2 F2 G2 C3 F3 A3 D4 G4")]),
        ("Baroque lute (13-course)", [T("D minor", "A1 B1 C2 D2 E2 F2 G2 A2 D3 F3 A3 D4 F4", "The basses are often retuned to the key of the piece")]),
        ("Viola da gamba", [
            T("Bass viol", "D2 G2 C3 E3 A3 D4"),
            T("Tenor viol", "G2 C3 F3 A3 D4 G4"),
            T("Treble viol", "D3 G3 C4 E4 A4 D5"),
            T("Violone in G", "G1 C2 F2 A2 D3 G3"),
        ]),
        ("Vihuela (Spanish)", [T("Standard", "G2 C3 F3 A3 D4 G4")]),
        ("Baroque guitar (5-course)", [T("Re-entrant", "A3 D4 G3 B3 E4", "The 4th and 5th courses tuned high")]),
        ("Hurdy-gurdy", [T("G/C", "C3 G3 G4 G4", "Drones C and G, two chanters on G")]),
    ]),
    ("Folk and world strings", [
        ("Bouzouki (Irish)", [
            T("GDAD", "G2 D3 A3 D4"),
            T("GDAE", "G2 D3 A3 E4"),
            T("ADAD", "A2 D3 A3 D4"),
        ]),
        ("Bouzouki (Greek)", [
            T("Tetrachordo (4-course)", "C3 F3 A3 D4"),
            T("Trichordo (3-course)", "D3 A3 D4"),
        ]),
        ("Oud", [
            T("Arabic", "C2 F2 A2 D3 G3 C4"),
            T("Turkish", "E2 A2 B2 E3 A3 D4"),
        ]),
        ("Appalachian dulcimer", [
            T("DAd (Ionian)", "D3 A3 D4"),
            T("DAA (Mixolydian)", "D3 A3 A3"),
            T("DGD", "D3 G3 D4"),
            T("CGc", "C3 G3 C4"),
        ]),
        ("Balalaika (prima)", [T("Standard", "E4 E4 A4")]),
        ("Domra (4-string)", [T("Standard", "G3 D4 A4 E5")]),
        ("Charango", [T("Standard", "G4 C5 E5 A4 E5", "Re-entrant: the middle course is the lowest")]),
        ("Cavaquinho", [T("Standard (DGBD)", "D4 G4 B4 D5")]),
        ("Cuatro (Venezuelan)", [T("Standard", "A3 D4 F#4 B3", "Re-entrant: the 1st string is lower than the 2nd")]),
        ("Cuatro (Puerto Rican)", [T("Standard", "B2 E3 A3 D4 G4")]),
        ("Tres cubano", [T("Standard", "G4 C4 E4")]),
        ("Vihuela (Mexican)", [T("Standard", "A3 D4 G4 B3 E4", "Re-entrant")]),
        ("Tiple (Colombian)", [T("Standard", "D4 G4 B4 E5")]),
        ("Bandurria", [T("Standard", "G#3 C#4 F#4 B4 E5 A5")]),
        ("Laud (Spanish)", [T("Standard", "G#2 C#3 F#3 B3 E4 A4")]),
        ("Kantele (5-string)", [T("Standard", "D4 E4 F4 G4 A4")]),
        ("Pipa", [T("Standard", "A2 D3 E3 A3")]),
        ("Zhongruan", [T("Standard", "G2 D3 G3 D4")]),
        ("Erhu", [T("Standard", "D4 A4")]),
        ("Shamisen", [
            T("Honchoshi", "C3 F3 C4", "Usually tuned to the singer: move it all up or down together"),
            T("Niagari", "C3 G3 C4"),
            T("Sansagari", "C3 F3 Bb3"),
        ]),
        ("Koto (13-string)", [T("Hirajoshi", "D3 G3 A3 Bb3 D4 Eb4 G4 A4 Bb4 D5 Eb5 G5 A5")]),
    ]),
    ("Winds and brass", [
        ("Flute", [T("Concert pitch", "", "Shows the note you read")]),
        ("Piccolo", [T("Concert pitch (sounds an octave up)", "", "Shows the note you read")]),
        ("Oboe", [T("Concert pitch", "")]),
        ("English horn (F)", [T("In F", "")]),
        ("Clarinet (Bb)", [T("In Bb", "")]),
        ("Clarinet (A)", [T("In A", "")]),
        ("Bass clarinet (Bb)", [T("In Bb", "")]),
        ("Bassoon", [T("Concert pitch", "")]),
        ("Soprano saxophone (Bb)", [T("In Bb", "")]),
        ("Alto saxophone (Eb)", [T("In Eb", "")]),
        ("Tenor saxophone (Bb)", [T("In Bb", "")]),
        ("Baritone saxophone (Eb)", [T("In Eb", "")]),
        ("Trumpet / cornet (Bb)", [T("In Bb", "")]),
        ("Flugelhorn (Bb)", [T("In Bb", "")]),
        ("French horn (F)", [T("In F", "")]),
        ("Mellophone (F)", [T("In F", "")]),
        ("Trombone", [T("Concert pitch", "")]),
        ("Euphonium / baritone horn", [T("Concert pitch (bass clef)", "")]),
        ("Tuba", [T("Concert pitch", "")]),
    ]),
    ("Anything else", [
        ("Chromatic (any instrument)", [T("Any note", "", "Voice, piano, harp, drums, handpan, didgeridoo, bagpipes...")]),
    ]),
]

# written = concert + this many semitones
TRANSPOSE = {
    "Piccolo": -12, "English horn (F)": 7, "Clarinet (Bb)": 2, "Clarinet (A)": 3, "Bass clarinet (Bb)": 14,
    "Soprano saxophone (Bb)": 2, "Alto saxophone (Eb)": 9, "Tenor saxophone (Bb)": 14, "Baritone saxophone (Eb)": 21,
    "Trumpet / cornet (Bb)": 2, "Flugelhorn (Bb)": 2, "French horn (F)": 7, "Mellophone (F)": 7,
}

# Temperaments: each pitch class's offset from equal temperament in cents, built on C
# (C, C#, D, Eb, E, F, F#, G, G#, A, Bb, B); the tuner turns them to the chosen key.
TEMPERAMENTS = [
    ("Equal temperament", [0] * 12),
    ("Just intonation (major)", [0, 11.7, 3.9, 15.6, -13.7, -2.0, -9.8, 2.0, 13.7, -15.6, 17.6, -11.7]),
    ("Pythagorean", [0, 13.7, 3.9, -5.9, 7.8, -2.0, 11.7, 2.0, 15.6, 5.9, -3.9, 9.8]),
    ("Quarter-comma meantone", [0, -24.0, -6.8, 10.3, -13.7, 3.4, -20.5, -3.4, -27.4, -10.3, 6.8, -17.1]),
    ("Werckmeister III", [0, -9.8, -7.8, -5.9, -9.8, -2.0, -11.7, -3.9, -7.8, -11.7, -3.9, -7.8]),
    ("Kirnberger III", [0, -9.8, -6.8, -5.9, -13.7, -2.0, -9.8, -3.4, -7.8, -10.3, -3.9, -11.7]),
    ("Vallotti", [0, -3.9, -2.0, 0, -5.9, 2.0, -5.9, -2.0, -2.0, -3.9, 2.0, -5.9]),
]

REFERENCES = [("440 Hz (modern)", 440), ("442 Hz (European orchestras)", 442), ("443 Hz", 443), ("441 Hz", 441), ("432 Hz", 432),
              ("430 Hz (classical era)", 430), ("415 Hz (baroque)", 415), ("466 Hz (Chorton)", 466)]

NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

def midi(note):
    """C4 = 60"""
    letter, rest = note[0], note[1:]
    pc = NAMES[letter]
    while rest and rest[0] in "#b":
        pc += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    return pc + 12 * (int(rest) + 1)

def check():
    for family, instruments in CATALOG:
        for name, tunings in instruments:
            for t in tunings:
                for n in t["notes"]:
                    m = midi(n)
                    assert 20 <= m <= 100, (name, t["name"], n)

def cpp_str(s):
    out = '"'
    for b in s.encode("utf-8"):
        c = chr(b)
        if b >= 128: out += "\\%03o" % b
        elif c in ('"', "\\"): out += "\\" + c
        else: out += c
    return out + '"'

def write_cpp():
    lines = []
    for family, instruments in CATALOG:
        lines.append("            { %s, {" % cpp_str(family))
        for name, tunings in instruments:
            ts = ", ".join("{ %s, { %s }, %s }" % (cpp_str(t["name"]), ", ".join(str(midi(n)) for n in t["notes"]), cpp_str(t["note"])) for t in tunings)
            lines.append("                { %s, %d, { %s } }," % (cpp_str(name), TRANSPOSE.get(name, 0), ts))
        lines.append("            } },")
    temps = ",\n".join("            { %s, { %s } }" % (cpp_str(n), ", ".join("%.1ff" % v for v in offs)) for n, offs in TEMPERAMENTS)
    refs = ", ".join("{ %s, %d }" % (cpp_str(n), v) for n, v in REFERENCES)
    body = open(os.path.join(HERE, "tunings_template.h"), encoding="utf-8").read()
    body = body.replace("@CATALOG@", "\n".join(lines)).replace("@TEMPERAMENTS@", temps).replace("@REFERENCES@", refs)
    open(os.path.join(HERE, "..", "Source", "edit", "Tunings.h"), "w", encoding="utf-8", newline="\n").write(body)

def write_json():
    data = {
        "families": [{"name": f, "instruments": [{"name": n, "transpose": TRANSPOSE.get(n, 0),
                                                   "tunings": [{"name": t["name"], "notes": t["notes"], "midi": [midi(x) for x in t["notes"]], "note": t["note"]} for t in ts]}
                                                  for n, ts in insts]} for f, insts in CATALOG],
        "temperaments": [{"name": n, "cents": offs} for n, offs in TEMPERAMENTS],
        "references": [{"name": n, "hz": v} for n, v in REFERENCES],
    }
    json.dump(data, open(os.path.join(HERE, "tunings.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)

if __name__ == "__main__":
    check()
    write_cpp()
    write_json()
    n = sum(len(i) for _, i in CATALOG)
    t = sum(len(ts) for _, i in CATALOG for _, ts in i)
    print(f"{n} instruments, {t} tunings, {len(TEMPERAMENTS)} temperaments")
