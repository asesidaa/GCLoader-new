#include "Input/Types/PhysicalKey.h"

#include <charconv>
#include <format>

namespace gc::input {

std::expected<PhysicalKey, std::string> ParsePhysicalKey(
    std::string_view token)
{
    if (token.size() != 7 || token[2] != ':')
    {
        return std::unexpected(
            "expected sc:hhhh, e0:hhhh, or e1:hhhh");
    }

    ScanCodePrefix prefix{};
    if (token.starts_with("sc:"))
    {
        prefix = ScanCodePrefix::None;
    }
    else if (token.starts_with("e0:"))
    {
        prefix = ScanCodePrefix::E0;
    }
    else if (token.starts_with("e1:"))
    {
        prefix = ScanCodePrefix::E1;
    }
    else
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

    return PhysicalKey{make_code, prefix};
}

std::string FormatPhysicalKey(PhysicalKey key)
{
    std::string_view prefix = "sc";
    switch (key.prefix)
    {
    case ScanCodePrefix::None:
        break;
    case ScanCodePrefix::E0:
        prefix = "e0";
        break;
    case ScanCodePrefix::E1:
        prefix = "e1";
        break;
    }

    return std::format("{}:{:04x}", prefix, key.make_code);
}

} // namespace gc::input
