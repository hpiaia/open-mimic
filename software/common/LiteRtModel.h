// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "DrumModel.h"

#include <memory>
#include <string>

namespace mimic
{
// CPU-only LiteRT CompiledModel. Construction and buffer allocation happen
// before acquisition; execution belongs to the trigger worker, not audio I/O.
class LiteRtModel final
{
public:
    explicit LiteRtModel(const std::string& path);
    ~LiteRtModel();
    LiteRtModel(const LiteRtModel&) = delete;
    LiteRtModel& operator=(const LiteRtModel&) = delete;
    [[nodiscard]] std::size_t classCount() const noexcept;
    DrumPrediction predict(const DrumFeatures& features) noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation;
};
} // namespace mimic
