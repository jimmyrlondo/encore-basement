// Encore: Studio
// Copyright (C) 2026 Encore Studio.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version. This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
// (LICENSE) for more details.

#include <JuceHeader.h>

using namespace std::literals;
using namespace tracktion::literals;

#include "Theme.h"
#include "edit/Utilities.h"
#include "EncorePlugins.h"
#include "BridgedPlugins.h"
#include "edit/Meters.h"
#include "edit/Components.h"
#include "edit/Mixer.h"
#include "edit/PianoRoll.h"
#include "edit/DrumGrid.h"
#include "edit/Tuner.h"
#include "edit/PluginWindow.h"
#include "PluginFinder.h"
#include "Mp3.h"
#include "Mastering.h"
#include "LiveImport.h"
#include "MainComponent.h"
#include "SelfTest.h"
#include "PluginTest.h"

class StudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Encore Studio"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& args) override
    {
        // Studio launched as its own plugin-scanning helper: no window, just scan
        if (te::PluginManager::startChildProcessPluginScan (args))
        {
           #if JUCE_WINDOWS
            // a plugin's activation prompt or splash never reaches the screen (see PluginFinder)
            bridgeos::hideWindowsWhileScanning (4000, PluginFinder::windowFlag().getFullPathName().toWideCharPointer());
           #endif
            return;
        }

        if (args.contains ("--devices"))
        {
            setApplicationReturnValue (selftest::devices (juce::File (args.fromFirstOccurrenceOf ("--devices", false, false).trim().unquoted())));
            quit();
            return;
        }
        if (args.contains ("--plugintest"))
        {
            auto rest = juce::StringArray::fromTokens (args.fromFirstOccurrenceOf ("--plugintest", false, false), true);
            rest.removeEmptyStrings();
            setApplicationReturnValue (selftest::plugins (juce::File (rest[0].unquoted()), juce::File (rest[1].unquoted())));
            quit();
            return;
        }
        if (args.contains ("--bridgetest"))
        {
            auto rest = juce::StringArray::fromTokens (args.fromFirstOccurrenceOf ("--bridgetest", false, false), true);
            rest.removeEmptyStrings();
            setApplicationReturnValue (selftest::bridge (juce::File (rest[0].unquoted()), juce::File (rest[1].unquoted())));
            quit();
            return;
        }
        if (args.contains ("--selftest"))
        {
            auto dir = juce::File (args.fromFirstOccurrenceOf ("--selftest", false, false).trim().unquoted());
            setApplicationReturnValue (selftest::run (dir));
            quit();
            return;
        }
        if (args.contains ("--profile"))
            studioProfile() = args.fromFirstOccurrenceOf ("--profile", false, false).trim().unquoted();
        carryOverOldSettings();
        juce::Logger::setCurrentLogger (&logger);
        // a crash leaves its last words in Encore Studio.log (for Help > Report a Problem next time)
        juce::SystemStats::setApplicationCrashHandler ([] (void*) { studioLog (juce::String ("CRASH: ") + juce::NewLine::getDefault() + juce::SystemStats::getStackBacktrace()); });
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        mainWindow = std::make_unique<MainWindow> (*this);
        studioLog ("startup: window up in " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
        openFrom (args);
    }

    void anotherInstanceStarted (const juce::String& args) override { openFrom (args); }

    /** "Encore Studio.exe "C:\...\Song.encore"": open that song once the window is up */
    void openFrom (const juce::String& args)
    {
        if (mainWindow == nullptr) return;
        auto tokens = juce::StringArray::fromTokens (args, true);
        for (auto& t : tokens)
            if (juce::File::isAbsolutePath (t.unquoted()))
                mainWindow->openSong (juce::File (t.unquoted()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::Logger::setCurrentLogger (nullptr);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        // a question box is open (recovery, save as...): close it first, then quit, or quitting hangs
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay (100, [] { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
        else
            quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::JUCEApplication& a)
            : DocumentWindow ("Encore: Studio", theme::base, DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);
            setIcon (encoreIconImage (256)); // title bar and taskbar (the .exe itself carries it too)
            auto* main = new MainComponent();
            content = main;
            main->onTitleChange = [this] (const juce::String& t) { setName (t); };
            main->updateTitle();
            setContentOwned (main, true);
            addKeyListener (main);
            setResizable (true, false);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            darkTitleBar (*this);
        }

        void closeButtonPressed() override { app.systemRequestedQuit(); }

        /** A song double-clicked in Explorer / Finder (or dropped on the app's icon) */
        void openSong (const juce::File& f)
        {
            if (content != nullptr && songfile::isSong (f) && f.existsAsFile())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (content), f] { if (safe) safe->openSongFile (f); });
        }

        juce::Component::SafePointer<MainComponent> content;

    private:
        juce::JUCEApplication& app;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    StudioLookAndFeel lookAndFeel;
    StudioLogger logger;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (StudioApplication)
