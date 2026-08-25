#pragma once

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace gc::rfid {

using CardData = std::array<std::uint8_t, 24>;

inline constexpr std::string_view kDefaultCardNumber{
    "7020392010281502"};
static_assert(kDefaultCardNumber.size() == 16);

inline constexpr CardData kDefaultCardData = [] {
    CardData result{
        0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00};
    for (std::size_t index = 0;
         index < kDefaultCardNumber.size();
         ++index) {
        result[8 + index] = static_cast<std::uint8_t>(
            kDefaultCardNumber[index]);
    }
    return result;
}();

[[nodiscard]] std::optional<CardData> ParseCardNumber(
    std::string_view number) noexcept;

[[nodiscard]] CardData LoadCardData(
    const std::filesystem::path& path) noexcept;
[[nodiscard]] CardData LoadCurrentDirectoryCardData() noexcept;

} // namespace gc::rfid
