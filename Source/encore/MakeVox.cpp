// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
// Creates the built-in VocalSuiteProcessor (JucePlugin_Name comes from CMakeLists.txt).
#include <JuceHeader.h>
#include "../../encore/plugins/Vox/Source/PluginProcessor.h"

juce::AudioPluginInstance* makeEncoreVox() { return new VocalSuiteProcessor(); }
