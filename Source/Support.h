#pragma once

// Help menu support: checking GitHub for a newer Basement, and a problem
// report (a zip of the log, settings and what Basement saw) to attach to an issue.

namespace support
{
    inline const char* repo() { return "jimmyrlondo/encore-basement"; }

    //==============================================================================
    /** "v0.4.1" or "0.4.1" as numbers, for comparing */
    inline juce::Array<int> versionNumbers (juce::String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        juce::Array<int> n;
        for (auto& part : juce::StringArray::fromTokens (v.upToFirstOccurrenceOf ("-", false, false), ".", {}))
            n.add (part.getIntValue());
        while (n.size() < 3) n.add (0);
        return n;
    }

    inline bool isNewer (const juce::String& candidate, const juce::String& current)
    {
        auto a = versionNumbers (candidate), b = versionNumbers (current);
        for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
            if (a[i] != b[i]) return a[i] > b[i];
        return false;
    }

    struct Release
    {
        juce::String version, page, download, notes;
        bool found = false;
        juce::String problem;   // couldn't check (offline...)
    };

    /** The newest published release on GitHub (prereleases count: that's what Basement ships as for now).
        Runs the request on a background thread; answers on the message thread. */
    inline void latestRelease (std::function<void (Release)> answer)
    {
        juce::Thread::launch ([answer]
        {
            Release r;
            juce::URL url ("https://api.github.com/repos/" + juce::String (repo()) + "/releases?per_page=10");
            int status = 0;
            auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                .withExtraHeaders ("User-Agent: Encore-Basement\r\nAccept: application/vnd.github+json")
                                                .withConnectionTimeoutMs (8000)
                                                .withStatusCode (&status));
            if (in == nullptr || status != 200)
                r.problem = in == nullptr ? "couldn't reach GitHub (offline?)" : "GitHub answered " + juce::String (status);
            else
            {
                const auto json = juce::JSON::parse (in->readEntireStreamAsString());
                const juce::String platformZip =
                   #if JUCE_MAC
                    "macos";
                   #else
                    "windows";
                   #endif
                if (auto* list = json.getArray())
                    for (auto& rel : *list)
                    {
                        if ((bool) rel["draft"]) continue;
                        const auto tag = rel["tag_name"].toString();
                        if (r.found && ! isNewer (tag, r.version)) continue;
                        r.found = true;
                        r.version = tag.trimCharactersAtStart ("vV");
                        r.page = rel["html_url"].toString();
                        r.notes = rel["body"].toString();
                        r.download = {};
                        if (auto* assets = rel["assets"].getArray())
                            for (auto& a : *assets)
                                if (a["name"].toString().containsIgnoreCase (platformZip))
                                    r.download = a["browser_download_url"].toString();
                    }
                if (! r.found) r.problem = "no releases listed";
            }
            juce::MessageManager::callAsync ([answer, r] { answer (r); });
        });
    }

    //==============================================================================
    /** Where Basement keeps its settings and log for this profile */
    inline juce::File settingsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (basementProfile());
    }

    /** Builds "Basement problem report <date>.zip" on the Desktop: what happened (in their words),
        the computer and audio setup, Basement.log, the settings, and optionally the open song's
        arrangement file (no audio). Returns the zip, or an empty File. */
    inline juce::File makeReport (const juce::String& whatHappened, const juce::String& systemInfo, const juce::File& songFile)
    {
        const auto now = juce::Time::getCurrentTime();
        auto zip = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                       .getNonexistentChildFile ("Basement problem report " + now.formatted ("%Y-%m-%d %H%M"), ".zip", false);
        juce::ZipFile::Builder b;
        juce::String readme;
        readme << "Encore: Basement problem report" << juce::newLine
               << "Made " << now.toString (true, true) << juce::newLine << juce::newLine
               << "What happened:" << juce::newLine << (whatHappened.isNotEmpty() ? whatHappened : juce::String ("(not filled in)")) << juce::newLine << juce::newLine
               << systemInfo;
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("basement-report-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        tmp.createDirectory();
        auto report = tmp.getChildFile ("report.txt");
        report.replaceWithText (readme);
        b.addFile (report, 9, "report.txt");
        auto folder = settingsFolder();
        if (folder.getChildFile ("Basement.log").existsAsFile()) b.addFile (folder.getChildFile ("Basement.log"), 9, "Basement.log");
        for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.settings;*.xml"))
            if (f.getSize() < 2000000) b.addFile (f, 9, "settings/" + f.getFileName());
        if (songFile.existsAsFile()) b.addFile (songFile, 9, "song/" + songFile.getFileName());
        bool ok = false;
        {
            juce::FileOutputStream out (zip);
            ok = out.openedOk() && b.writeToStream (out, nullptr);
        }
        tmp.deleteRecursively();
        if (! ok) { zip.deleteFile(); return {}; }
        return zip;
    }

    /** Help > Report a Problem: say what happened, then the zip is made and the issue page opens */
    class ReportComponent : public juce::Component
    {
    public:
        std::function<void (juce::String whatHappened, bool includeSong)> onMake;

        ReportComponent (bool hasSong, bool crashedLastTime)
        {
            hubui::styleLabel (intro, 14.0f, theme::paper);
            intro.setText (juce::String (crashedLastTime ? "Basement didn't close properly last time. Sorry about that.\n" : "")
                             + "Tell us what happened. Basement makes a zip on your Desktop with this, its log and your audio setup "
                               "(no recordings), then opens a new GitHub issue to attach it to.",
                           juce::dontSendNotification);
            intro.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (intro);
            hubui::styleEditor (what);
            what.setMultiLine (true, true);
            what.setReturnKeyStartsNewLine (true);
            what.setTextToShowWhenEmpty ("What were you doing, and what went wrong?", theme::muted);
            addAndMakeVisible (what);
            song.setButtonText ("Include the open song's arrangement (tracks and effects, no audio)");
            song.setToggleState (hasSong, juce::dontSendNotification);
            song.setEnabled (hasSong);
            song.setColour (juce::ToggleButton::textColourId, theme::paper);
            song.setColour (juce::ToggleButton::tickColourId, theme::accent);
            addAndMakeVisible (song);
            make.setColour (juce::TextButton::buttonColourId, theme::brand);
            make.onClick = [this]
            {
                if (onMake) onMake (what.getText().trim(), song.getToggleState());
                close();
            };
            cancel.onClick = [this] { close(); };
            addAndMakeVisible (make);
            addAndMakeVisible (cancel);
            setSize (560, 360);
        }

        void paint (juce::Graphics& g) override { g.fillAll (theme::surface); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (24, 20);
            intro.setBounds (r.removeFromTop (64));
            r.removeFromTop (6);
            auto b = r.removeFromBottom (36);
            make.setBounds (b.removeFromRight (150));
            b.removeFromRight (8);
            cancel.setBounds (b.removeFromRight (90));
            r.removeFromBottom (10);
            song.setBounds (r.removeFromBottom (26));
            r.removeFromBottom (8);
            what.setBounds (r);
        }

    private:
        juce::Label intro;
        juce::TextEditor what;
        juce::ToggleButton song;
        juce::TextButton make { "Make the report" }, cancel { "Cancel" };
        void close() { if (auto* d = findParentComponentOfClass<juce::DialogWindow>()) d->exitModalState (0); }
    };
}
