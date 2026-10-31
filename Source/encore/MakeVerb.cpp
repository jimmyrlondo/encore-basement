// Encore: Basement Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
// Creates the built-in VerbProcessor (JucePlugin_Name comes from CMakeLists.txt).
#include <JuceHeader.h>
#include "../../encore/plugins/Verb/Source/PluginProcessor.h"

juce::AudioPluginInstance* makeEncoreVerb() { return new VerbProcessor(); }
