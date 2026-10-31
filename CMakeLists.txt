# Encore: Basement Studio, an open-source DAW (AGPLv3).
# Built on JUCE (AGPLv3) and Tracktion Engine (GPLv3), both git submodules.
#
#   git clone https://github.com/jimmyrlondo/encore-studio.git
#   git submodule update --init --depth 1 external/JUCE external/tracktion_engine
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.22)
project(EncoreStudio VERSION 0.4.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(external/JUCE)

# The 32-bit plugin bridge (Windows): a small 32-bit program Studio runs 32-bit
# plugins in. It's built on its own, in a 32-bit build folder:
#   cmake -B build32 -A Win32 -DSTUDIO_BRIDGE_ONLY=ON
#   cmake --build build32 --config Release
# and goes beside "Encore Studio.exe" (the installer puts it there).
option(STUDIO_BRIDGE_ONLY "Build only the 32-bit plugin bridge" OFF)
if (STUDIO_BRIDGE_ONLY)
    juce_add_gui_app(EncoreStudioBridge32
        PRODUCT_NAME "Encore Studio Bridge32"
        COMPANY_NAME "Encore"
        VERSION ${PROJECT_VERSION}
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/encore-icon.png"
        NEEDS_CURL FALSE
        NEEDS_WEB_BROWSER FALSE)
    juce_generate_juce_header(EncoreStudioBridge32)
    target_sources(EncoreStudioBridge32 PRIVATE Source/bridge/BridgeMain.cpp Source/bridge/Shm.cpp)
    target_include_directories(EncoreStudioBridge32 PRIVATE Source/bridge external/fst)
    target_compile_definitions(EncoreStudioBridge32 PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(EncoreStudioBridge32 PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_recommended_config_flags)
    return()
endif()

add_subdirectory(external/tracktion_engine/modules ${CMAKE_BINARY_DIR}/tracktion)

juce_add_gui_app(EncoreStudio
    PRODUCT_NAME "Encore Studio"
    COMPANY_NAME "Encore"
    BUNDLE_ID com.encoremusic.studio
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Encore Basement Studio records from your audio inputs."
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/encore-icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/encore-icon.png"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

# the Encore icon, drawn in the app (SVG, so it's sharp at any size)
juce_add_binary_data(StudioAssets SOURCES Assets/encore-icon.svg)

juce_generate_juce_header(EncoreStudio)

target_sources(EncoreStudio PRIVATE Source/Main.cpp $<$<PLATFORM_ID:Windows>:Source/bridge/Shm.cpp>)
target_include_directories(EncoreStudio PRIVATE Source)

# Encore's own plugins (Vox, Verb, Equalize, Master), built in: Encore Studio hosts
# their processors directly. The sources in encore/ come from the plugin repo
# (tools/sync-encore-plugins.ps1); each plugin's files get its product name.
foreach (entry "Vox|Encore Vox" "Verb|Encore Verb" "EQ|Encore Equalize" "Master|Encore Master")
    string (REPLACE "|" ";" parts "${entry}")
    list (GET parts 0 dir)
    list (GET parts 1 product)
    file (GLOB plugin_sources CONFIGURE_DEPENDS encore/plugins/${dir}/Source/*.cpp)
    list (APPEND plugin_sources Source/encore/Make${dir}.cpp)
    target_sources(EncoreStudio PRIVATE ${plugin_sources})
    set_source_files_properties(${plugin_sources} PROPERTIES COMPILE_DEFINITIONS "JucePlugin_Name=\"${product}\"")
endforeach()
target_include_directories(EncoreStudio PRIVATE encore/shared)

# older VST2 plugins: FST, an open (GPLv3+) stand-in for the VST2 headers
target_include_directories(EncoreStudio PRIVATE external/fst)

# MP3 export: LAME's encoder library (LGPL), built from source
file (GLOB lame_sources CONFIGURE_DEPENDS external/lame/libmp3lame/*.c)
add_library(lame STATIC ${lame_sources})
target_include_directories(lame PRIVATE external/lame external/lame/libmp3lame PUBLIC external/lame/include)
target_compile_definitions(lame PRIVATE HAVE_CONFIG_H)
if (MSVC)
    target_compile_options(lame PRIVATE /W0)
else()
    target_compile_options(lame PRIVATE -w)
endif()

target_compile_definitions(EncoreStudio PRIVATE
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
    BASEMENT_BUILTIN=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(EncoreStudio PRIVATE
    StudioAssets
    lame
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
    target_compile_options(EncoreStudio PRIVATE /bigobj /MP)
endif()
