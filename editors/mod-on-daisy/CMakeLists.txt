# Mod on Daisy Editor, JUCE standalone (GUI.md § 3). VST3 follows later (D6).
#
#   cmake -S editors/mod-on-daisy -B build/editor -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/editor --config Release
#
# JUCE 9.0.4 is fetched by CMake; -DMOD_EDITOR_JUCE_DIR=<path> uses a local checkout.
cmake_minimum_required(VERSION 3.22)
project(ModOnDaisyEditor VERSION 0.1.1 LANGUAGES C CXX)

# Static MSVC runtime: the .exe runs without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MOD_EDITOR_JUCE_DIR "" CACHE PATH "Local JUCE checkout (default: fetch JUCE 9.0.4)")
set(MOD_MODULE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/mod-on-daisy/include")
# Shared bootloader request (common/bootloader), header-only like the module.
set(BOOTLOADER_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../common/bootloader")

if(MOD_EDITOR_JUCE_DIR)
    add_subdirectory("${MOD_EDITOR_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4 # 3fb5cbc0340758596dac39f7c8de47d562b4896e
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# Core tests without JUCE (also run by `make -C editors/mod-on-daisy test`).
add_executable(mod_editor_core_tests tests/test_core.cpp)
target_include_directories(mod_editor_core_tests PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}")
# The simulator sends column base values like the Physical Modeling MOD build.
target_compile_definitions(mod_editor_core_tests PRIVATE MOD_ON_DAISY_EXTENSIONS=1)
target_compile_options(mod_editor_core_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4>)
enable_testing()
add_test(NAME editor_core COMMAND mod_editor_core_tests)

juce_add_gui_app(ModOnDaisyEditor
    PRODUCT_NAME "Mod on Daisy Editor"
    COMPANY_NAME "Daisy-Seed"
    VERSION ${PROJECT_VERSION}
    BUNDLE_ID org.daisyseed.modondaisy.editor)

target_sources(ModOnDaisyEditor PRIVATE
    src/app/Main.cpp
    src/ui/EditorComponent.cpp)
target_include_directories(ModOnDaisyEditor PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}")
target_compile_definitions(ModOnDaisyEditor PRIVATE
    MOD_EDITOR_VERSION="${PROJECT_VERSION}"
    MOD_ON_DAISY_EXTENSIONS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ModOnDaisyEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ModOnDaisyEditor,JUCE_VERSION>")
# Source files are UTF-8 (umlauts in the German interface).
target_compile_options(ModOnDaisyEditor PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(ModOnDaisyEditor
    PRIVATE
        juce::juce_gui_basics
        juce::juce_audio_basics
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# VST3 plugin for Studio One (GUI.md § 3): the same editor as an audio effect that passes
# audio through unchanged, holds the Daisy port itself and keeps the patch in the project.
juce_add_plugin(ModOnDaisyPlugin
    PRODUCT_NAME "Mod on Daisy"
    COMPANY_NAME "Daisy-Seed"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Dsed
    PLUGIN_CODE MoDa
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools
    BUNDLE_ID org.daisyseed.modondaisy.plugin)
target_sources(ModOnDaisyPlugin PRIVATE
    src/plugin/Plugin.cpp
    src/ui/EditorComponent.cpp)
target_include_directories(ModOnDaisyPlugin PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}")
target_compile_definitions(ModOnDaisyPlugin PUBLIC
    MOD_ON_DAISY_EXTENSIONS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_options(ModOnDaisyPlugin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(ModOnDaisyPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Host-side check of the built .vst3 (no Steinberg validator needed): load, pass-through,
# state round trip, editor window. Run with MOD_ON_DAISY_NO_AUTOCONNECT=1.
juce_add_console_app(ModOnDaisyPluginCheck PRODUCT_NAME "Mod on Daisy Plugin Check")
target_sources(ModOnDaisyPluginCheck PRIVATE tests/plugin_check.cpp)
target_compile_definitions(ModOnDaisyPluginCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(ModOnDaisyPluginCheck
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
