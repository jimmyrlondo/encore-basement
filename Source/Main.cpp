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
#include "edit/Components.h"
#include "edit/PluginWindow.h"
#include "MainComponent.h"
#include "SelfTest.h"

class BasementApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Encore Basement"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& args) override
    {
        if (args.contains ("--devices"))
        {
            setApplicationReturnValue (selftest::devices (juce::File (args.fromFirstOccurrenceOf ("--devices", false, false).trim().unquoted())));
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
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (*this);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::JUCEApplication& a)
            : DocumentWindow ("Encore: Basement", theme::base, DocumentWindow::allButtons), app (a)
        {
            setUsingNativeTitleBar (true);
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
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (BasementApplication)
