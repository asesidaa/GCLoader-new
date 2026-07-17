#pragma once

#include <Windows.h>

#include "Nesys/NesysHookTransaction.h"
#include "Nesys/NesysServiceProcess.h"
#include "Config/RegistryConfig.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace gc::nesys_service {

using RegOpenKeyExAFn = LSTATUS(WINAPI*)(
    HKEY,
    LPCSTR,
    DWORD,
    REGSAM,
    PHKEY);
using RegQueryValueExAFn = LSTATUS(WINAPI*)(
    HKEY,
    LPCSTR,
    LPDWORD,
    LPDWORD,
    LPBYTE,
    LPDWORD);
using RegCloseKeyFn = LSTATUS(WINAPI*)(HKEY);

struct RegistryOverrideValues {
    DWORD country{0};
    DWORD game_kind{0};
    DWORD event_next_time{0};
    DWORD condition_time{0};
    DWORD traffic_count{0};
    DWORD log_level{0};
    std::string news_path;
    std::string event_path;
    std::string log_path;
};

std::optional<RegistryOverrideValues> CreateRegistryOverrideValues(
    const RegistryConfig& config);

class RegistryConfigOverride {
public:
    RegistryConfigOverride(
        ProcessRole role,
        RegistryOverrideValues values);

    RegistryConfigOverride(const RegistryConfigOverride&) = delete;
    RegistryConfigOverride& operator=(const RegistryConfigOverride&) = delete;

    LSTATUS Open(
        RegOpenKeyExAFn original,
        HKEY root,
        LPCSTR subkey,
        DWORD options,
        REGSAM access,
        PHKEY result);
    LSTATUS Query(
        RegQueryValueExAFn original,
        HKEY key,
        LPCSTR value_name,
        LPDWORD reserved,
        LPDWORD type,
        LPBYTE data,
        LPDWORD data_size) noexcept;
    LSTATUS Close(
        RegCloseKeyFn original,
        HKEY key) noexcept;

private:
    bool IsTracked(HKEY key) const noexcept;
    bool IsSynthetic(HKEY key) const noexcept;
    HKEY SyntheticHandle() const noexcept;
    void LogFirstTrackedOpen() noexcept;
    void LogFirstSyntheticOpen(LSTATUS physical_status) noexcept;
    void LogFirstOverride(
        std::size_t index,
        const char* value_name,
        DWORD type) noexcept;

    const ProcessRole role_;
    const RegistryOverrideValues values_;
    mutable std::mutex tracked_mutex_;
    std::unordered_set<HKEY> tracked_handles_;
    std::size_t synthetic_open_count_{0};
    std::atomic_bool tracked_open_logged_{false};
    std::atomic_bool synthetic_open_logged_{false};
    std::array<std::atomic_bool, 9> override_logged_{};
};

bool InitializeRegistryConfigOverride(
    ProcessRole role,
    const RegistryConfig& config) noexcept;
std::span<const char* const> RegistryOverrideHookExports() noexcept;
void AppendRegistryOverrideHookRequests(
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service
