#pragma once

#include "Patches/RuntimeImage/RuntimeImageError.h"

#include <expected>

namespace gc::runtime_image {

enum class BytePatchState : std::uint8_t { original, installed, mismatch };

struct BytePatch final {
    SiteIdentity identity;
    BytePattern original;
    BytePattern replacement;
    MemoryKind memory_kind{MemoryKind::code};
};

class RuntimeImage final {
public:
    [[nodiscard]] static std::expected<RuntimeImage, RuntimeImageError>
    MainModule() noexcept;

    [[nodiscard]] std::uintptr_t base() const noexcept { return base_; }
    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }
    [[nodiscard]] std::expected<std::uintptr_t, RuntimeImageError>
    Resolve(const SiteIdentity&, std::size_t) const noexcept;
    [[nodiscard]] std::expected<BytePattern, RuntimeImageError>
    Read(const SiteIdentity&, std::uint8_t) const noexcept;
    [[nodiscard]] std::expected<BytePatchState, RuntimeImageError>
    Inspect(const BytePatch&) const noexcept;
    [[nodiscard]] std::expected<void, RuntimeImageError>
    Write(const SiteIdentity&, BytePattern replacement, MemoryKind) const noexcept;
    [[nodiscard]] std::expected<void, RuntimeImageError>
    ExchangePointer(const SiteIdentity&, void* expected, void* replacement) const noexcept;

private:
    RuntimeImage(std::uintptr_t base, std::uint32_t size) noexcept
        : base_{base}, size_{size} {}
    std::uintptr_t base_{};
    std::uint32_t size_{};
};

} // namespace gc::runtime_image
