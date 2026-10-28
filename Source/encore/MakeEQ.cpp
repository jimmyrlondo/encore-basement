// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
// Creates the built-in EqProcessor (JucePlugin_Name comes from CMakeLists.txt).
#include <JuceHeader.h>
#include "../../encore/plugins/EQ/Source/PluginProcessor.h"

juce::AudioPluginInstance* makeEncoreEQ() { return new EqProcessor(); }
