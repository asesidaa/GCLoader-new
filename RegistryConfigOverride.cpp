#include "RegistryConfigOverride.h"

#include <array>
#include <cstring>
#include <memory>
#include <utility>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

RegOpenKeyExAFn g_original_reg_open_key_ex_a = nullptr;
RegQueryValueExAFn g_original_reg_query_value_ex_a = nullptr;
RegCloseKeyFn g_original_reg_close_key = nullptr;
std::unique_ptr<RegistryConfigOverride> g_registry_override;

constexpr std::array<const char*, 3> kRegistryExports{
    "RegOpenKeyExA",
    "RegQueryValueExA",
    "RegCloseKey",
};

enum class OwnedValueIndex : std::size_t {
    Country = 0,
    GameKind = 1,
    EventNextTime = 2,
    ConditionTime = 3,
    LogLevel = 4,
    NewsPath = 5,
    EventPath = 6,
    LogPath = 7,
};

struct RegistryValueView {
    const char* name;
    DWORD type;
    const void* bytes;
    DWORD size;
    OwnedValueIndex index;
};

bool is_type_x_open(HKEY root, LPCSTR subkey) {
    return root == HKEY_LOCAL_MACHINE &&
        subkey != nullptr &&
        EqualsIgnoreCaseAscii(subkey, "SOFTWARE\\taito\\typex");
}

RegistryValueView dword_view(
    const char* name,
    const DWORD& value,
    OwnedValueIndex index) noexcept {
    return {name, REG_DWORD, &value, sizeof(value), index};
}

RegistryValueView string_view(
    const char* name,
    const std::string& value,
    OwnedValueIndex index) noexcept {
    return {
        name,
        REG_SZ,
        value.c_str(),
        static_cast<DWORD>(value.size() + 1),
        index,
    };
}

std::optional<RegistryValueView> find_owned_value(
    ProcessRole role,
    const RegistryOverrideValues& values,
    LPCSTR value_name) noexcept {
    if (value_name == nullptr) {
        return std::nullopt;
    }
    if (role == ProcessRole::Game) {
        if (EqualsIgnoreCaseAscii(value_name, "Country")) {
            return dword_view(
                "Country",
                values.country,
                OwnedValueIndex::Country);
        }
        return std::nullopt;
    }
    if (EqualsIgnoreCaseAscii(value_name, "GameKind")) {
        return dword_view(
            "GameKind",
            values.game_kind,
            OwnedValueIndex::GameKind);
    }
    if (EqualsIgnoreCaseAscii(value_name, "EventNextTime")) {
        return dword_view(
            "EventNextTime",
            values.event_next_time,
            OwnedValueIndex::EventNextTime);
    }
    if (EqualsIgnoreCaseAscii(value_name, "ConditionTime")) {
        return dword_view(
            "ConditionTime",
            values.condition_time,
            OwnedValueIndex::ConditionTime);
    }
    if (EqualsIgnoreCaseAscii(value_name, "LogLevel")) {
        return dword_view(
            "LogLevel",
            values.log_level,
            OwnedValueIndex::LogLevel);
    }
    if (EqualsIgnoreCaseAscii(value_name, "NewsPath")) {
        return string_view(
            "NewsPath",
            values.news_path,
            OwnedValueIndex::NewsPath);
    }
    if (EqualsIgnoreCaseAscii(value_name, "EventPath")) {
        return string_view(
            "EventPath",
            values.event_path,
            OwnedValueIndex::EventPath);
    }
    if (EqualsIgnoreCaseAscii(value_name, "LogPath")) {
        return string_view(
            "LogPath",
            values.log_path,
            OwnedValueIndex::LogPath);
    }
    return std::nullopt;
}

const char* registry_type_name(DWORD type) noexcept {
    return type == REG_DWORD ? "REG_DWORD" : "REG_SZ";
}

LSTATUS copy_registry_value(
    const RegistryValueView& value,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) noexcept {
    if (reserved != nullptr || (data != nullptr && data_size == nullptr)) {
        return ERROR_INVALID_PARAMETER;
    }
    if (type != nullptr) {
        *type = value.type;
    }
    if (data_size == nullptr) {
        return ERROR_SUCCESS;
    }

    const DWORD capacity = *data_size;
    *data_size = value.size;
    if (data == nullptr) {
        return ERROR_SUCCESS;
    }
    if (capacity < value.size) {
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, value.bytes, value.size);
    return ERROR_SUCCESS;
}

LSTATUS WINAPI reg_open_key_ex_a_detour(
    HKEY root,
    LPCSTR subkey,
    DWORD options,
    REGSAM access,
    PHKEY result) {
    if (g_registry_override == nullptr ||
        g_original_reg_open_key_ex_a == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    try {
        return g_registry_override->Open(
            g_original_reg_open_key_ex_a,
            root,
            subkey,
            options,
            access,
            result);
    } catch (...) {
        if (result != nullptr && *result != nullptr) {
            const HKEY opened_handle = *result;
            *result = nullptr;
            if (g_original_reg_close_key != nullptr) {
                g_original_reg_close_key(opened_handle);
            }
        }
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

LSTATUS WINAPI reg_query_value_ex_a_detour(
    HKEY key,
    LPCSTR value_name,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) {
    if (g_registry_override == nullptr ||
        g_original_reg_query_value_ex_a == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    return g_registry_override->Query(
        g_original_reg_query_value_ex_a,
        key,
        value_name,
        reserved,
        type,
        data,
        data_size);
}

LSTATUS WINAPI reg_close_key_detour(HKEY key) {
    if (g_registry_override == nullptr ||
        g_original_reg_close_key == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    return g_registry_override->Close(g_original_reg_close_key, key);
}

} // namespace

std::optional<RegistryOverrideValues> CreateRegistryOverrideValues(
    const RegistryConfig& config) {
    if (!gc::registry_config::ValidateRegistryConfig(config).valid()) {
        return std::nullopt;
    }
    const auto& nesys = config.nesys();
    return RegistryOverrideValues{
        static_cast<DWORD>(
            gc::registry_config::GameCountryRegistryDword(
                config.game().country())),
        static_cast<DWORD>(nesys.game_kind()),
        static_cast<DWORD>(nesys.event_next_time()),
        static_cast<DWORD>(nesys.condition_time()),
        static_cast<DWORD>(nesys.log_level()),
        nesys.news_path(),
        nesys.event_path(),
        nesys.log_path(),
    };
}

RegistryConfigOverride::RegistryConfigOverride(
    ProcessRole role,
    RegistryOverrideValues values)
    : role_(role),
      values_(std::move(values)) {
}

void RegistryConfigOverride::LogFirstTrackedOpen() noexcept {
    if (tracked_open_logged_.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO
            << "RegistryConfigOverride: first tracked Type X open"
            << " role=" << ProcessRoleName(role_);
    } catch (...) {
    }
}

void RegistryConfigOverride::LogFirstOverride(
    std::size_t index,
    const char* value_name,
    DWORD type) noexcept {
    if (override_logged_[index].exchange(true, std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO
            << "RegistryConfigOverride: first value override"
            << " role=" << ProcessRoleName(role_)
            << " value=" << value_name
            << " type=" << registry_type_name(type);
    } catch (...) {
    }
}

LSTATUS RegistryConfigOverride::Open(
    RegOpenKeyExAFn original,
    HKEY root,
    LPCSTR subkey,
    DWORD options,
    REGSAM access,
    PHKEY result) {
    if (original == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    const LSTATUS status =
        original(root, subkey, options, access, result);
    if (status != ERROR_SUCCESS ||
        result == nullptr ||
        *result == nullptr ||
        !is_type_x_open(root, subkey)) {
        return status;
    }
    {
        std::scoped_lock lock(tracked_mutex_);
        tracked_handles_.insert(*result);
    }
    LogFirstTrackedOpen();
    return status;
}

bool RegistryConfigOverride::IsTracked(HKEY key) const noexcept {
    std::scoped_lock lock(tracked_mutex_);
    return tracked_handles_.contains(key);
}

LSTATUS RegistryConfigOverride::Query(
    RegQueryValueExAFn original,
    HKEY key,
    LPCSTR value_name,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) noexcept {
    if (original == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    if (!IsTracked(key)) {
        return original(
            key,
            value_name,
            reserved,
            type,
            data,
            data_size);
    }
    const auto owned = find_owned_value(role_, values_, value_name);
    if (!owned.has_value()) {
        return original(
            key,
            value_name,
            reserved,
            type,
            data,
            data_size);
    }
    const LSTATUS status = copy_registry_value(
        *owned,
        reserved,
        type,
        data,
        data_size);
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA) {
        LogFirstOverride(
            static_cast<std::size_t>(owned->index),
            owned->name,
            owned->type);
    }
    return status;
}

LSTATUS RegistryConfigOverride::Close(
    RegCloseKeyFn original,
    HKEY key) noexcept {
    if (original == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    std::scoped_lock lock(tracked_mutex_);
    const LSTATUS status = original(key);
    if (status == ERROR_SUCCESS) {
        tracked_handles_.erase(key);
    }
    return status;
}

bool InitializeRegistryConfigOverride(
    ProcessRole role,
    const RegistryConfig& config) noexcept {
    try {
        auto values = CreateRegistryOverrideValues(config);
        if (!values.has_value()) {
            return false;
        }
        g_registry_override = std::make_unique<RegistryConfigOverride>(
            role,
            std::move(*values));
        return true;
    } catch (...) {
        return false;
    }
}

std::span<const char* const> RegistryOverrideHookExports() noexcept {
    return kRegistryExports;
}

void AppendRegistryOverrideHookRequests(
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"Advapi32.dll",
        "RegOpenKeyExA",
        reinterpret_cast<LPVOID>(&reg_open_key_ex_a_detour),
        reinterpret_cast<LPVOID*>(&g_original_reg_open_key_ex_a),
    });
    requests.push_back({
        L"Advapi32.dll",
        "RegQueryValueExA",
        reinterpret_cast<LPVOID>(&reg_query_value_ex_a_detour),
        reinterpret_cast<LPVOID*>(&g_original_reg_query_value_ex_a),
    });
    requests.push_back({
        L"Advapi32.dll",
        "RegCloseKey",
        reinterpret_cast<LPVOID>(&reg_close_key_detour),
        reinterpret_cast<LPVOID*>(&g_original_reg_close_key),
    });
}

} // namespace gc::nesys_service
