#pragma once

#include <cstdint>

namespace gc::timing {

struct AbsoluteHostTime final {
    std::int64_t qpc_ticks{};
    std::uint32_t multimedia_time_ms{};
};

} // namespace gc::timing
