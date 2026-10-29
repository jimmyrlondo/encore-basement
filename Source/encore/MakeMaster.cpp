// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
// Creates the built-in MasterProcessor (JucePlugin_Name comes from CMakeLists.txt).
#include <JuceHeader.h>
#include "../../encore/plugins/Master/Source/PluginProcessor.h"

juce::AudioPluginInstance* makeEncoreMaster() { return new MasterProcessor(); }
