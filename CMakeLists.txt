cmake_minimum_required(VERSION 3.22)
project(CrowdMike VERSION 0.2.0 LANGUAGES C CXX)

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

# ---------------------------------------------------------------------------------------------
# Source groups. DSP/ is JUCE-light (juce_core/audio_basics/dsp only) so it builds in fast unit
# tests. Everything else (engine, control, recording, UI, plug-in wrapper) is the "app" layer.
# ---------------------------------------------------------------------------------------------
file(GLOB_RECURSE CM_DSP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/DSP/*.cpp)
file(GLOB_RECURSE CM_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/*.cpp)
list(REMOVE_ITEM CM_APP_SOURCES ${CM_DSP_SOURCES})
list(FILTER CM_APP_SOURCES EXCLUDE REGEX "Source/Standalone/")
file(GLOB_RECURSE CM_STANDALONE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/Standalone/*.cpp)

juce_add_plugin(CrowdMike
    COMPANY_NAME "Circuit Drift Labs"
    BUNDLE_ID "com.circuitdriftlabs.crowdmike"
    PLUGIN_MANUFACTURER_CODE CrDL
    PLUGIN_CODE CrM1
    FORMATS Standalone VST3
    PRODUCT_NAME "CrowdMike"
    VST3_CATEGORIES Fx Spatial
)

target_sources(CrowdMike PRIVATE ${CM_DSP_SOURCES} ${CM_APP_SOURCES})
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
    juce::juce_osc
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
juce_generate_juce_header(CrowdMike)

# The standalone app owns its audio-device mapping, autosave and OSC, so it uses a custom JUCEApplication.
if(TARGET CrowdMike_Standalone)
    target_compile_definitions(CrowdMike_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    target_sources(CrowdMike_Standalone PRIVATE ${CM_STANDALONE_SOURCES})
endif()

clap_juce_extensions_plugin(
    TARGET CrowdMike
    CLAP_ID "com.circuitdriftlabs.crowdmike"
    CLAP_FEATURES audio-effect
)

# ---------------------------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------------------------
enable_testing()

# Light DSP-only tests (juce_core / audio_basics / dsp).
file(GLOB_RECURSE CM_DSP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/dsp/*.cpp)
add_executable(crowdmike_dsp_tests tests/TestMain.cpp ${CM_DSP_TEST_SOURCES} ${CM_DSP_SOURCES})
target_include_directories(crowdmike_dsp_tests PRIVATE Source tests)
target_compile_definitions(crowdmike_dsp_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(crowdmike_dsp_tests PRIVATE juce::juce_dsp)
add_test(NAME crowdmike_dsp_tests COMMAND crowdmike_dsp_tests)

# Full engine / processor / UI tests.
file(GLOB_RECURSE CM_ENGINE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/engine/*.cpp)
add_executable(crowdmike_tests
    tests/TestMain.cpp
    tests/DspSmokeTests.cpp
    ${CM_ENGINE_TEST_SOURCES}
    ${CM_DSP_SOURCES}
    ${CM_APP_SOURCES}
)
target_include_directories(crowdmike_tests PRIVATE Source tests)
target_compile_definitions(crowdmike_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(crowdmike_tests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_osc
)
add_test(NAME crowdmike_tests COMMAND crowdmike_tests)

# Recording / playback tests (juce_audio_formats + threads, no GUI).
file(GLOB_RECURSE CM_REC_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/Recording/*.cpp)
file(GLOB_RECURSE CM_REC_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/recording/*.cpp)
add_executable(crowdmike_recording_tests tests/TestMain.cpp ${CM_REC_TEST_SOURCES} ${CM_REC_SOURCES} ${CM_DSP_SOURCES})
target_include_directories(crowdmike_recording_tests PRIVATE Source tests)
target_compile_definitions(crowdmike_recording_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(crowdmike_recording_tests PRIVATE juce::juce_dsp juce::juce_audio_formats juce::juce_events)
add_test(NAME crowdmike_recording_tests COMMAND crowdmike_recording_tests)
