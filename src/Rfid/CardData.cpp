#include "Rfid/CardData.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace gc::rfid {
namespace {

constexpr bool IsAsciiWhitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f' || value == '\v';
}

std::string_view TrimAsciiWhitespace(std::string_view value) noexcept
{
    while (!value.empty() && IsAsciiWhitespace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && IsAsciiWhitespace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

CardData AssembleCardData(std::string_view number) noexcept
{
    auto result = kDefaultCardData;
    std::ranges::transform(
        number,
        result.begin() + 8,
        [](char value) { return static_cast<std::uint8_t>(value); });
    return result;
}

} // namespace

std::optional<CardData> ParseCardNumber(
    std::string_view number) noexcept
{
    if (number.size() != 16 ||
        !std::ranges::all_of(number, [](char value) {
            return value >= '0' && value <= '9';
        })) {
        return std::nullopt;
    }
    return AssembleCardData(number);
}

CardData LoadCardData(const std::filesystem::path& path) noexcept
{
    try {
        std::ifstream input{path, std::ios::binary};
        if (!input) {
            return kDefaultCardData;
        }

        const std::string contents{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        if (input.bad()) {
            return kDefaultCardData;
        }

        if (const auto parsed = ParseCardNumber(
                TrimAsciiWhitespace(contents))) {
            return *parsed;
        }
        return kDefaultCardData;
    } catch (...) {
        return kDefaultCardData;
    }
}

CardData LoadCurrentDirectoryCardData() noexcept
{
    try {
        return LoadCardData(std::filesystem::path{L"card.txt"});
    } catch (...) {
        return kDefaultCardData;
    }
}

} // namespace gc::rfid
