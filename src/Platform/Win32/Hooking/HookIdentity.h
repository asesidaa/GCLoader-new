#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace gc::hooking {
struct HookIdentity final {
    std::string_view feature;
    std::string_view site;
    friend bool operator==(const HookIdentity&, const HookIdentity&) = default;
};
struct ExportTarget final {
    std::wstring_view module;
    std::string_view name;
};
struct ResolvedHookTarget final {
    HookIdentity identity;
    std::uintptr_t address{};
    std::optional<ExportTarget> export_target;
};
enum class HookKind : std::uint8_t { inline_detour, mid_detour };
enum class HookSharing : std::uint8_t { exclusive, named_dispatcher };
}
