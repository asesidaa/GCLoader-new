#pragma once
#include "Config/Validation/ValidationContext.h"
#include <rfl.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace gc::config::validation {
inline bool IsUtf8ContinuationByte(unsigned char value) noexcept
{
    return value >= 0x80U && value <= 0xBFU;
}

inline bool IsValidUtf8(std::string_view value) noexcept
{
    std::size_t index = 0;
    while (index < value.size())
    {
        const auto first =
            static_cast<unsigned char>(value[index]);
        if (first <= 0x7FU)
        {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU)
        {
            if (index + 1 >= value.size() ||
                !IsUtf8ContinuationByte(
                    static_cast<unsigned char>(value[index + 1])))
            {
                return false;
            }
            index += 2;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU)
        {
            if (index + 2 >= value.size())
            {
                return false;
            }
            const auto second =
                static_cast<unsigned char>(value[index + 1]);
            const auto third =
                static_cast<unsigned char>(value[index + 2]);
            const bool valid_second =
                first == 0xE0U
                    ? second >= 0xA0U && second <= 0xBFU
                    : first == 0xEDU
                    ? second >= 0x80U && second <= 0x9FU
                    : IsUtf8ContinuationByte(second);
            if (!valid_second || !IsUtf8ContinuationByte(third))
            {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U)
        {
            if (index + 3 >= value.size())
            {
                return false;
            }
            const auto second =
                static_cast<unsigned char>(value[index + 1]);
            const auto third =
                static_cast<unsigned char>(value[index + 2]);
            const auto fourth =
                static_cast<unsigned char>(value[index + 3]);
            const bool valid_second =
                first == 0xF0U
                    ? second >= 0x90U && second <= 0xBFU
                    : first == 0xF4U
                    ? second >= 0x80U && second <= 0x8FU
                    : IsUtf8ContinuationByte(second);
            if (!valid_second ||
                !IsUtf8ContinuationByte(third) ||
                !IsUtf8ContinuationByte(fourth))
            {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

template <class Validator, class T>
bool ValidateLeaf(
    const T& value,
    ConfigPath path,
    ConfigErrorCode code,
    std::string message,
    ConfigErrors& errors)
{
    if (Validator::from_value(value))
    {
        return true;
    }
    errors.push_back({
        .path = std::move(path),
        .code = code,
        .message = std::move(message),
    });
    return false;
}

}
