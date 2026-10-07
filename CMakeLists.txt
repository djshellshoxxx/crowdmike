cmake_minimum_required(VERSION 3.22)
project(CrowdMike VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.3)
FetchContent_MakeAvailable(JUCE)

enable_testing()
add_executable(crowdmike_tests tests/DspSmokeTests.cpp)
target_include_directories(crowdmike_tests PRIVATE Source)
target_link_libraries(crowdmike_tests PRIVATE juce::juce_audio_basics juce::juce_dsp)
add_test(NAME crowdmike_tests COMMAND crowdmike_tests)
