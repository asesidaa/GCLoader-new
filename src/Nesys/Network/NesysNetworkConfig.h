#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gc::nesys_service {

using Ipv4Octets = std::array<std::uint8_t, 4>;

std::optional<Ipv4Octets> ParseDottedDecimalIpv4(
    std::string_view text) noexcept;

bool IsDottedDecimalIpv4(std::string_view text) noexcept;

} // namespace gc::nesys_service
