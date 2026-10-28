// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Finds your plugins in the background. Every launch checks the plugin folders
// (the standard ones plus any you add in Preferences) and only looks inside
// plugins that are new or changed, so after the first time it takes a moment.
// Each plugin is opened in a separate helper process, so a plugin that
// crashes while being checked can't take Basement down with it; it just gets
// skipped (and listed as skipped in Plugin Manager).
#pragma once

class PluginFinder : private juce::Thread
{
public:
    PluginFinder (te::Engine& e) : Thread ("Basement plugin finder"), engine (e) {}
    ~PluginFinder() override { *alive = false; stopThread (20000); }

    std::function<void (juce::String)> onProgress;    // message thread
    std::function<void (int newOnes)> onFinished;      // message thread

    void start (const juce::StringArray& extraFolders)
    {
        if (isThreadRunning()) return;
        extra = extraFolders;
        startThread (juce::Thread::Priority::low);
    }

    bool isRunning() const { return isThreadRunning(); }
    juce::String status() const { const juce::ScopedLock sl (lock); return current; }

    /** Every folder Basement looks in, for the Preferences page */
    static juce::StringArray defaultFolders (te::Engine& e)
    {
        juce::StringArray out;
        for (auto* f : e.getPluginManager().pluginFormatManager.getFormats())
            if (f->getName() == "VST3" || f->getName() == "VST")
                for (int i = 0; i < f->getDefaultLocationsToSearch().getNumPaths(); ++i)
                    out.addIfNotAlreadyThere (f->getDefaultLocationsToSearch()[i].getFullPathName());
        return out;
    }

private:
    te::Engine& engine;
    juce::StringArray extra;
    juce::CriticalSection lock;
    juce::String current;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    void setStatus (const juce::String& s)
    {
        { const juce::ScopedLock sl (lock); current = s; }
        juce::MessageManager::callAsync ([this, s, a = alive] { if (*a && onProgress) onProgress (s); });
    }

    void run() override
    {
        auto& list = engine.getPluginManager().knownPluginList;
        const int before = list.getNumTypes();
        const auto t0 = juce::Time::getMillisecondCounter();
        for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
        {
            if (! (format->getName() == "VST3" || format->getName() == "VST")) continue;
            auto paths = format->getDefaultLocationsToSearch();
            for (auto& f : extra) if (juce::File (f).isDirectory()) paths.addIfNotAlreadyThere (juce::File (f));
            auto pedal = engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal");
            juce::PluginDirectoryScanner scanner (list, *format, paths, true, pedal, true);
            juce::String name;
            int n = 0;
            const int total = format->searchPathsForPlugins (paths, true, false).size();
            while (! threadShouldExit())
            {
                const auto next = scanner.getNextPluginFileThatWillBeScanned();
                // only plugins that are new or changed are opened; the rest are skipped instantly
                if (! list.isListingUpToDate (next, *format))
                    setStatus ("Finding plugins: " + juce::File::createFileWithoutCheckingPath (next).getFileNameWithoutExtension()
                               + " (" + juce::String (n + 1) + " of " + juce::String (total) + ")");
                if (! scanner.scanNextFile (true, name)) break;
                ++n;
            }
            for (auto& failed : scanner.getFailedFiles()) basementLog ("plugin skipped (didn't load): " + failed);
        }
        const int found = list.getNumTypes() - before;
        basementLog ("plugin search done in " + juce::String ((juce::Time::getMillisecondCounter() - t0) / 1000.0, 1) + " s: "
                     + juce::String (list.getNumTypes()) + " plugins known, " + juce::String (found) + " new");
        { const juce::ScopedLock sl (lock); current = {}; }
        juce::MessageManager::callAsync ([this, found, a = alive] { if (*a && onFinished) onFinished (found); });
    }
};
