// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// What a song is beyond its audio: title, artist, album, genre, key, notes and
// the recording format, saved inside the song file (a BASEMENTSONG child of the
// edit) so it travels with the song and can later go straight to Encore.
// Also the recent-songs list the Hub shows, and song templates.
#pragma once

//==============================================================================
struct SongInfo
{
    juce::String title, artist, album, genre, key, notes;
    double bpm = 120.0;
    int sigNum = 4, sigDen = 4;
    double sampleRate = 48000.0;
    int bitDepth = 24;
    juce::Time created;

    static inline const juce::Identifier id { "BASEMENTSONG" };

    void writeTo (te::Edit& edit) const
    {
        auto v = edit.state.getOrCreateChildWithName (id, nullptr);
        v.setProperty ("title", title, nullptr);
        v.setProperty ("artist", artist, nullptr);
        v.setProperty ("album", album, nullptr);
        v.setProperty ("genre", genre, nullptr);
        v.setProperty ("key", key, nullptr);
        v.setProperty ("notes", notes, nullptr);
        v.setProperty ("sampleRate", sampleRate, nullptr);
        v.setProperty ("bitDepth", bitDepth, nullptr);
        v.setProperty ("created", created.toMilliseconds() > 0 ? created.toMilliseconds() : juce::Time::currentTimeMillis(), nullptr);
        if (auto* t = edit.tempoSequence.getTempo (0)) t->setBpm (bpm);
        if (auto* s = edit.tempoSequence.getTimeSig (0)) { s->numerator = sigNum; s->denominator = sigDen; }
    }

    static SongInfo readFrom (te::Edit& edit, const juce::File& songFile)
    {
        SongInfo i;
        auto v = edit.state.getChildWithName (id);
        i.title = v.getProperty ("title", songFile.getFileNameWithoutExtension());
        i.artist = v.getProperty ("artist");
        i.album = v.getProperty ("album");
        i.genre = v.getProperty ("genre");
        i.key = v.getProperty ("key");
        i.notes = v.getProperty ("notes");
        i.sampleRate = v.getProperty ("sampleRate", 48000.0);
        i.bitDepth = v.getProperty ("bitDepth", 24);
        i.created = juce::Time ((juce::int64) v.getProperty ("created", songFile.getCreationTime().toMilliseconds()));
        if (auto* t = edit.tempoSequence.getTempo (0)) i.bpm = t->getBpm();
        if (auto* s = edit.tempoSequence.getTimeSig (0)) { i.sigNum = s->numerator; i.sigDen = s->denominator; }
        return i;
    }

    juce::String sigText() const { return juce::String (sigNum) + "/" + juce::String (sigDen); }
    juce::String bpmText() const { auto s = juce::String (bpm, 1); return s.endsWith (".0") ? s.dropLastCharacters (2) : s; }
    juce::String formatText() const
    {
        auto khz = juce::String (sampleRate / 1000.0, 1);
        if (khz.endsWith (".0")) khz = khz.dropLastCharacters (2);
        return khz + " kHz / " + (bitDepth == 32 ? juce::String ("32-bit float") : juce::String (bitDepth) + "-bit");
    }
};

//==============================================================================
/** The Hub's "Recent" list, kept in Basement's settings. Each entry is a small
    summary written when a song is saved, so the list shows real details
    without opening every song. */
namespace recent
{
    struct Entry
    {
        juce::File file;
        juce::String title, artist, album, key, sig, format;
        double bpm = 0, lengthSeconds = 0;
        int tracks = 0;
        juce::Time saved;
    };

    inline juce::Array<Entry> load (juce::PropertiesFile& props)
    {
        juce::Array<Entry> out;
        if (auto xml = props.getXmlValue ("recentSongs"))
            for (auto* e : xml->getChildIterator())
            {
                Entry r;
                r.file = juce::File (e->getStringAttribute ("path"));
                r.title = e->getStringAttribute ("title", r.file.getFileNameWithoutExtension());
                r.artist = e->getStringAttribute ("artist");
                r.album = e->getStringAttribute ("album");
                r.key = e->getStringAttribute ("key");
                r.sig = e->getStringAttribute ("sig");
                r.format = e->getStringAttribute ("format");
                r.bpm = e->getDoubleAttribute ("bpm");
                r.lengthSeconds = e->getDoubleAttribute ("length");
                r.tracks = e->getIntAttribute ("tracks");
                r.saved = juce::Time (e->getStringAttribute ("saved").getLargeIntValue());
                if (r.file != juce::File()) out.add (r);
            }
        return out;
    }

    inline void save (juce::PropertiesFile& props, const juce::Array<Entry>& list)
    {
        juce::XmlElement xml ("RECENT");
        for (auto& r : list)
        {
            auto* e = xml.createNewChildElement ("SONG");
            e->setAttribute ("path", r.file.getFullPathName());
            e->setAttribute ("title", r.title);
            e->setAttribute ("artist", r.artist);
            e->setAttribute ("album", r.album);
            e->setAttribute ("key", r.key);
            e->setAttribute ("sig", r.sig);
            e->setAttribute ("format", r.format);
            e->setAttribute ("bpm", r.bpm);
            e->setAttribute ("length", r.lengthSeconds);
            e->setAttribute ("tracks", r.tracks);
            e->setAttribute ("saved", juce::String (r.saved.toMilliseconds()));
        }
        props.setValue ("recentSongs", &xml);
        props.saveIfNeeded();
    }

    /** Puts this song at the top of the list with up-to-date details */
    inline void remember (juce::PropertiesFile& props, te::Edit& edit, const juce::File& songFile, juce::Time saved = juce::Time::getCurrentTime())
    {
        auto info = SongInfo::readFrom (edit, songFile);
        Entry r;
        r.file = songFile;
        r.title = info.title;
        r.artist = info.artist;
        r.album = info.album;
        r.key = info.key;
        r.sig = info.sigText();
        r.format = info.formatText();
        r.bpm = info.bpm;
        r.lengthSeconds = edit.getLength().inSeconds();
        r.tracks = te::getAudioTracks (edit).size();
        r.saved = saved;

        auto list = load (props);
        for (int i = list.size(); --i >= 0;)
            if (list.getReference (i).file == songFile) list.remove (i);
        list.insert (0, r);
        while (list.size() > 40) list.removeLast();
        save (props, list);
    }

    inline void forget (juce::PropertiesFile& props, const juce::File& songFile)
    {
        auto list = load (props);
        for (int i = list.size(); --i >= 0;)
            if (list.getReference (i).file == songFile) list.remove (i);
        save (props, list);
    }
}

//==============================================================================
/** Everything the New song form decides, so a song can be created from it with
    or without a window (the self-test does the same thing headless). */
struct TrackPlan
{
    juce::String name;
    juce::String input; // a Basement input name ("Volt 476 1"...), or empty for none
    juce::String group; // a group track this one plays into ("Drums"), or empty
};

struct SongPlan
{
    SongInfo info;
    juce::File folder;               // the song's own folder (created)
    juce::Array<TrackPlan> tracks;
    juce::File templateFile;         // a saved template to start from, or none
    bool importAfterCreate = false;  // "Mix stems": ask for files right away

    juce::File songFile() const { return folder.getChildFile (folder.getFileName() + ".basement"); }
};

/** Templates: the built-in starting points, plus any the user saved */
namespace templates
{
    struct Template
    {
        juce::String name, description;
        juce::StringArray trackNames;   // empty with allInputs = one track per input
        bool allInputs = false, inputs = true, importStems = false;
        double bpm = 120;
        juce::File file;                // a user template
        juce::StringArray groups;       // per track: the group it plays into ("Drums"), or empty
        juce::Array<int> inputNumbers;  // per track: which input (1 = the first), 0 = the next free one
        int needsInputs = 0;            // inputs recorded at once (the tile says if yours has fewer)

        /** The input for track i, given the interface's inputs: the template's own pick if there is one
            (wrapping round on a smaller interface), else the next one nobody's using */
        juce::String inputFor (int i, const juce::StringArray& available, const juce::StringArray& used) const
        {
            if (! inputs || available.isEmpty()) return {};
            const int n = inputNumbers[i];
            if (n > 0) return available[(n - 1) % available.size()];
            for (auto& in : available) if (! used.contains (in)) return in;
            return {};
        }
    };

    /** A band layout: tracks as "name|input|group" (input and group optional) */
    inline Template band (const juce::String& name, const juce::String& description, int needs, std::initializer_list<const char*> tracks)
    {
        Template t { name, description, {} };
        t.needsInputs = needs;
        for (auto* spec : tracks)
        {
            auto parts = juce::StringArray::fromTokens (spec, "|", {});
            t.trackNames.add (parts[0]);
            t.inputNumbers.add (parts[1].getIntValue());
            t.groups.add (parts[2]);
        }
        return t;
    }

    inline juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (basementProfile()).getChildFile ("Templates");
    }

    inline juce::Array<Template> all()
    {
        juce::Array<Template> t;
        t.add ({ "Blank", "Two tracks on inputs 1 and 2", { "Track 1", "Track 2" } });
        t.add ({ "Singer-songwriter", "Vocal and acoustic guitar", { "Vocal", "Acoustic guitar" } });
        // bands, sized to the interface: drums first on every input, then the rest overdubbed
        t.add (band ("Band, 2 inputs", "Drums in stereo, then each part on input 1", 2,
                     { "Drums L|1|Drums", "Drums R|2|Drums", "Bass|1", "Guitar 1|1|Guitars", "Guitar 2|1|Guitars", "Vocal|1|Vocals", "Backing vocals|1|Vocals" }));
        t.add (band ("Band, 4 inputs", "Kick, snare and overheads at once, then the rest one at a time", 4,
                     { "Kick|1|Drums", "Snare|2|Drums", "Overhead L|3|Drums", "Overhead R|4|Drums", "Bass|1", "Guitar 1|1|Guitars", "Guitar 2|1|Guitars", "Vocal|1|Vocals", "Backing vocals|2|Vocals" }));
        t.add (band ("Band, 8 inputs", "A full drum kit at once, then bass, guitars and vocals", 8,
                     { "Kick|1|Drums", "Snare|2|Drums", "Hi-hat|3|Drums", "Tom|4|Drums", "Floor tom|5|Drums", "Overhead L|6|Drums", "Overhead R|7|Drums", "Room|8|Drums",
                       "Bass|1", "Guitar 1|1|Guitars", "Guitar 2|2|Guitars", "Vocal|1|Vocals", "Backing vocals|2|Vocals" }));
        t.add (band ("Full band, live", "Everyone at once, a track per input, grouped", 10,
                     { "Kick||Drums", "Snare||Drums", "Overhead L||Drums", "Overhead R||Drums", "Bass", "Guitar 1||Guitars", "Guitar 2||Guitars", "Keys", "Vocal||Vocals", "Backing vocals||Vocals" }));
        t.add ({ "Live multitrack", "One track per input on your interface", {}, true });
        t.add ({ "Podcast", "Host and two guests, no click", { "Host", "Guest 1", "Guest 2" } });
        t.add ({ "Mix stems", "Bring in audio files, each on its own track", {}, false, false, true });
        for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.basement"))
        {
            Template u { f.getFileNameWithoutExtension(), "Your template", {} };
            u.file = f;
            if (auto xml = juce::XmlDocument::parse (f))
                for (auto* e : xml->getChildWithTagNameIterator ("TRACK")) // audio tracks
                    u.trackNames.add (e->getStringAttribute ("name", "Track"));
            t.add (u);
        }
        return t;
    }
}

namespace songs
{
    /** Makes the song described by plan (tracks, names, colours, tempo, info) and
        saves it. Inputs are assigned by the caller, since they need a device. */
    inline std::unique_ptr<te::Edit> createFromPlan (te::Engine& engine, const SongPlan& plan)
    {
        auto songFile = plan.songFile();
        plan.folder.createDirectory();
        std::unique_ptr<te::Edit> edit;
        if (plan.templateFile.existsAsFile() && plan.templateFile.copyFileTo (songFile))
            edit = open (engine, songFile);
        if (edit == nullptr)
            edit = create (engine, songFile);

        auto tracks = te::getAudioTracks (*edit);
        for (int i = tracks.size(); i < plan.tracks.size(); ++i)
            edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getAudioTracks (*edit).getLast()), nullptr);
        tracks = te::getAudioTracks (*edit);
        if (! plan.templateFile.existsAsFile())
            for (int i = tracks.size(); --i >= plan.tracks.size();)
                edit->deleteTrack (tracks[i]);
        tracks = te::getAudioTracks (*edit);
        for (int i = 0; i < tracks.size() && i < plan.tracks.size(); ++i)
        {
            tracks[i]->setName (plan.tracks[i].name.isNotEmpty() ? plan.tracks[i].name : "Track " + juce::String (i + 1));
            if (! plan.templateFile.existsAsFile()) tracks[i]->setColour (theme::trackColour (i));
            tracks[i]->state.setProperty ("basementInput", plan.tracks[i].input.isEmpty() ? juce::String ("none") : plan.tracks[i].input, nullptr);
        }

        // group tracks, in the order their names first appear (members picked before any group
        // track is inserted, since that shifts the tracks after it)
        juce::StringArray groupNames;
        for (auto& tp : plan.tracks) if (tp.group.isNotEmpty()) groupNames.addIfNotAlreadyThere (tp.group);
        const auto planned = te::getAudioTracks (*edit);
        for (auto& g : groupNames)
        {
            juce::Array<te::AudioTrack*> members;
            for (int i = 0; i < planned.size() && i < plan.tracks.size(); ++i)
                if (plan.tracks[i].group == g) members.add (planned[i]);
            groups::add (*edit, g, members);
        }

        auto info = plan.info;
        info.created = juce::Time::getCurrentTime();
        info.writeTo (*edit);
        te::EditFileOperations (*edit).save (true, true, false);
        return edit;
    }
}
