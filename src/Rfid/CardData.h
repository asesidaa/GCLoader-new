#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace gc::rfid {

using CardData = std::array<std::uint8_t, 24>;

inline constexpr CardData kDefaultCardData{
    0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00,
    '7', '0', '2', '0', '3', '9', '2', '0',
    '1', '0', '2', '8', '1', '5', '0', '2'};

[[nodiscard]] CardData LoadCardData(
    const std::filesystem::path& path) noexcept;
[[nodiscard]] CardData LoadCurrentDirectoryCardData() noexcept;

} // namespace gc::rfid
