cmake_minimum_required(VERSION 3.22)

project(OpenMimic VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
include(CTest)

option(MIMIC_BUILD_UI "Build the JUCE graphical interface" ON)
option(MIMIC_USE_JUCE "Build the JUCE audio foundation" ON)

if(MIMIC_USE_JUCE OR MIMIC_BUILD_UI)
    if(NOT JUCE_PATH)
        message(FATAL_ERROR "Set JUCE_PATH to the pinned JUCE 9.0.4 sources")
    endif()

    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

    add_library(mimic-juce-config INTERFACE)
    target_compile_definitions(mimic-juce-config INTERFACE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_XRANDR=0
        JUCE_USE_XINERAMA=0
        JUCE_USE_XCURSOR=0
        JUCE_USE_XRENDER=1
        JUCE_USE_XINPUT=1
    )
endif()

add_library(mimic-hardware STATIC
    common/Adc.cpp
    common/Capture.cpp
    common/ExclusiveOutputFile.cpp
)
target_include_directories(mimic-hardware PUBLIC common)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mimic-hardware PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MIMIC_USE_JUCE)
    juce_add_console_app(mimicd PRODUCT_NAME "mimicd")
else()
    add_executable(mimicd)
endif()

target_sources(mimicd PRIVATE mimicd/src/main.cpp)
target_link_libraries(mimicd PRIVATE mimic-hardware Threads::Threads)

if(MIMIC_USE_JUCE)
    target_sources(mimicd PRIVATE
        mimicd/src/AudioEngine.cpp
        mimicd/src/AuxControl.cpp
        mimicd/src/PearlAudioDevice.cpp
        mimicd/src/PearlAudioBoard.cpp
        mimicd/src/AudioTest.cpp
    )
    target_link_libraries(mimicd PRIVATE
        mimic-juce-config
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    target_compile_definitions(mimicd PRIVATE MIMIC_USE_JUCE=1 JUCE_ALSA=0 JUCE_JACK=0)
endif()

install(TARGETS mimicd RUNTIME DESTINATION bin)

if(MIMIC_BUILD_UI)
    juce_add_gui_app(mimic
        PRODUCT_NAME "mimic"
        COMPANY_NAME "Open Mimic"
        BUNDLE_ID "org.openmimic.ui"
    )
    target_sources(mimic PRIVATE
        mimic/src/main.cpp
        mimic/src/AudioDebugPanel.cpp
        mimic/src/AuxInputPanel.cpp
        mimic/src/TouchChoice.cpp
        mimic/src/WheelInput.cpp
        mimic/src/MainComponent.cpp
        mimic/src/NetworkMonitor.cpp
        mimic/src/SystemMonitor.cpp
    )
    target_include_directories(mimic PRIVATE common)
    target_link_libraries(mimic PRIVATE
        mimic-juce-config
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Threads::Threads
    )
    install(TARGETS mimic RUNTIME DESTINATION bin)
endif()

if(BUILD_TESTING)
    add_executable(aux-control-test tests/aux_control_test.cpp)
    target_include_directories(aux-control-test PRIVATE common)
    add_test(NAME aux-control COMMAND aux-control-test)

    add_executable(capture-test tests/capture_test.cpp)
    target_link_libraries(capture-test PRIVATE mimic-hardware)
    add_test(NAME capture-format COMMAND capture-test)

    if(MIMIC_USE_JUCE)
        add_test(NAME audio-dsp COMMAND mimicd audio-self-test)
    endif()
endif()
