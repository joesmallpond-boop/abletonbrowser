cmake_minimum_required(VERSION 3.22)
project(BashBrowser VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

option(SB_BUILD_TESTS "Build the analysis test tool" ON)
option(SB_BUILD_PLUGIN "Build the plugin" ON)

set(SB_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SB_FORMATS AU)
endif()

set(SB_CORE_SOURCES
    Source/Analysis/Analyzer.cpp
    Source/Analysis/NameParser.cpp
    Source/Analysis/AbletonProject.cpp
    Source/Analysis/MusicTheory.cpp
    Source/Library/SampleInfo.cpp
    Source/Library/Library.cpp
    Source/Library/FilterModel.cpp
    Source/Audio/SampleLoader.cpp)

if(SB_BUILD_PLUGIN)
juce_add_plugin(BashBrowser
    COMPANY_NAME "Kalabash"
    BUNDLE_ID "com.kalabash.bashbrowser"
    PLUGIN_MANUFACTURER_CODE Klbs
    PLUGIN_CODE Bshb
    PRODUCT_NAME "Bash Browser"
    FORMATS ${SB_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(BashBrowser PRIVATE
    ${SB_CORE_SOURCES}
    Source/Audio/PreviewPlayer.cpp
    Source/Audio/SamplerEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/FilterPanel.cpp
    Source/UI/ResultsTable.cpp
    Source/UI/WaveformView.cpp
    Source/UI/TagEditor.cpp
    Source/UI/SamplerPanel.cpp
    Source/UI/FoldersPanel.cpp)

target_include_directories(BashBrowser PRIVATE Source third_party/include)

target_compile_definitions(BashBrowser PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BashBrowser
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

if(SB_BUILD_TESTS)
    juce_add_console_app(AnalyzerTests PRODUCT_NAME "AnalyzerTests")
    target_sources(AnalyzerTests PRIVATE tests/AnalyzerTests.cpp ${SB_CORE_SOURCES})
    target_include_directories(AnalyzerTests PRIVATE Source third_party/include)
    target_compile_definitions(AnalyzerTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
    target_link_libraries(AnalyzerTests PRIVATE
        juce::juce_audio_formats juce::juce_dsp juce::juce_events juce::juce_recommended_config_flags)
endif()

if(SB_BUILD_TESTS)
    juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
    target_sources(EngineTests PRIVATE tests/EngineTests.cpp Source/Audio/SamplerEngine.cpp Source/Audio/PreviewPlayer.cpp Source/Audio/SampleLoader.cpp)
    target_include_directories(EngineTests PRIVATE Source third_party/include)
    target_compile_definitions(EngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(EngineTests PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
