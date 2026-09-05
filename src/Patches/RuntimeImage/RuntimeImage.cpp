#include "Patches/RuntimeImage/RuntimeImage.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace gc::runtime_image {
namespace {

constexpr auto kAddressMaximum = std::numeric_limits<std::uintptr_t>::max();

RuntimeImageError Error(
    MemoryStage stage, const SiteIdentity& identity,
    std::uintptr_t address, std::size_t size, DWORD code) noexcept {
    return {.stage = stage, .identity = identity, .address = address,
            .size = size, .win32_error = code};
}

bool ValidPattern(const BytePattern& pattern) noexcept {
    return pattern.size != 0 && pattern.size <= kMaximumPatternBytes;
}

bool EqualBytes(const BytePattern& left, const BytePattern& right) noexcept {
    return left.size == right.size && ValidPattern(left) &&
        std::ranges::equal(left.view(), right.view());
}

template <typename Value>
bool ReadValue(std::uintptr_t address, Value& value, DWORD& error) noexcept {
    SIZE_T count{};
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                           &value, sizeof(value), &count)) {
        error = GetLastError();
        return false;
    }
    if (count != sizeof(value)) {
        error = ERROR_PARTIAL_COPY;
        return false;
    }
    return true;
}

struct ProtectionChange final {
    void* address{};
    std::size_t size{};
    DWORD original{};
};

// A bounded pattern can intersect at most two Windows pages. Save each
// region's protection separately: VirtualProtect reports only its first page.
struct ProtectionChanges final {
    std::array<ProtectionChange, 2> entries{};
    std::size_t count{};

    void Restore(RuntimeImageError& error, bool& failed) const noexcept {
        error.restore_attempted = count != 0;
        error.restore_succeeded = count != 0;
        for (std::size_t index = count; index > 0; --index) {
            const auto& entry = entries[index - 1];
            DWORD ignored{};
            if (!VirtualProtect(entry.address, entry.size, entry.original, &ignored)) {
                const auto code = GetLastError();
                error.restore_succeeded = false;
                if (!failed) {
                    failed = true;
                    error.stage = MemoryStage::restore_protection;
                    error.win32_error = code;
                }
            }
        }
    }
};

std::expected<ProtectionChanges, RuntimeImageError> MakeWritable(
    const SiteIdentity& identity, std::uintptr_t address, std::size_t size,
    DWORD protection) noexcept {
    ProtectionChanges changes{};
    auto cursor = address;
    const auto end = address + size;
    auto error = Error(MemoryStage::protect, identity, address, size, ERROR_SUCCESS);
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) == 0) {
            error.stage = MemoryStage::query;
            error.win32_error = GetLastError();
            bool failed = true;
            changes.Restore(error, failed);
            return std::unexpected(error);
        }
        const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (changes.count == changes.entries.size() || region.RegionSize == 0 ||
            region.RegionSize > kAddressMaximum - start || start > cursor ||
            start + region.RegionSize <= cursor) {
            error.win32_error = ERROR_INVALID_ADDRESS;
            bool failed = true;
            changes.Restore(error, failed);
            return std::unexpected(error);
        }
        auto& change = changes.entries[changes.count];
        change.address = reinterpret_cast<void*>(cursor);
        change.size = std::min<std::uintptr_t>(end, start + region.RegionSize) - cursor;
        if (!VirtualProtect(change.address, change.size, protection, &change.original)) {
            error.win32_error = GetLastError();
            bool failed = true;
            changes.Restore(error, failed);
            return std::unexpected(error);
        }
        ++changes.count;
        cursor += change.size;
    }
    return changes;
}

BytePattern PointerPattern(void* value) noexcept {
    static_assert(sizeof(value) == 4, "RuntimeImage targets x86");
    BytePattern pattern{};
    pattern.size = sizeof(value);
    std::memcpy(pattern.bytes.data(), &value, sizeof(value));
    return pattern;
}

bool GuardedExchange(void* address, void* expected, void* replacement,
                     void*& observed) noexcept {
    __try {
        observed = InterlockedCompareExchangePointer(
            static_cast<void* volatile*>(address), replacement, expected);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

std::expected<RuntimeImage, RuntimeImageError> RuntimeImage::MainModule() noexcept {
    constexpr SiteIdentity identity{"RuntimeImage", "main_module", 0};
    const auto module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return std::unexpected(Error(MemoryStage::resolve_module, identity, 0, 0, GetLastError()));
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    IMAGE_DOS_HEADER dos{};
    DWORD code{};
    if (!ReadValue(base, dos, code)) {
        return std::unexpected(Error(MemoryStage::parse_image, identity, base, sizeof(dos), code));
    }
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        static_cast<std::uintptr_t>(dos.e_lfanew) > kAddressMaximum - base) {
        return std::unexpected(Error(MemoryStage::parse_image, identity, base, sizeof(dos),
                                     ERROR_BAD_EXE_FORMAT));
    }
    const auto nt_address = base + static_cast<std::uintptr_t>(dos.e_lfanew);
    IMAGE_NT_HEADERS32 nt{};
    if (sizeof(nt) > kAddressMaximum - nt_address ||
        !ReadValue(nt_address, nt, code)) {
        return std::unexpected(Error(MemoryStage::parse_image, identity, nt_address,
                                     sizeof(nt), code == 0 ? ERROR_BAD_EXE_FORMAT : code));
    }
    if (nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage == 0 ||
        nt.OptionalHeader.SizeOfImage > kAddressMaximum - base ||
        static_cast<std::uint32_t>(dos.e_lfanew) > nt.OptionalHeader.SizeOfImage ||
        sizeof(nt) > nt.OptionalHeader.SizeOfImage - static_cast<std::uint32_t>(dos.e_lfanew)) {
        return std::unexpected(Error(MemoryStage::parse_image, identity, nt_address,
                                     sizeof(nt), ERROR_BAD_EXE_FORMAT));
    }
    return RuntimeImage{base, nt.OptionalHeader.SizeOfImage};
}

std::expected<std::uintptr_t, RuntimeImageError> RuntimeImage::Resolve(
    const SiteIdentity& identity, std::size_t span) const noexcept {
    if (span == 0 || identity.rva >= size_ || span > size_ - identity.rva ||
        identity.rva > kAddressMaximum - base_) {
        return std::unexpected(Error(MemoryStage::address_range, identity, base_, span,
                                     ERROR_INVALID_ADDRESS));
    }
    const auto address = base_ + identity.rva;
    if (span > kAddressMaximum - address) {
        return std::unexpected(Error(MemoryStage::address_range, identity, address, span,
                                     ERROR_ARITHMETIC_OVERFLOW));
    }
    const auto end = address + span;
    for (auto cursor = address; cursor < end;) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) == 0) {
            return std::unexpected(Error(MemoryStage::query, identity, cursor, span, GetLastError()));
        }
        const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.State != MEM_COMMIT || region.Protect == 0 ||
            (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
            region.RegionSize == 0 || start > cursor ||
            region.RegionSize > kAddressMaximum - start || start + region.RegionSize <= cursor) {
            return std::unexpected(Error(MemoryStage::query, identity, cursor, span, ERROR_NOACCESS));
        }
        cursor = std::min<std::uintptr_t>(end, start + region.RegionSize);
    }
    return address;
}

std::expected<BytePattern, RuntimeImageError> RuntimeImage::Read(
    const SiteIdentity& identity, std::uint8_t count) const noexcept {
    if (count == 0 || count > kMaximumPatternBytes) {
        return std::unexpected(Error(MemoryStage::read, identity, 0, count, ERROR_INVALID_PARAMETER));
    }
    const auto address = Resolve(identity, count);
    if (!address) {
        return std::unexpected(address.error());
    }
    BytePattern result{};
    SIZE_T transferred{};
    const bool read = ReadProcessMemory(
        GetCurrentProcess(), reinterpret_cast<const void*>(*address),
        result.bytes.data(), count, &transferred) != FALSE;
    const auto code = read ? ERROR_PARTIAL_COPY : GetLastError();
    result.size = static_cast<std::uint8_t>(std::min<SIZE_T>(transferred, count));
    if (!read || transferred != count) {
        auto error = Error(MemoryStage::read, identity, *address, count, code);
        error.observed = result;
        return std::unexpected(error);
    }
    return result;
}

std::expected<BytePatchState, RuntimeImageError> RuntimeImage::Inspect(
    const BytePatch& patch) const noexcept {
    if (!ValidPattern(patch.original) || !ValidPattern(patch.replacement) ||
        patch.original.size != patch.replacement.size) {
        return std::unexpected(Error(MemoryStage::read, patch.identity, 0, patch.original.size,
                                     ERROR_INVALID_PARAMETER));
    }
    const auto actual = Read(patch.identity, patch.original.size);
    if (!actual) {
        return std::unexpected(actual.error());
    }
    if (EqualBytes(*actual, patch.original)) {
        return BytePatchState::original;
    }
    if (EqualBytes(*actual, patch.replacement)) {
        return BytePatchState::installed;
    }
    return BytePatchState::mismatch;
}

std::expected<void, RuntimeImageError> RuntimeImage::Write(
    const SiteIdentity& identity, BytePattern replacement, MemoryKind kind) const noexcept {
    if (!ValidPattern(replacement) || (kind != MemoryKind::code && kind != MemoryKind::data)) {
        return std::unexpected(Error(MemoryStage::write, identity, 0, replacement.size,
                                     ERROR_INVALID_PARAMETER));
    }
    const auto address = Resolve(identity, replacement.size);
    if (!address) {
        return std::unexpected(address.error());
    }
    const auto protection = MakeWritable(identity, *address, replacement.size,
        kind == MemoryKind::code ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
    if (!protection) {
        auto error = protection.error();
        error.expected = replacement;
        return std::unexpected(error);
    }

    auto error = Error(MemoryStage::write, identity, *address, replacement.size, ERROR_SUCCESS);
    error.expected = replacement;
    // A failing kernel copy may have changed a prefix even when its count is zero.
    error.memory_changed = true;
    SIZE_T transferred{};
    const bool written = WriteProcessMemory(
        GetCurrentProcess(), reinterpret_cast<void*>(*address),
        replacement.bytes.data(), replacement.size, &transferred) != FALSE;
    const auto write_error = written ? ERROR_PARTIAL_COPY : GetLastError();
    bool failed = !written || transferred != replacement.size;
    if (failed) {
        error.win32_error = write_error;
    }
    if (kind == MemoryKind::code &&
        !FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(*address),
                               replacement.size)) {
        const auto code = GetLastError();
        if (!failed) {
            failed = true;
            error.stage = MemoryStage::flush_instruction_cache;
            error.win32_error = code;
        }
    }
    protection->Restore(error, failed);
    if (failed) {
        return std::unexpected(error);
    }

    const auto actual = Read(identity, replacement.size);
    if (!actual || !EqualBytes(*actual, replacement)) {
        error.stage = MemoryStage::read_back;
        error.win32_error = actual ? ERROR_INVALID_DATA : actual.error().win32_error;
        error.observed = actual ? *actual : actual.error().observed;
        return std::unexpected(error);
    }
    return {};
}

std::expected<void, RuntimeImageError> RuntimeImage::ExchangePointer(
    const SiteIdentity& identity, void* expected, void* replacement) const noexcept {
    const auto address = Resolve(identity, sizeof(void*));
    if (!address) {
        return std::unexpected(address.error());
    }
    if (*address % alignof(void*) != 0) {
        return std::unexpected(Error(MemoryStage::compare_exchange, identity, *address,
                                     sizeof(void*), ERROR_INVALID_ADDRESS));
    }
    const auto protection = MakeWritable(identity, *address, sizeof(void*), PAGE_READWRITE);
    if (!protection) {
        auto error = protection.error();
        error.expected = PointerPattern(expected);
        return std::unexpected(error);
    }
    auto error = Error(MemoryStage::compare_exchange, identity, *address,
                       sizeof(void*), ERROR_SUCCESS);
    error.expected = PointerPattern(expected);
    void* observed{};
    bool failed = !GuardedExchange(reinterpret_cast<void*>(*address), expected, replacement, observed);
    if (failed) {
        error.win32_error = ERROR_NOACCESS;
        error.memory_changed = true;
    } else {
        error.observed = PointerPattern(observed);
        failed = observed != expected;
        error.memory_changed = !failed;
        if (failed) {
            error.win32_error = ERROR_INVALID_DATA;
        }
    }
    protection->Restore(error, failed);
    if (failed) {
        return std::unexpected(error);
    }
    const auto actual = Read(identity, sizeof(void*));
    if (!actual || !EqualBytes(*actual, PointerPattern(replacement))) {
        error.stage = MemoryStage::read_back;
        error.expected = PointerPattern(replacement);
        error.observed = actual ? *actual : actual.error().observed;
        error.win32_error = actual ? ERROR_INVALID_DATA : actual.error().win32_error;
        return std::unexpected(error);
    }
    return {};
}

const char* MemoryStageName(MemoryStage stage) noexcept {
    switch (stage) {
    case MemoryStage::resolve_module: return "resolve_module";
    case MemoryStage::parse_image: return "parse_image";
    case MemoryStage::address_range: return "address_range";
    case MemoryStage::query: return "query";
    case MemoryStage::read: return "read";
    case MemoryStage::protect: return "protect";
    case MemoryStage::write: return "write";
    case MemoryStage::flush_instruction_cache: return "flush_instruction_cache";
    case MemoryStage::restore_protection: return "restore_protection";
    case MemoryStage::read_back: return "read_back";
    case MemoryStage::compare_exchange: return "compare_exchange";
    case MemoryStage::publish_original: return "publish_original";
    case MemoryStage::register_vtable_slot: return "register_vtable_slot";
    }
    return "unknown";
}

} // namespace gc::runtime_image
