#pragma once

#include "Patches/RuntimeImage/BytePattern.h"

#include <Windows.h>
#include <string_view>

namespace gc::runtime_image {

enum class MemoryKind : std::uint8_t { code, data };
enum class MemoryStage : std::uint8_t {
    resolve_module,
    parse_image,
    address_range,
    query,
    read,
    protect,
    write,
    flush_instruction_cache,
    restore_protection,
    read_back,
    compare_exchange,
    publish_original,
    register_vtable_slot,
};

struct SiteIdentity final {
    std::string_view feature;
    std::string_view site;
    Rva rva{};
};

struct RuntimeImageError final {
    MemoryStage stage{};
    SiteIdentity identity{};
    std::uintptr_t address{};
    std::size_t size{};
    BytePattern expected{};
    BytePattern observed{};
    DWORD win32_error{};
    bool memory_changed{};
    bool restore_attempted{};
    bool restore_succeeded{};
};

[[nodiscard]] const char* MemoryStageName(MemoryStage stage) noexcept;

} // namespace gc::runtime_image
