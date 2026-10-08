cmake_minimum_required(VERSION 3.22)
project(CrowdMike VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(clap_juce_extensions)

juce_add_plugin(CrowdMike
    COMPANY_NAME "Circuit Drift Labs"
    BUNDLE_ID "com.circuitdriftlabs.crowdmike"
    PLUGIN_MANUFACTURER_CODE CrDL
    PLUGIN_CODE CrM1
    FORMATS Standalone VST3
    PRODUCT_NAME "CrowdMike"
)

target_sources(CrowdMike PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DSP/InputStrip.cpp
    Source/DSP/InputStrip.h
    Source/DSP/SafetyLimiter.cpp
    Source/DSP/SafetyLimiter.h
)

target_include_directories(CrowdMike PRIVATE Source)
# The project ships no VST2 build, so prevent JUCE from treating VST3 as a VST2 replacement.
target_compile_definitions(CrowdMike PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(CrowdMike PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
juce_generate_juce_header(CrowdMike)

clap_juce_extensions_plugin(
    TARGET CrowdMike
    CLAP_ID "com.circuitdriftlabs.crowdmike"
    CLAP_FEATURES audio-effect
)

enable_testing()
add_executable(crowdmike_tests
    tests/DspSmokeTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/InputStrip.cpp
    Source/DSP/SafetyLimiter.cpp
)
target_include_directories(crowdmike_tests PRIVATE Source)
target_compile_definitions(crowdmike_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(crowdmike_tests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)
add_test(NAME crowdmike_tests COMMAND crowdmike_tests)
