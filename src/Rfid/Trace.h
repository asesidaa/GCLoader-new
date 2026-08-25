#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>

namespace gc::rfid::trace {

[[nodiscard]] inline std::string FormatBytes(
    std::span<const std::byte> bytes,
    std::size_t limit = 64)
{
    if (bytes.empty()) {
        return "<empty>";
    }

    const auto shown = std::min(bytes.size(), limit);
    std::string result;
    result.reserve(shown * 3 + 32);
    for (std::size_t index = 0; index < shown; ++index) {
        if (index != 0) {
            result.push_back(' ');
        }
        static constexpr char digits[] = "0123456789abcdef";
        const auto value = std::to_integer<unsigned int>(bytes[index]);
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0F]);
    }

    if (shown != bytes.size()) {
        result += " ... (+";
        result += std::to_string(bytes.size() - shown);
        result += " bytes)";
    }
    return result;
}

} // namespace gc::rfid::trace
