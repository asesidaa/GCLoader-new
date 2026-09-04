#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gc::runtime_image {

using Rva = std::uint32_t;
inline constexpr std::size_t kMaximumPatternBytes = 32;

struct BytePattern final {
    std::array<std::byte, kMaximumPatternBytes> bytes{};
    std::uint8_t size{};

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return {bytes.data(), size <= bytes.size() ? size : 0U};
    }

    friend bool operator==(const BytePattern&, const BytePattern&) = default;
};

template <std::uint8_t... Values>
[[nodiscard]] consteval BytePattern PatternOf() noexcept {
    static_assert(sizeof...(Values) > 0);
    static_assert(sizeof...(Values) <= kMaximumPatternBytes);
    BytePattern pattern{};
    std::size_t index{};
    ((pattern.bytes[index++] = std::byte{Values}), ...);
    pattern.size = static_cast<std::uint8_t>(sizeof...(Values));
    return pattern;
}

} // namespace gc::runtime_image
