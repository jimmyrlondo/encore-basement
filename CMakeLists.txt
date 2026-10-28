# Encore: Basement, an open-source DAW (AGPLv3).
# Built on JUCE (AGPLv3) and Tracktion Engine (GPLv3), both git submodules.
#
#   git clone https://github.com/jimmyrlondo/encore-basement.git
#   git submodule update --init --depth 1 external/JUCE external/tracktion_engine
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.22)
project(EncoreBasement VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(external/JUCE)
add_subdirectory(external/tracktion_engine/modules ${CMAKE_BINARY_DIR}/tracktion)

juce_add_gui_app(Basement
    PRODUCT_NAME "Encore Basement"
    COMPANY_NAME "Encore"
    BUNDLE_ID com.encoremusic.basement
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Basement records from your audio inputs."
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/encore-icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/encore-icon.png"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

# the Encore icon, drawn in the app (SVG, so it's sharp at any size)
juce_add_binary_data(BasementAssets SOURCES Assets/encore-icon.svg)

juce_generate_juce_header(Basement)

target_sources(Basement PRIVATE Source/Main.cpp)
target_include_directories(Basement PRIVATE Source)

# Encore's own plugins (Vox, Verb, Equalize, Master), built in: Basement hosts
# their processors directly. The sources in encore/ come from the plugin repo
# (tools/sync-encore-plugins.ps1); each plugin's files get its product name.
foreach (entry "Vox|Encore Vox" "Verb|Encore Verb" "EQ|Encore Equalize" "Master|Encore Master")
    string (REPLACE "|" ";" parts "${entry}")
    list (GET parts 0 dir)
    list (GET parts 1 product)
    file (GLOB plugin_sources CONFIGURE_DEPENDS encore/plugins/${dir}/Source/*.cpp)
    list (APPEND plugin_sources Source/encore/Make${dir}.cpp)
    target_sources(Basement PRIVATE ${plugin_sources})
    set_source_files_properties(${plugin_sources} PROPERTIES COMPILE_DEFINITIONS "JucePlugin_Name=\"${product}\"")
endforeach()
target_include_directories(Basement PRIVATE encore/shared)

# older VST2 plugins: FST, an open (GPLv3+) stand-in for the VST2 headers
target_include_directories(Basement PRIVATE external/fst)

target_compile_definitions(Basement PRIVATE
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
    BASEMENT_BUILTIN=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(Basement PRIVATE
    BasementAssets
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

if (MSVC)
    target_compile_options(Basement PRIVATE /bigobj /MP)
endif()
