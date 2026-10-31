// Encore: Basement Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Who you're making music as, and where each artist's music lives.
//
// For now the account is a profile on this computer (account.json beside the
// settings): your name, and either one artist or a record label with its
// artists. When Encore sign-in arrives (docs/ENCORE-API.md) the same profile
// is filled in from your Encore account instead, and the folders stay as they are.
//
// Every artist gets one folder in the music library:
//
//   <Library>/<Artist>/artist.json        who this is (name, genre)
//   <Library>/<Artist>/Songs/<Song>/      each song's session (the .encore file, recordings...)
//   <Library>/<Artist>/Releases/<Name>/   singles, EPs and albums: what goes to Encore
//   <Library>/<Artist>/Masters/<Name>/    every finished master, kept even when it's redone
//   <Library>/<Artist>/Live/<Show>/       live shows brought in from Encore Live
#pragma once

namespace account
{
    struct Artist
    {
        juce::String name;
        juce::String genre;      // their genres from Apple Music's list, the main one first ("Indie Pop, Alternative")
        juce::String hometown, members, website;
        juce::String copyright;  // who owns the recordings, for release notes ("Sick City", or a label)
        juce::String pro, ipi;   // the songwriter's performing rights organisation (ASCAP, BMI...) and IPI number

        /** The genre new songs get */
        juce::String mainGenre() const { return genre.upToFirstOccurrenceOf (",", false, false).trim(); }

        void writeTo (juce::DynamicObject& o) const
        {
            o.setProperty ("name", name);
            o.setProperty ("genre", genre);
            for (auto [key, value] : { std::pair<const char*, const juce::String*> { "hometown", &hometown }, { "members", &members }, { "website", &website },
                                       { "copyright", &copyright }, { "pro", &pro }, { "ipi", &ipi } })
                if (value->isNotEmpty()) o.setProperty (key, *value);
        }
        static Artist from (const juce::var& v)
        {
            Artist a;
            a.name = v["name"].toString().trim();
            a.genre = v["genre"].toString();
            a.hometown = v["hometown"].toString();
            a.members = v["members"].toString();
            a.website = v["website"].toString();
            a.copyright = v["copyright"].toString();
            a.pro = v["pro"].toString();
            a.ipi = v["ipi"].toString();
            return a;
        }
    };

    struct Profile
    {
        juce::String name, email;
        juce::String label;                 // the record label's name; empty for an artist or band
        juce::Array<Artist> artists;        // one for an artist, any number for a label
        int current = 0;                    // the artist new songs are made for
        juce::File library;                 // where every artist's folder goes
        juce::String signedInWith { "this computer" };   // "encore" once Encore sign-in exists
        bool skipped = false;               // chose "Skip for now" on the first screen

        bool isLabel() const   { return label.isNotEmpty(); }
        bool signedIn() const  { return ! artists.isEmpty(); }
        const Artist* artist() const { return juce::isPositiveAndBelow (current, artists.size()) ? &artists.getReference (current) : nullptr; }
        juce::String who() const { return isLabel() ? label : artist() != nullptr ? artist()->name : name; }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("kind", "encore-studio-account");
            o->setProperty ("version", 1);
            o->setProperty ("name", name);
            o->setProperty ("email", email);
            o->setProperty ("label", label);
            juce::Array<juce::var> list;
            for (auto& a : artists)
            {
                auto* x = new juce::DynamicObject();
                a.writeTo (*x);
                list.add (juce::var (x));
            }
            o->setProperty ("artists", list);
            o->setProperty ("current", current);
            o->setProperty ("library", library.getFullPathName());
            o->setProperty ("signedInWith", signedInWith);
            o->setProperty ("skipped", skipped);
            return juce::var (o);
        }

        static Profile fromVar (const juce::var& v)
        {
            Profile p;
            p.name = v["name"].toString();
            p.email = v["email"].toString();
            p.label = v["label"].toString();
            if (auto* list = v["artists"].getArray())
                for (auto& a : *list)
                    if (a["name"].toString().trim().isNotEmpty()) p.artists.add (Artist::from (a));
            p.current = juce::jlimit (0, juce::jmax (0, p.artists.size() - 1), (int) v["current"]);
            if (v["library"].toString().isNotEmpty()) p.library = juce::File (v["library"].toString());
            if (v["signedInWith"].toString().isNotEmpty()) p.signedInWith = v["signedInWith"].toString();
            p.skipped = (bool) v["skipped"];
            return p;
        }
    };

    /** The music library a new profile suggests: Music/Encore Studio */
    inline juce::File defaultLibrary() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Encore Studio"); }

    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (studioProfile()).getChildFile ("account.json");
    }

    /** True until the first screen has been answered (set up a profile, or skip) */
    inline bool neverAsked() { return ! file().existsAsFile(); }

    inline Profile& get()
    {
        static Profile p = []
        {
            auto v = juce::JSON::parse (file());
            auto prof = Profile::fromVar (v);
            if (prof.library == juce::File()) prof.library = defaultLibrary();
            return prof;
        }();
        return p;
    }

    inline void save()
    {
        file().getParentDirectory().createDirectory();
        file().replaceWithText (juce::JSON::toString (get().toVar()));
    }

    /** Sign out: forget the profile on this computer (every folder and song stays where it is) */
    inline void signOut()
    {
        auto lib = get().library;
        get() = {};
        get().library = lib;
        get().skipped = true;
        save();
    }
}

namespace library
{
    inline juce::String folderName (const juce::String& s)
    {
        auto n = juce::File::createLegalFileName (s.trim());
        return n.isNotEmpty() ? n : juce::String ("Untitled");
    }

    inline juce::File artistFolder (const juce::File& lib, const account::Artist& a) { return lib.getChildFile (folderName (a.name)); }

    /** Makes (or tops up) an artist's folders, with a note saying what goes where */
    inline juce::File ensure (const juce::File& lib, const account::Artist& a)
    {
        auto dir = artistFolder (lib, a);
        for (auto sub : { "Songs", "Releases", "Masters", "Live" }) dir.getChildFile (sub).createDirectory();
        auto* o = new juce::DynamicObject();
        o->setProperty ("kind", "encore-artist");
        a.writeTo (*o);
        dir.getChildFile ("artist.json").replaceWithText (juce::JSON::toString (juce::var (o)));
        auto readme = dir.getChildFile ("What goes where.txt");
        if (! readme.existsAsFile())
            readme.replaceWithText (a.name + "'s music, kept by Encore: Basement Studio\r\n\r\n"
                                    "Songs     Every song's session: the song file, its recordings and imported audio.\r\n"
                                    "          Back up or move a song by copying its folder.\r\n"
                                    "Releases  Singles, EPs and albums, as they go to Encore.\r\n"
                                    "Masters   Every finished master. When a song is mastered again, the earlier\r\n"
                                    "          one moves to \"Earlier masters\" so nothing is ever lost.\r\n"
                                    "Live      Live shows brought in from Encore Live or a multitrack recorder.\r\n");
        return dir;
    }

    /** The current artist's Songs folder, or nothing when no one is signed in */
    inline juce::File songsFolder()
    {
        auto& p = account::get();
        if (auto* a = p.artist()) return ensure (p.library, *a).getChildFile ("Songs");
        return {};
    }

    /** Where new songs go: the signed-in artist's Songs folder, else the folder chosen in
        Preferences, else Music/Encore Studio (or the Music/Encore Basement of earlier versions) */
    inline juce::File songsFolderOr (const juce::String& chosen)
    {
        if (auto s = songsFolder(); s != juce::File()) return s;
        if (chosen.isNotEmpty()) return juce::File (chosen);
        auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (music.getChildFile ("Encore Basement").isDirectory() && ! music.getChildFile ("Encore Studio").exists()) return music.getChildFile ("Encore Basement");
        return music.getChildFile ("Encore Studio");
    }

    inline juce::File liveFolder()
    {
        auto& p = account::get();
        if (auto* a = p.artist()) return ensure (p.library, *a).getChildFile ("Live");
        return {};
    }

    /** The artist folder a song belongs to (<Artist>/Songs/<Song>/<Song>.encore), or nothing */
    inline juce::File artistFolderOf (const juce::File& songFile)
    {
        auto songs = songFile.getParentDirectory().getParentDirectory();
        auto artist = songs.getParentDirectory();
        if (songs.getFileName() == "Songs" && artist.getChildFile ("artist.json").existsAsFile()) return artist;
        return {};
    }

    /** A release's name for a song: its album or EP, else the song itself (a single) */
    inline juce::String releaseName (const juce::String& album, const juce::String& title) { return folderName (album.trim().isNotEmpty() ? album : title); }

    inline juce::File releaseFolder (const juce::File& artistDir, const juce::String& album, const juce::String& title)
    {
        return artistDir.getChildFile ("Releases").getChildFile (releaseName (album, title));
    }

    /** Copies a finished master into <Artist>/Masters/<Release>/<Title>.<ext>. A master already
        there moves into "Earlier masters" with its date first, so no master is ever lost. */
    inline juce::File keepMaster (const juce::File& artistDir, const juce::File& master, const juce::String& album, const juce::String& title)
    {
        auto dir = artistDir.getChildFile ("Masters").getChildFile (releaseName (album, title));
        dir.createDirectory();
        auto dest = dir.getChildFile (folderName (title) + master.getFileExtension());
        if (dest.existsAsFile())
        {
            auto earlier = dir.getChildFile ("Earlier masters");
            earlier.createDirectory();
            auto stamp = dest.getLastModificationTime().formatted ("%Y-%m-%d %H%M");
            dest.moveFileTo (earlier.getNonexistentChildFile (dest.getFileNameWithoutExtension() + " " + stamp, dest.getFileExtension(), false));
        }
        return master.copyFileTo (dest) ? dest : juce::File();
    }
}
