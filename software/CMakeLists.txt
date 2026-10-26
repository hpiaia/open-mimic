cmake_minimum_required(VERSION 3.22)

project(OpenMimic VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
include(CTest)

option(MIMIC_BUILD_UI "Build the JUCE graphical interface" ON)
option(MIMIC_USE_JUCE "Build the JUCE audio foundation" ON)
option(MIMIC_USE_LITERT "Use the production LiteRT CPU runtime" OFF)

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
    common/PadCapture.cpp
    common/ExclusiveOutputFile.cpp
)
target_include_directories(mimic-hardware PUBLIC common)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mimic-hardware PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(mimic-samples STATIC
    common/SampleDecoder.cpp
    common/InstrumentSelection.cpp
    common/HatControl.cpp
)
target_include_directories(mimic-samples PUBLIC common)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mimic-samples PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mimic-sample-decode tools/decode_sample.cpp)
target_link_libraries(mimic-sample-decode PRIVATE mimic-samples)
install(TARGETS mimic-sample-decode RUNTIME DESTINATION bin)

add_library(mimic-midi STATIC common/MidiStream.cpp)
target_include_directories(mimic-midi PUBLIC common)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mimic-midi PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(mimic-drums STATIC common/DrumModel.cpp)
target_include_directories(mimic-drums PUBLIC common)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mimic-drums PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_library(mimic-calibration STATIC common/PadCalibration.cpp)
target_include_directories(mimic-calibration PUBLIC common)
add_library(mimic-triggers STATIC common/TriggerEngine.cpp)
target_include_directories(mimic-triggers PUBLIC common)
target_link_libraries(mimic-triggers PUBLIC mimic-drums mimic-calibration)
if(MIMIC_USE_LITERT)
    find_path(LITERT_INCLUDE_DIR litert/c/litert_compiled_model.h REQUIRED)
    find_library(LITERT_LIBRARY NAMES LiteRt REQUIRED)
    add_library(mimic-ml STATIC common/LiteRtModel.cpp)
    target_include_directories(mimic-ml PRIVATE "${LITERT_INCLUDE_DIR}")
    target_link_libraries(mimic-ml PUBLIC mimic-drums PRIVATE "${LITERT_LIBRARY}")
endif()
add_executable(mimic-drum-classify tools/classify_drum.cpp)
target_link_libraries(mimic-drum-classify PRIVATE mimic-drums)
if(MIMIC_USE_LITERT)
    target_link_libraries(mimic-drum-classify PRIVATE mimic-ml)
    target_compile_definitions(mimic-drum-classify PRIVATE MIMIC_USE_LITERT=1)
endif()
install(TARGETS mimic-drum-classify RUNTIME DESTINATION bin)
add_executable(mimic-trigger-replay tools/replay_triggers.cpp)
target_link_libraries(mimic-trigger-replay PRIVATE mimic-triggers)
if(MIMIC_USE_LITERT)
    target_link_libraries(mimic-trigger-replay PRIVATE mimic-ml)
    target_compile_definitions(mimic-trigger-replay PRIVATE MIMIC_USE_LITERT=1)
    add_executable(mimic-drum-benchmark tools/benchmark_drum.cpp)
    target_link_libraries(mimic-drum-benchmark PRIVATE mimic-ml)
    install(TARGETS mimic-drum-benchmark RUNTIME DESTINATION bin)
endif()
install(TARGETS mimic-trigger-replay RUNTIME DESTINATION bin)
install(DIRECTORY models/shared-drum-pilot DESTINATION share/open-mimic/models)

if(MIMIC_USE_JUCE)
    juce_add_console_app(mimicd PRODUCT_NAME "mimicd")
else()
    add_executable(mimicd)
endif()

target_sources(mimicd PRIVATE
    mimicd/src/main.cpp
    mimicd/src/MidiService.cpp
    mimicd/src/PadService.cpp
)
target_link_libraries(mimicd PRIVATE mimic-hardware mimic-midi mimic-triggers Threads::Threads)

if(MIMIC_USE_LITERT)
    target_link_libraries(mimicd PRIVATE mimic-ml)
    target_compile_definitions(mimicd PRIVATE MIMIC_USE_LITERT=1)
endif()

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
        mimic/src/MidiPanel.cpp
        mimic/src/PadCalibrationPanel.cpp
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
    add_executable(pad-calibration-test tests/pad_calibration_test.cpp)
    target_link_libraries(pad-calibration-test PRIVATE mimic-calibration)
    add_test(NAME pad-calibration COMMAND pad-calibration-test)
    add_executable(trigger-engine-test tests/trigger_engine_test.cpp)
    target_link_libraries(trigger-engine-test PRIVATE mimic-triggers)
    add_test(NAME trigger-engine COMMAND trigger-engine-test)
    add_executable(drum-model-test tests/drum_model_test.cpp)
    target_link_libraries(drum-model-test PRIVATE mimic-drums)
    add_test(NAME drum-model COMMAND drum-model-test)

    add_executable(pad-capture-test tests/pad_capture_test.cpp)
    target_link_libraries(pad-capture-test PRIVATE mimic-hardware)
    add_test(NAME pad-capture COMMAND pad-capture-test)

    add_executable(instrument-selection-trace tests/instrument_selection_trace.cpp)
    target_link_libraries(instrument-selection-trace PRIVATE mimic-samples)

    add_executable(midi-stream-test tests/midi_stream_test.cpp)
    target_link_libraries(midi-stream-test PRIVATE mimic-midi)
    add_test(NAME midi-stream COMMAND midi-stream-test)

    add_executable(hat-control-test tests/hat_control_test.cpp)
    target_link_libraries(hat-control-test PRIVATE mimic-samples)
    add_test(NAME hat-control COMMAND hat-control-test)

    add_executable(sample-decoder-test tests/sample_decoder_test.cpp)
    target_link_libraries(sample-decoder-test PRIVATE mimic-samples)
    add_test(NAME sample-decoder COMMAND sample-decoder-test)

    add_executable(instrument-selection-test tests/instrument_selection_test.cpp)
    target_link_libraries(instrument-selection-test PRIVATE mimic-samples)
    add_test(NAME instrument-selection COMMAND instrument-selection-test)

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
