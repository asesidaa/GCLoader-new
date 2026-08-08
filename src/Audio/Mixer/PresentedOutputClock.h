#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <cstdint>
#include <optional>

namespace gc::audio {

class IPresentedOutputClock {
public:
    virtual ~IPresentedOutputClock() = default;

    [[nodiscard]] virtual std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept = 0;
    virtual void Invalidate() noexcept = 0;
};

} // namespace gc::audio
