cmake_minimum_required(VERSION 3.25)

# These must be set before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures (universal by default)")
# Static MSVC runtime so the Windows plugin doesn't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(reLoad VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RELOAD_BUILD_TESTS "Build the unit/render test executable" ON)

# ---------------------------------------------------------------------------
# Dependencies (pinned by version + SHA-256). Archives are cached in
# .deps-cache/ so wiping a build directory doesn't trigger a re-download.
# ---------------------------------------------------------------------------
include(FetchContent)
set(RELOAD_DEPS_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/.deps-cache")

FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.15.tar.gz
    URL_HASH SHA256=0eafd736286c6e7bb6958d79fce18d42cb654b8e231890d327b7cece379bd156
    DOWNLOAD_DIR "${RELOAD_DEPS_CACHE}"
    DOWNLOAD_NAME JUCE-8.0.15.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(RELOAD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND RELOAD_FORMATS AU)
endif()

juce_add_plugin(reLoad
    PRODUCT_NAME "reLoad"
    COMPANY_NAME "reLoad Audio"
    BUNDLE_ID "com.reloadaudio.reload"
    PLUGIN_MANUFACTURER_CODE Rlda      # AU: first letter upper-case, rest lower-case
    PLUGIN_CODE Rlod                   # AU: exactly one upper-case letter
    FORMATS ${RELOAD_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE      # scripts/build_install_mac.sh owns installation
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    DESCRIPTION "Wavetable synth that rebuilds recorded sounds as wavetables")

target_sources(reLoad PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/plugin/Parameters.cpp
    src/dsp/SynthEngine.cpp)

target_include_directories(reLoad PUBLIC src)

target_compile_definitions(reLoad PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(reLoad
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(RELOAD_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.0.tar.gz
        URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
        DOWNLOAD_DIR "${RELOAD_DEPS_CACHE}"
        DOWNLOAD_NAME Catch2-v3.16.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(reload_tests
        tests/EngineRenderTests.cpp
        tests/ProcessorTests.cpp)

    # Link the plugin's shared-code library and inherit its JUCE settings, so
    # tests exercise exactly the code that ships in the plugin.
    target_link_libraries(reload_tests PRIVATE reLoad Catch2::Catch2WithMain)
    target_include_directories(reload_tests PRIVATE
        tests
        $<TARGET_PROPERTY:reLoad,INCLUDE_DIRECTORIES>)
    target_compile_definitions(reload_tests PRIVATE
        $<TARGET_PROPERTY:reLoad,COMPILE_DEFINITIONS>)

    # Tests only need to run on the build machine; skip the second slice.
    if(APPLE)
        set_target_properties(reload_tests PROPERTIES OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()

    enable_testing()
    include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
    catch_discover_tests(reload_tests DISCOVERY_MODE PRE_TEST)
endif()
