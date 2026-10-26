// SPDX-License-Identifier: AGPL-3.0-only
// Golden output from isolated ARM emulation of original BuildForZone3 (0x5c22d4).
// ELF SHA-256: b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c.
// Unicorn 2.1.4; synthetic ordered IDs 0..4; ID lookup stub returns its argument.
// These are emulated function results, not hardware or acoustic observations.
#pragma once

#include <array>

namespace
{
struct OriginalHatEntry
{
    int first;
    int second;
    float firstGain;
    float secondGain;
    bool twoVoices;
    std::array<bool, 4> flags;
};

constexpr std::array<OriginalHatEntry, 100> originalHatEntries{{
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {0, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {1, 2, 0x1.0000000000000p+0f, 0x0.0p+0f, true, {true, true, true, false}},
    {1, 2, 0x1.e5b9d00000000p-1f, 0x1.99999a0000000p-4f, true, {true, true, true, false}},
    {1, 2, 0x1.c9f25c0000000p-1f, 0x1.99999a0000000p-3f, true, {true, true, true, false}},
    {1, 2, 0x1.ac5eb40000000p-1f, 0x1.3333340000000p-2f, true, {true, true, true, false}},
    {1, 2, 0x1.8c97f00000000p-1f, 0x1.99999a0000000p-2f, true, {true, true, true, false}},
    {1, 2, 0x1.6a09e60000000p-1f, 0x1.0000000000000p-1f, true, {true, true, true, false}},
    {1, 2, 0x1.43d1360000000p-1f, 0x1.3333340000000p-1f, true, {true, true, true, false}},
    {1, 2, 0x1.186f180000000p-1f, 0x1.6666660000000p-1f, true, {true, true, true, false}},
    {1, 2, 0x1.c9f25c0000000p-2f, 0x1.99999a0000000p-1f, true, {true, true, true, false}},
    {1, 2, 0x1.43d1380000000p-2f, 0x1.cccccc0000000p-1f, true, {true, true, true, false}},
    {1, 2, 0x0.0p+0f, 0x1.0000000000000p+0f, true, {true, true, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {true, false, true, false}},
    {2, 3, 0x1.0000000000000p+0f, 0x0.0p+0f, true, {true, false, true, true}},
    {2, 3, 0x1.eea3960000000p-1f, 0x1.08654a0000000p-2f, true, {true, false, true, true}},
    {2, 3, 0x1.dca5640000000p-1f, 0x1.75e9760000000p-2f, true, {true, false, true, true}},
    {2, 3, 0x1.c9f25c0000000p-1f, 0x1.c9f25c0000000p-2f, true, {true, false, true, true}},
    {2, 3, 0x1.b6735c0000000p-1f, 0x1.08654a0000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.a20bd60000000p-1f, 0x1.279a740000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.8c97f00000000p-1f, 0x1.43d1360000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.75e9740000000p-1f, 0x1.5dc3380000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.5dc3380000000p-1f, 0x1.75e9760000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.43d1360000000p-1f, 0x1.8c97f00000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.279a740000000p-1f, 0x1.a20bd80000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.08654a0000000p-1f, 0x1.b6735c0000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.c9f25c0000000p-2f, 0x1.c9f25c0000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.75e9740000000p-2f, 0x1.dca5640000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x1.08654a0000000p-2f, 0x1.eea3960000000p-1f, true, {true, false, true, true}},
    {2, 3, 0x0.0p+0f, 0x1.0000000000000p+0f, true, {true, false, true, true}},
    {3, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
    {3, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
    {3, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
    {3, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
    {3, 4, 0x1.0000000000000p+0f, 0x0.0p+0f, true, {false, false, false, false}},
    {3, 4, 0x1.c9f25c0000000p-1f, 0x1.c9f25c0000000p-2f, true, {false, false, false, false}},
    {3, 4, 0x1.8c97f00000000p-1f, 0x1.43d1360000000p-1f, true, {false, false, false, false}},
    {3, 4, 0x1.43d1360000000p-1f, 0x1.8c97f00000000p-1f, true, {false, false, false, false}},
    {3, 4, 0x1.c9f25c0000000p-2f, 0x1.c9f25c0000000p-1f, true, {false, false, false, false}},
    {3, 4, 0x0.0p+0f, 0x1.0000000000000p+0f, true, {false, false, false, false}},
    {4, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
    {4, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
    {4, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
    {4, -1, 0x1.0000000000000p+0f, 0x1.0000000000000p+0f, false, {false, false, false, false}},
}};
} // namespace
