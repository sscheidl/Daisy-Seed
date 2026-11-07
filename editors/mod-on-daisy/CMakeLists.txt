# Daisy Control Panel (JUCE standalone: matrix page + SD card page) and the Mod on Daisy VST3 (GUI.md § 3,
# docs/dcp/SD_PAGE_WORK_ORDER.md).
#
#   cmake -S editors/mod-on-daisy -B build/editor -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/editor --config Release
#
# JUCE 9.0.4 is fetched by CMake; -DMOD_EDITOR_JUCE_DIR=<path> uses a local checkout.
cmake_minimum_required(VERSION 3.22)
project(ModOnDaisyEditor VERSION 0.5.0 LANGUAGES C CXX)

# Static MSVC runtime: the .exe runs without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MOD_EDITOR_JUCE_DIR "" CACHE PATH "Local JUCE checkout (default: fetch JUCE 9.0.4)")
set(MOD_MODULE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/mod-on-daisy/include")
# Shared bootloader request (common/bootloader), header-only like the module.
set(BOOTLOADER_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../common/bootloader")
# Acid Seq: the firmware's SQ codec, device and parameter table (shared with the panel).
set(SQ_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../sequencers/step-seq/include"
    # Streichwurst: parameter register, tables and Mod on Daisy columns (simulator, patch).
    "${CMAKE_CURRENT_SOURCE_DIR}/../../instruments/streichwurst/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../instruments/streichwurst/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../effects/streichwurst-fx/include")

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
target_include_directories(mod_editor_core_tests PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}" "${SQ_INCLUDE}")
# The simulator sends column base values like the Physical Modeling MOD build.
target_compile_definitions(mod_editor_core_tests PRIVATE MOD_ON_DAISY_EXTENSIONS=1)
target_compile_options(mod_editor_core_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4>)
# SD share core (Nebulae/Looper card sharing) against the simulated instruments; reads
# tests/sd_share_vectors.txt from the source directory.
add_executable(mod_editor_sd_share_tests tests/test_sd_share.cpp)
target_include_directories(mod_editor_sd_share_tests PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}")
target_compile_options(mod_editor_sd_share_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4>)
enable_testing()
add_test(NAME editor_core COMMAND mod_editor_core_tests)
# Threaded serial transport with a fake OS port (phase 3 of the SD page work order).
add_executable(mod_editor_serial_tests tests/test_serial_transport.cpp)
target_include_directories(mod_editor_serial_tests PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}")
target_compile_options(mod_editor_serial_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4>)
find_package(Threads REQUIRED)
target_link_libraries(mod_editor_serial_tests PRIVATE Threads::Threads)
add_test(NAME editor_sd_share COMMAND mod_editor_sd_share_tests WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME editor_serial COMMAND mod_editor_serial_tests)

# The standalone app: "Daisy Control Panel" (D3 of the SD page work order; the CMake
# target keeps its name). The VST3 below keeps name, codes and bundle name unchanged.
juce_add_gui_app(ModOnDaisyEditor
    PRODUCT_NAME "Daisy Control Panel"
    COMPANY_NAME "Daisy-Seed"
    VERSION ${PROJECT_VERSION}
    BUNDLE_ID org.daisyseed.controlpanel)

target_sources(ModOnDaisyEditor PRIVATE
    src/app/Main.cpp
    src/ui/EditorComponent.cpp)
target_include_directories(ModOnDaisyEditor PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}" "${SQ_INCLUDE}")
target_compile_definitions(ModOnDaisyEditor PRIVATE
    MOD_EDITOR_VERSION="${PROJECT_VERSION}"
    MOD_EDITOR_SD_PAGE=1
    MOD_ON_DAISY_EXTENSIONS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ModOnDaisyEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ModOnDaisyEditor,JUCE_VERSION>")
# Source files are UTF-8 (typographic quotes and dashes in the interface texts).
target_compile_options(ModOnDaisyEditor PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(ModOnDaisyEditor
    PRIVATE
        juce::juce_gui_basics
        juce::juce_audio_basics
        juce::juce_audio_devices
        # SD card page: COM port enumeration (SetupAPI) for the kit's USB CDC port.
        $<$<PLATFORM_ID:Windows>:setupapi>
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
target_include_directories(ModOnDaisyPlugin PRIVATE "${MOD_MODULE_INCLUDE}" "${BOOTLOADER_INCLUDE}" "${SQ_INCLUDE}")
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
