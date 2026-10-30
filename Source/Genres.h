// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Genres: Apple Music's list (the one Encore uses): its main genres and the
// genres under each, from Apple's public genre service (genre 34, Music).
#pragma once

namespace genres
{
    struct Genre { const char* name; std::vector<const char*> more; };

    inline const std::vector<Genre>& apple()
    {
        static const std::vector<Genre> list {
            { "African", { "African Dancehall", "African Reggae", "Afrikaans", "Afro House", "Afro Soul", "Afro-Beat", "Afro-folk", "Afro-fusion", "Afro-Pop", "Afrobeats", "Alte", "Amapiano", "Benga", "Bongo-Flava", "Chimurenga", "Coup\303\251-D\303\251cal\303\251", "Fuji", "Genge", "Gqom", "Highlife", "Kizomba", "Kuduro", "Kwaito", "Kwassa", "Mapouka", "Maskandi", "Mbalax", "Ndombolo", "Shangaan Electro", "Soukous", "Taarab", "Zouglou" } },
            { "Alternative", { "Chinese Alt", "College Rock", "EMO", "Goth Rock", "Grunge", "Indie Egyptian", "Indie Levant", "Indie Maghreb", "Indie Pop", "Indie Rock", "Korean Indie", "New Wave", "Pop Punk", "Punk", "Turkish Alternative" } },
            { "Anime", {  } },
            { "Arabic", { "Arabic Pop", "Dabke", "Islamic", "Khaleeji", "Khaleeji Jalsat", "Khaleeji Shailat", "Levant", "Maghreb Rai", "North African" } },
            { "Blues", { "Acoustic Blues", "Chicago Blues", "Classic Blues", "Contemporary Blues", "Country Blues", "Delta Blues", "Electric Blues" } },
            { "Brazilian", { "Ax\303\251", "Baile Funk", "Bossa Nova", "Choro", "Forr\303\263", "Frevo", "MPB", "Pagode", "Samba", "Sertanejo" } },
            { "Children's Music", { "Lullabies", "Sing-Along", "Stories" } },
            { "Chinese", { "Chinese Classical", "Chinese Flute", "Chinese Opera", "Chinese Orchestral", "Chinese Regional Folk", "Chinese Strings", "Taiwanese Folk", "Tibetan Native Music" } },
            { "Christian", { "CCM", "Christian Metal", "Christian Pop", "Christian Rap", "Christian Rock", "Classic Christian", "Contemporary Gospel", "Gospel", "Praise & Worship", "Southern Gospel", "Traditional Gospel" } },
            { "Classical", { "21st Century", "Air de Cours", "Ars Antiqua", "Ars Nova", "Art Song", "Avant-Garde", "Ballet", "Baroque", "Baroque Era", "Bel Canto", "Brass & Woodwinds", "Cantata", "Carol", "Cello", "Chamber Music", "Chanson", "Chant", "Choral", "Classical Crossover", "Concert Aria", "Concerto", "Concerto Grosso", "Contemporary Era", "Dodecaphony", "Early 20th Century", "Early Music", "Early Romantic", "Eclecticism", "Electroacoustic", "Electronic", "English Ayre", "Experimental Classical", "Expressionism", "Fantasy", "Film Music", "First Viennese School", "French Baroque", "Gregorian Chant", "Guitar", "High Classical", "Impressionism", "Impressionist", "Late 20th Century", "Late Romantic", "Lied", "Mass", "Medieval", "Medieval Era", "Meistergesang", "Minimalism", "Minnesang", "Modern Era", "Modernism", "Music Theatre", "Musique concr\303\250te", "Nationalism", "Neoclassicism", "New Complexity", "New Simplicity", "Nocturne", "Opera", "Oratorio", "Orchestral", "Original Score", "Passion", "Percussion", "Piano", "Polyphony", "Polystylism", "Post-modernism", "PostMinimalism", "Prima Pratica", "Renaissance", "Romantic", "Romantic Era", "Sacred", "Second Viennese School", "Seconda Pratica", "Serialism", "Solo Instrumental", "Sonata", "Spectral Music", "Stage Works", "String Quartet", "Symphony", "Tone Poem", "Troubadour Music", "Trouv\303\250re Music", "Video Game", "Violin", "Vocal Music", "Wedding Music" } },
            { "Comedy", { "Novelty", "Standup Comedy" } },
            { "Country", { "Alternative Country", "Americana", "Bluegrass", "Contemporary Bluegrass", "Contemporary Country", "Country Gospel", "Country Hip-Hop/Rap", "Honky Tonk", "Outlaw Country", "Thai Country", "Traditional Bluegrass", "Traditional Country", "Urban Cowboy" } },
            { "Cuban", { "Bolero", "Chachacha", "Guajira", "Guaracha", "Mambo", "Son", "Timba" } },
            { "Dance", { "Breakbeat", "Exercise", "Garage", "Hardcore", "House", "Jungle/Drum'n'bass", "Maghreb Dance", "Techno", "Trance" } },
            { "Disney", {  } },
            { "Easy Listening", { "Lounge", "Swing" } },
            { "Electronic", { "Ambient", "Bass", "Downtempo", "Dubstep", "Electro-Cha'abi", "Electronica", "IDM/Experimental", "Industrial", "Levant Electronic", "Maghreb Electronic" } },
            { "Enka", {  } },
            { "Fitness & Workout", {  } },
            { "Folk", { "Iraqi Folk", "Khaleeji Folk" } },
            { "French Pop", {  } },
            { "German Folk", {  } },
            { "German Pop", {  } },
            { "Hip-Hop/Rap", { "Alternative Rap", "Chinese Hip-Hop", "Deutschrap", "Dirty South", "East Coast Rap", "Egyptian Hip-Hop", "Gangsta Rap", "Ghanaian Drill", "Hardcore Rap", "Hip-Hop", "Hip-Hop in Russian", "Khaleeji Hip-Hop", "Korean Hip-Hop", "Latin Rap", "Levant Hip-Hop", "Maghreb Hip-Hop", "Old School Rap", "Rap", "South African Hip-Hop", "Turkish Hip-Hop/Rap", "UK Hip-Hop", "Underground Rap", "West Coast Rap" } },
            { "Holiday", { "Chanukah", "Christmas", "Christmas: Children's", "Christmas: Classic", "Christmas: Classical", "Christmas: Country", "Christmas: Jazz", "Christmas: Modern", "Christmas: Pop", "Christmas: R&B", "Christmas: Religious", "Christmas: Rock", "Easter", "Halloween", "Holiday: Other", "Thanksgiving" } },
            { "H\303\266rspiele", {  } },
            { "Indian", { "Assamese", "Bengali", "Bhojpuri", "Bollywood", "Carnatic Classical", "Devotional & Spiritual", "Ghazals", "Gujarati", "Haryanvi", "Hindustani Classical", "Indian Classical", "Indian Folk", "Indian Pop", "Kannada", "Malayalam", "Marathi", "Odia", "Punjabi", "Rajasthani", "Regional Indian", "Sufi", "Tamil", "Telugu", "Urdu" } },
            { "Inspirational", {  } },
            { "Instrumental", {  } },
            { "J-Pop", {  } },
            { "Jazz", { "Avant-Garde Jazz", "Big Band", "Bop", "Contemporary Jazz", "Cool Jazz", "Crossover Jazz", "Dixieland", "Ethio Jazz", "Fusion", "Hard Bop", "Latin Jazz", "Mainstream Jazz", "Ragtime", "Smooth Jazz", "Trad Jazz", "Vocal Jazz" } },
            { "Jewish Music", { "Jewish Holidays", "Klezmer" } },
            { "Karaoke", {  } },
            { "Kayokyoku", {  } },
            { "Korean", { "Korean Classical", "Korean Trad Instrumental", "Korean Trad Song", "Korean Trad Theater" } },
            { "Latin", { "Baladas y Boleros", "Contemporary Latin", "Latin Jazz", "M\303\272sica Mexicana", "M\303\272sica tropical", "Pop Latino", "Ra\303\255ces", "Rock y Alternativo", "Urbano latino" } },
            { "Marching", {  } },
            { "New Age", { "Environmental", "Healing", "Meditation", "Nature", "Relaxation", "Travel", "Yoga" } },
            { "Orchestral", {  } },
            { "Pop", { "Adult Contemporary", "Britpop", "C-Pop", "Cantopop/HK-Pop", "Deutschpop", "Egyptian Pop", "Indo Pop", "Iraqi Pop", "K-Pop", "Khaleeji Pop", "Korean Folk-Pop", "Levant Pop", "Maghreb Pop", "Malaysian Pop", "Mandopop", "Manilla Sound", "Oldies", "Original Pilipino Music", "Pinoy Pop", "Pop in Russian", "Pop/Rock", "Shows", "Soft Rock", "T-Pop", "Tai-Pop", "Teen Pop", "Thai Pop", "Tribute", "Turkish Pop" } },
            { "R&B/Soul", { "Contemporary R&B", "Disco", "Doo Wop", "Funk", "Motown", "Neo-Soul", "Quiet Storm", "Soul" } },
            { "Reggae", { "Dub", "Lovers Rock", "Modern Dancehall", "Roots Reggae", "Ska" } },
            { "Rock", { "Adult Alternative", "American Trad Rock", "Arena Rock", "Blues-Rock", "British Invasion", "Chinese Rock", "Death Metal/Black Metal", "Glam Rock", "Hair Metal", "Hard Rock", "Jam Bands", "Korean Rock", "Metal", "Prog-Rock/Art Rock", "Psychedelic", "Rock & Roll", "Rock in Russian", "Rockabilly", "Roots Rock", "Singer/Songwriter", "Southern Rock", "Surf", "Tex-Mex", "Turkish Rock" } },
            { "Russian", { "Bard in Russian", "Chanson in Russian", "Romance in Russian" } },
            { "Singer/Songwriter", { "Alternative Folk", "Contemporary Folk", "Contemporary Singer/Songwriter", "Folk-Rock", "New Acoustic", "Traditional Folk" } },
            { "Soundtrack", { "Foreign Cinema", "Musicals", "Original Score", "Sound Effects", "TV Soundtrack", "Video Game" } },
            { "Spoken Word", {  } },
            { "Tarab", { "Egyptian Tarab", "Iraqi Tarab", "Khaleeji Tarab" } },
            { "Turkish", { "Arabesque", "Fantezi", "Halk", "Religious", "Sanat", "\303\226zg\303\274n" } },
            { "Vocal", { "Standards", "Traditional Pop", "Trot", "Vocal Pop" } },
            { "Worldwide", { "Asia", "Australia", "Cajun", "Calypso", "Caribbean", "Celtic", "Celtic Folk", "Contemporary Celtic", "Dangdut", "Dini", "Drinking Songs", "Europe", "Fado", "Farsi", "Flamenco", "France", "Hawaii", "Iberia", "Indonesian Religious", "Israeli", "Japan", "Japanese Pop", "Klezmer", "North America", "Polka", "Soca", "South Africa", "South America", "Tango", "Traditional Celtic", "Worldbeat", "Zydeco" } },
        };
        return list;
    }

    inline juce::String text (const char* utf8) { return juce::String (juce::CharPointer_UTF8 (utf8)); }

    /** Every genre, the main ones and the ones under them */
    inline juce::StringArray all()
    {
        juce::StringArray out;
        for (auto& g : apple())
        {
            out.addIfNotAlreadyThere (text (g.name));
            for (auto* m : g.more) out.addIfNotAlreadyThere (text (m));
        }
        return out;
    }

    /** A menu of them: each main genre opens to the ones under it. first: shown at the top (your own) */
    inline juce::PopupMenu menu (std::function<void (juce::String)> picked, const juce::StringArray& first = {})
    {
        juce::PopupMenu m;
        if (! first.isEmpty())
        {
            for (auto& f : first) m.addItem (f, [picked, f] { picked (f); });
            m.addSeparator();
        }
        for (auto& g : apple())
        {
            const auto name = text (g.name);
            if (g.more.empty()) { m.addItem (name, [picked, name] { picked (name); }); continue; }
            juce::PopupMenu sub;
            sub.addItem (name, [picked, name] { picked (name); });
            sub.addSeparator();
            for (auto* x : g.more) { const auto n = text (x); sub.addItem (n, [picked, n] { picked (n); }); }
            m.addSubMenu (name, sub);
        }
        return m;
    }

    /** A genre box: type one, or pick from the list with the button beside it. Several are
        written with commas ("Indie Pop, Alternative"); the first is the main one. */
    class Field : public juce::Component
    {
    public:
        std::function<void()> onChange;
        juce::StringArray suggestions;   // shown first in the list (the artist's own genres)

        explicit Field (bool several) : multiple (several)
        {
            editor.setTextToShowWhenEmpty (several ? "Pick from the list, or type them with commas" : "Pick or type one", theme::muted);
            editor.setFont (juce::FontOptions (14.0f));
            editor.setIndents (10, 0);
            editor.setJustification (juce::Justification::centredLeft);
            editor.onTextChange = [this] { if (onChange) onChange(); };
            pick.setButtonText (several ? "Add..." : "List...");
            pick.setTooltip ("Apple Music's genres (what Encore uses)");
            pick.onClick = [this]
            {
                juce::Component::SafePointer<Field> safe (this);
                menu ([safe] (juce::String g) { if (safe) safe->choose (g); }, suggestions)
                    .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pick));
            };
            addAndMakeVisible (editor);
            addAndMakeVisible (pick);
        }
        void setText (const juce::String& t) { editor.setText (t, false); }
        juce::String getText() const { return tidy (editor.getText()); }
        juce::StringArray list() const { auto a = juce::StringArray::fromTokens (getText(), ",", {}); a.trim(); a.removeEmptyStrings(); return a; }
        juce::TextEditor& getEditor() { return editor; }

        void resized() override
        {
            auto r = getLocalBounds();
            pick.setBounds (r.removeFromRight (72));
            r.removeFromRight (6);
            editor.setBounds (r);
        }

        static juce::String tidy (const juce::String& s)
        {
            auto a = juce::StringArray::fromTokens (s, ",", {});
            a.trim();
            a.removeEmptyStrings();
            a.removeDuplicates (true);
            return a.joinIntoString (", ");
        }

    private:
        const bool multiple;
        juce::TextEditor editor;
        juce::TextButton pick;
        void choose (const juce::String& g)
        {
            if (! multiple) editor.setText (g, false);
            else
            {
                auto a = list();
                a.addIfNotAlreadyThere (g);
                editor.setText (a.joinIntoString (", "), false);
            }
            if (onChange) onChange();
        }
    };
}
