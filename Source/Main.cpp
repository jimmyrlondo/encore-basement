// Encore: Basement
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

class BasementApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Encore Basement"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& args) override
    {
        // Basement launched as its own plugin-scanning helper: no window, just scan
        if (te::PluginManager::startChildProcessPluginScan (args))
            return;

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
        if (args.contains ("--selftest"))
        {
            auto dir = juce::File (args.fromFirstOccurrenceOf ("--selftest", false, false).trim().unquoted());
            setApplicationReturnValue (selftest::run (dir));
            quit();
            return;
        }
        if (args.contains ("--profile"))
            basementProfile() = args.fromFirstOccurrenceOf ("--profile", false, false).trim().unquoted();
        juce::Logger::setCurrentLogger (&logger);
        // a crash leaves its last words in Basement.log (for Help > Report a Problem next time)
        juce::SystemStats::setApplicationCrashHandler ([] (void*) { basementLog (juce::String ("CRASH: ") + juce::NewLine::getDefault() + juce::SystemStats::getStackBacktrace()); });
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        mainWindow = std::make_unique<MainWindow> (*this);
        basementLog ("startup: window up in " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
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
            : DocumentWindow ("Encore: Basement", theme::base, DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);
            setIcon (encoreIconImage (256)); // title bar and taskbar (the .exe itself carries it too)
            auto* main = new MainComponent();
            main->onTitleChange = [this] (const juce::String& t) { setName (t); };
            main->updateTitle();
            setContentOwned (main, true);
            addKeyListener (main);
            setResizable (true, false);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { app.systemRequestedQuit(); }

    private:
        juce::JUCEApplication& app;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    BasementLookAndFeel lookAndFeel;
    BasementLogger logger;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (BasementApplication)
