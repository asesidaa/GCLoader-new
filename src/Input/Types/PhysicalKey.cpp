#include "Input/Types/PhysicalKey.h"

#include <array>
#include <charconv>
#include <format>
#include <optional>

namespace gc::input {

namespace {
struct ScanCodePrefixToken final {
    ScanCodePrefix value;
    std::string_view token;
};

constexpr std::array<ScanCodePrefixToken, 3> kPrefixTokens{{
    {ScanCodePrefix::None, "sc"},
    {ScanCodePrefix::E0, "e0"},
    {ScanCodePrefix::E1, "e1"},
}};

constexpr std::string_view kUnknownPrefixFallback = "sc";
} // namespace

std::expected<PhysicalKey, std::string> ParsePhysicalKey(
    std::string_view token)
{
    if (token.size() != 7 || token[2] != ':')
    {
        return std::unexpected(
            "expected sc:hhhh, e0:hhhh, or e1:hhhh");
    }

    std::optional<ScanCodePrefix> prefix;
    for (const auto& row : kPrefixTokens)
    {
        if (row.token == token.substr(0, 2))
        {
            prefix = row.value;
            break;
        }
    }
    if (!prefix)
    {
        return std::unexpected(
            "expected lowercase sc, e0, or e1 prefix");
    }

    std::uint16_t make_code{};
    const auto digits = token.substr(3);
    const auto* begin = digits.data();
    const auto* end = begin + digits.size();
    const auto parsed = std::from_chars(begin, end, make_code, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
    {
        return std::unexpected("expected four hexadecimal digits");
    }
    if (make_code == 0)
    {
        return std::unexpected("scan-code make value must not be zero");
    }

    return PhysicalKey{make_code, *prefix};
}

std::string FormatPhysicalKey(PhysicalKey key)
{
    for (const auto& row : kPrefixTokens)
    {
        if (row.value == key.prefix)
        {
            return std::format("{}:{:04x}", row.token, key.make_code);
        }
    }

    return std::format("{}:{:04x}", kUnknownPrefixFallback, key.make_code);
}

} // namespace gc::input
