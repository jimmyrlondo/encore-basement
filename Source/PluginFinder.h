// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Finds your plugins in the background. Every launch checks the plugin folders
// (the standard ones plus any you add in Preferences) and only looks inside
// plugins that are new or changed, so after the first time it takes a moment.
// Each plugin is opened in a separate helper process, so a plugin that
// crashes while being checked can't take Studio down with it; it just gets
// skipped (and listed as skipped in Plugin Manager). One that stops to show a
// window (an activation prompt, say) is set aside within seconds, and one
// that never answers after a minute, so the rest still get found.
#pragma once

#if JUCE_WINDOWS
 #include <process.h>
 inline int studioPid() { return _getpid(); }

 // three user32 calls, declared here rather than pulling all of <windows.h> into Studio
 namespace studiowin
 {
     extern "C" __declspec(dllimport) int __stdcall EnumWindows (int (__stdcall*) (void*, std::intptr_t), std::intptr_t);
     extern "C" __declspec(dllimport) int __stdcall IsWindowVisible (void*);
     extern "C" __declspec(dllimport) unsigned long __stdcall GetWindowThreadProcessId (void*, unsigned long*);

     /** Whether a process has a window showing (a plugin's activation prompt, say). Owned windows
         count too, which .NET's "main window" misses */
     inline bool hasVisibleWindow (unsigned long pid)
     {
         struct Search { unsigned long pid; bool found; } search { pid, false };
         EnumWindows ([] (void* h, std::intptr_t param) -> int
         {
             auto& s = *reinterpret_cast<Search*> (param);
             unsigned long owner = 0;
             GetWindowThreadProcessId (h, &owner);
             if (owner == s.pid && IsWindowVisible (h)) { s.found = true; return 0; }
             return 1;
         }, reinterpret_cast<std::intptr_t> (&search));
         return search.found;
     }
 }
#else
 #include <unistd.h>
 inline int studioPid() { return (int) getpid(); }
#endif

class PluginFinder : private juce::Thread
{
public:
    PluginFinder (te::Engine& e) : Thread ("Encore Studio plugin finder"), engine (e) {}
    ~PluginFinder() override
    {
        *alive = false;
        stopWatchdog();
        stopThread (20000);
    }

    std::function<void (juce::String)> onProgress;    // message thread
    std::function<void (int newOnes)> onFinished;      // message thread

    void start (const juce::StringArray& extraFolders)
    {
        if (isThreadRunning()) return;
        extra = extraFolders;
        pedal = engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal");
        stopWatchdog();
        startThread (juce::Thread::Priority::low);
        watching = true;
        watchdog = std::thread ([this] { watch(); });
    }

    bool isRunning() const { return isThreadRunning(); }
    juce::String status() const { const juce::ScopedLock sl (lock); return current; }

    /** Every folder Studio looks in, for the Preferences page */
    static juce::StringArray defaultFolders (te::Engine& e)
    {
        juce::StringArray out;
        for (auto* f : e.getPluginManager().pluginFormatManager.getFormats())
            if (f->getName() == "VST3" || f->getName() == "VST")
            {
                auto paths = f->getDefaultLocationsToSearch();
                if (f->getName() == "VST") addCommonVst2Folders (paths);
                for (int i = 0; i < paths.getNumPaths(); ++i)
                    out.addIfNotAlreadyThere (paths[i].getFullPathName());
            }
        return out;
    }

    /** Where newer installers put VST2 plugins on Windows, besides the folders JUCE knows */
    static void addCommonVst2Folders (juce::FileSearchPath& paths)
    {
       #if JUCE_WINDOWS
        auto common = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files");
        for (auto f : { common.getChildFile ("VST2"), common.getChildFile ("Steinberg").getChildFile ("VST2") })
            if (f.isDirectory()) paths.addIfNotAlreadyThere (f);
       #else
        juce::ignoreUnused (paths);
       #endif
    }

private:
    te::Engine& engine;
    juce::StringArray extra;
    juce::CriticalSection lock;
    juce::String current;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    // the plugin being looked at now, and since when (0 = none)
    static constexpr juce::uint32 hangLimitMs = 60000, windowCheckAfterMs = 5000;
    juce::File pedal;   // JUCE writes the plugin it's about to open here (last line), in case it crashes
    std::atomic<juce::uint32> fileStarted { 0 };
    std::atomic<bool> setAside { false };
    std::thread watchdog;
    std::atomic<bool> watching { false };

    void stopWatchdog()
    {
        watching = false;
        if (watchdog.joinable()) watchdog.join();
    }

    /** Its own thread, so it still works when a plugin's window has frozen Studio's: a plugin that
        stops the search to show a window (an activation prompt, say) is set aside after a few seconds,
        and one that just never answers after a minute */
    void watch()
    {
        juce::uint32 lastWindowCheck = 0;
        while (watching && isThreadRunning())
        {
            juce::Thread::sleep (250);
            const auto started = fileStarted.load();
            if (started == 0 || setAside) continue;
            const auto now = juce::Time::getMillisecondCounter();
            juce::String why;
            if (now - started >= hangLimitMs)
                why = "no answer in " + juce::String (hangLimitMs / 1000) + " s";
            else if (now - started >= windowCheckAfterMs && now - lastWindowCheck >= 4000)
            {
                lastWindowCheck = now;
                for (auto& h : helperProcesses())
                    if (h.hasWindow) why = "it opened a window, like an activation prompt";
            }
            if (why.isEmpty() || fileStarted.load() != started) continue;

            juce::StringArray lines;
            pedal.readLines (lines);
            lines.removeEmptyStrings();
            if (lines.isEmpty()) continue;
            const auto file = lines[lines.size() - 1];
            studioLog ("plugin skipped (" + why + "): " + file);
            engine.getPluginManager().knownPluginList.addToBlacklist (file);
            setAside = true;
            // the search is waiting on the stuck helper, so it's the only one running right now
            const auto stuck = helperProcesses();
            if (auto abort = engine.getPluginManager().abortCurrentPluginScan) abort();
            // stopping a search only disconnects from the helper; one held up by a plugin would stay open,
            // and so would anything the plugin started (a licensing tool's window, say)
            for (auto& h : stuck)
               #if JUCE_WINDOWS
                run ({ "taskkill", "/F", "/T", "/PID", h.pid }, false);
               #else
               {
                   run ({ "pkill", "-9", "-P", h.pid }, false);
                   run ({ "kill", "-9", h.pid }, false);
               }
               #endif
        }
    }

    static juce::String run (const juce::StringArray& cmd, bool wantOutput)
    {
        juce::ChildProcess p;
        if (! p.start (cmd, wantOutput ? juce::ChildProcess::wantStdOut : 0)) return {};
        auto out = wantOutput ? p.readAllProcessOutput() : juce::String();
        p.waitForProcessToFinish (15000);
        return out;
    }

    struct Helper { juce::String pid; bool hasWindow = false; };

    /** Studio's own plugin-checking helpers (its child processes), and whether each has a window up,
        itself or through something a plugin started (copy protection like PACE shows its activation
        window from a process of its own) */
    static std::vector<Helper> helperProcesses()
    {
        const auto pid = juce::String (studioPid());
       #if JUCE_WINDOWS
        auto out = run ({ "powershell", "-NoProfile", "-NonInteractive", "-Command",
                          "$all = Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name; "
                          "$all | Where-Object { $_.ParentProcessId -eq " + pid + " -and $_.Name -like '*Encore Studio*' } | ForEach-Object { "
                          "$tree = @($_.ProcessId); $i = 0; "
                          "while ($i -lt $tree.Count) { $p = $tree[$i]; $all | Where-Object { $_.ParentProcessId -eq $p } | ForEach-Object { $tree += $_.ProcessId }; $i++ }; "
                          "$tree -join ' ' }" }, true);
       #else
        auto out = run ({ "pgrep", "-P", pid }, true);
       #endif
        std::vector<Helper> helpers;
        for (auto& l : juce::StringArray::fromLines (out))
        {
            // Windows: "helper descendant descendant...", Mac: "helper"
            auto ids = juce::StringArray::fromTokens (l.trim(), " ", {});
            ids.removeEmptyStrings();
            if (ids.isEmpty() || ! ids[0].containsOnly ("0123456789")) continue;
            const auto id = ids[0];
           #if JUCE_WINDOWS
            bool window = false;
            for (auto& p : ids) window = window || studiowin::hasVisibleWindow ((unsigned long) p.getLargeIntValue());
            helpers.push_back ({ id, window });
           #else
            helpers.push_back ({ id, false });   // no window check on Mac yet: the minute limit still applies
           #endif
        }
        return helpers;
    }

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
            if (format->getName() == "VST") addCommonVst2Folders (paths);
            for (auto& f : extra) if (juce::File (f).isDirectory()) paths.addIfNotAlreadyThere (juce::File (f));
            const int total = format->searchPathsForPlugins (paths, true, false).size();
            // a plugin set aside for hanging stops the search (and its helper process): start again without it
            for (bool again = true; again && ! threadShouldExit();)
            {
                again = false;
                juce::PluginDirectoryScanner scanner (list, *format, paths, true, pedal, true);
                juce::String name;
                int n = 0;
                while (! threadShouldExit())
                {
                    setStatus ("Finding plugins: " + scanner.getNextPluginFileThatWillBeScanned()
                               + " (" + juce::String (n + 1) + " of " + juce::String (total) + ")");
                    fileStarted = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());
                    const bool more = scanner.scanNextFile (true, name);
                    fileStarted = 0;
                    if (setAside.exchange (false)) { again = true; break; }
                    if (! more) break;
                    ++n;
                }
                for (auto& failed : scanner.getFailedFiles())
                {
                    // VST2 folders are full of helper .dlls that aren't plugins: note them so later searches skip them
                    // (Plugin Manager can clear the list)
                    if (format->getName() == "VST") list.addToBlacklist (failed);
                    studioLog ("plugin skipped (didn't load" + juce::String (format->getName() == "VST" ? ", won't check again" : "") + "): " + failed);
                }
            }
        }
        const int found = list.getNumTypes() - before;
        studioLog ("plugin search done in " + juce::String ((juce::Time::getMillisecondCounter() - t0) / 1000.0, 1) + " s: "
                     + juce::String (list.getNumTypes()) + " plugins known, " + juce::String (found) + " new");
        { const juce::ScopedLock sl (lock); current = {}; }
        juce::MessageManager::callAsync ([this, found, a = alive] { if (*a && onFinished) onFinished (found); });
    }
};
