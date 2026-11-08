# DCP probes: throw-away VST3 plugins for host tests (docs/dcp/T4_PARAMETER_TITLES.md).
# Not part of the editor build and never shipped.
#
#   cmake -S editors/dcp-probes -B build/dcp-probes -G "Visual Studio 18 2026" -A x64 \
#         -DDCP_PROBE_JUCE_DIR=.tools/juce-9.0.4/src
#   cmake --build build/dcp-probes --config Release
cmake_minimum_required(VERSION 3.22)
project(DcpProbes VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DCP_PROBE_JUCE_DIR "" CACHE PATH "Local JUCE 9.0.4 checkout")
if(NOT DCP_PROBE_JUCE_DIR)
    message(FATAL_ERROR "Set DCP_PROBE_JUCE_DIR to the pinned JUCE 9.0.4 checkout")
endif()
add_subdirectory("${DCP_PROBE_JUCE_DIR}" juce EXCLUDE_FROM_ALL)

# T4: does Studio One follow renamed automation parameters (kParamTitlesChanged)?
# An instrument with MIDI input like the planned DCP plugin, but its own plugin code,
# so it never collides with the "Mod on Daisy" plugin (Dsed/MoDa).
juce_add_plugin(DcpT4Probe
    PRODUCT_NAME "DCP T4 Probe"
    COMPANY_NAME "Daisy-Seed"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Dsed
    PLUGIN_CODE DcT4
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Tools
    BUNDLE_ID org.daisyseed.dcp.t4probe)
target_sources(DcpT4Probe PRIVATE t4/Probe.cpp)
target_compile_definitions(DcpT4Probe PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_options(DcpT4Probe PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(DcpT4Probe
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Pre-check in JUCE's own VST3 host: DcpT4Check "<path>/DCP T4 Probe.vst3"
juce_add_console_app(DcpT4Check PRODUCT_NAME "DcpT4Check")
target_sources(DcpT4Check PRIVATE t4/Check.cpp)
target_compile_definitions(DcpT4Check PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_compile_options(DcpT4Check PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(DcpT4Check
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
