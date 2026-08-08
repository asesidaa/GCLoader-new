#pragma once

#include <Windows.h>

#include <algorithm>

namespace gc::audio::detail {

inline constexpr DWORD kExclusiveAudioMaxStartupTimeoutMs = 10'000;
inline constexpr DWORD kExclusiveAudioSummaryIntervalMs = 30'000;

struct ExclusiveAudioEngineTiming {
    DWORD summary_interval_ms{kExclusiveAudioSummaryIntervalMs};
};

constexpr DWORD ClampExclusiveAudioStartupTimeout(DWORD timeout_ms) noexcept {
    return std::min(timeout_ms, kExclusiveAudioMaxStartupTimeoutMs);
}

} // namespace gc::audio::detail
