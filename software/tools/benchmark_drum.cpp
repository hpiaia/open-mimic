// SPDX-License-Identifier: AGPL-3.0-only
#include "LiteRtModel.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
        {
            throw std::invalid_argument(
                "Usage: mimic-drum-benchmark MODEL FEATURES.txt ITERATIONS");
        }
        std::size_t used = 0;
        const auto iterations = std::stoul(argv[3], &used);
        if (argv[3][used] != '\0' || iterations < 100 || iterations > 100000)
        {
            throw std::invalid_argument("Iterations must be in 100..100000");
        }
        std::ifstream input(argv[2]);
        std::vector<mimic::DrumFeatures> cases;
        mimic::DrumFeatures features{};
        while (input >> features[0])
        {
            for (std::size_t index = 1; index < features.size(); ++index)
            {
                if (!(input >> features[index]))
                {
                    throw std::runtime_error("Incomplete feature row");
                }
            }
            cases.push_back(features);
            if (cases.size() > 4096)
            {
                throw std::runtime_error("At most 4096 benchmark cases are allowed");
            }
        }
        if (!input.eof() || cases.empty())
        {
            throw std::runtime_error("Cannot read benchmark features");
        }
        mimic::LiteRtModel model(argv[1]);
        for (unsigned index = 0; index < 100; ++index)
        {
            if (!model.predict(cases[index % cases.size()]).valid)
            {
                throw std::runtime_error("Benchmark warm-up failed");
            }
        }
        std::vector<double> durations;
        durations.reserve(iterations);
        double checksum = 0, total = 0;
        for (unsigned long index = 0; index < iterations; ++index)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto prediction = model.predict(cases[index % cases.size()]);
            const auto elapsed =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                    .count();
            if (!prediction.valid)
            {
                throw std::runtime_error("Invalid benchmark prediction");
            }
            checksum += prediction.probabilities[0];
            durations.push_back(elapsed);
            total += elapsed;
        }
        std::sort(durations.begin(), durations.end());
        const auto percentile = [&](double fraction)
        { return durations[static_cast<std::size_t>(fraction * (durations.size() - 1))]; };
        std::cout << "{\"iterations\":" << iterations << ",\"cases\":" << cases.size()
                  << ",\"mean_us\":" << total / static_cast<double>(iterations)
                  << ",\"p50_us\":" << percentile(.5) << ",\"p95_us\":" << percentile(.95)
                  << ",\"p99_us\":" << percentile(.99) << ",\"max_us\":" << durations.back()
                  << ",\"checksum\":" << checksum << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "mimic-drum-benchmark: " << error.what() << '\n';
        return 1;
    }
}
