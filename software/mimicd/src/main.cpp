// SPDX-License-Identifier: AGPL-3.0-only
#include "Adc.h"
#include "Capture.h"
#include "MidiService.h"
#include "PadService.h"

#ifdef MIMIC_USE_JUCE
#include "AudioEngine.h"
#include "AudioTest.h"
#include "AuxControl.h"
#endif

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "pad-service")
        {
            if (argc == 2)
            {
                return mimic::runPadService();
            }
            if (argc == 4 && std::string(argv[2]) == "--data-directory")
            {
                return mimic::runPadService(argv[3]);
            }
            throw std::invalid_argument("Usage: pad-service [--data-directory PATH]");
        }
        if (command == "pad-command")
        {
            return mimic::runPadCommand(argc, argv);
        }
        if (command == "midi-service")
        {
            if (argc == 2)
            {
                return mimic::runMidiService();
            }
            if (argc == 4 && std::string(argv[2]) == "--din-device")
            {
                return mimic::runMidiService(argv[3]);
            }
            throw std::invalid_argument("Usage: midi-service [--din-device /dev/ttyS9]");
        }
        if (command == "midi-config" || command == "midi-test" || command == "midi-panic")
        {
            return mimic::runMidiCommand(argc, argv);
        }
        if (command == "probe" && argc == 2)
        {
            for (int index = 0; index < 2; ++index)
            {
                mimic::Adc adc("/dev/ads7953." + std::to_string(index));
                const auto& layout = adc.layout();
                std::cout << "ADC" << index << ": map=" << layout.mapBytes
                          << " DMA=" << layout.dmaBytes << " portion=" << layout.portionBytes
                          << '\n';
            }
            return 0;
        }
        if (command == "capture" && argc == 4)
        {
            std::size_t consumed = 0;
            const auto seconds = std::stoul(argv[3], &consumed);
            if (consumed != std::string(argv[3]).size() || seconds < 1 || seconds > 60)
            {
                throw std::invalid_argument("SECONDS must be 1..60");
            }
            mimic::capture(argv[2], static_cast<unsigned>(seconds));
            return 0;
        }
#ifdef MIMIC_USE_JUCE
        if (command == "aux-control" && argc == 2)
        {
            return mimic::runAuxControl();
        }
        if (command == "audio-probe" && argc == 2)
        {
            return mimic::audioProbe();
        }
        if (command == "audio-stop" && argc == 2)
        {
            return mimic::stopAudioTest();
        }
        if (command == "audio-test")
        {
            mimic::AudioTestOptions options;
            for (int index = 2; index < argc; index += 2)
            {
                if (index + 1 >= argc)
                {
                    throw std::invalid_argument("Missing audio-test option value");
                }
                const std::string option = argv[index];
                const std::string value = argv[index + 1];
                std::size_t consumed = 0;
                if (option == "--channels")
                {
                    options.channelMask = 0;
                    std::istringstream channels(value);
                    std::string channel;
                    while (std::getline(channels, channel, ','))
                    {
                        const auto number = std::stoi(channel, &consumed);
                        if (consumed != channel.size() || number < 0 || number > 15)
                        {
                            throw std::invalid_argument("Channels must be 0..15");
                        }
                        options.channelMask |= static_cast<std::uint16_t>(1U << number);
                    }
                }
                else
                {
                    const auto number = std::stod(value, &consumed);
                    if (consumed != value.size())
                    {
                        throw std::invalid_argument("Invalid numeric option");
                    }
                    if (option == "--frequency")
                    {
                        options.frequencyHz = number;
                    }
                    else if (option == "--seconds")
                    {
                        options.durationSeconds = number;
                    }
                    else if (option == "--amplitude")
                    {
                        options.amplitude = static_cast<float>(number);
                    }
                    else
                    {
                        throw std::invalid_argument("Unknown audio-test option: " + option);
                    }
                }
            }
            return mimic::runAudioTest(options);
        }
        if (command == "audio-self-test" && argc == 2)
        {
            mimic::audioSelfTest();
            return mimic::audioToneSelfTest();
        }
#endif
        std::cerr
            << "Usage: mimicd probe | mimicd capture OUTPUT.omadc SECONDS | mimicd "
               "pad-service [--data-directory PATH] | pad-command ACTION [ARGS] | "
               "aux-control | audio-self-test | audio-probe | audio-test [--channels 1,3] "
               "[--frequency 440] "
               "[--seconds 3] [--amplitude 1.0] | audio-stop | midi-service [--din-device PATH] | "
               "midi-config [--input din|none|usb-CxDy] [--output ID] [--channel 0..16] | "
               "midi-test [--note 36] [--velocity 96] [--channel 10] [--ms 250] | midi-panic\n";
        return 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "mimicd: " << e.what() << '\n';
        return 1;
    }
}
