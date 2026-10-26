// SPDX-License-Identifier: AGPL-3.0-only
#include "Adc.h"
#include "AudioEngine.h"
#include "Capture.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        const std::string command = argc > 1 ? argv[1] : "";
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
        if (command == "audio-self-test" && argc == 2)
        {
            return mimic::audioSelfTest();
        }
#endif
        std::cerr << "Usage: mimicd probe | mimicd capture OUTPUT.omadc SECONDS | mimicd "
                     "audio-self-test\n";
        return 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "mimicd: " << e.what() << '\n';
        return 1;
    }
}
