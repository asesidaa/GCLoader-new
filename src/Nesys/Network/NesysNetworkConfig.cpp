#include "Nesys/Network/NesysNetworkConfig.h"

#include <charconv>
#include <system_error>

namespace gc::nesys_service {

std::optional<Ipv4Octets> ParseDottedDecimalIpv4(
    std::string_view text) noexcept {
    Ipv4Octets octets{};
    std::size_t begin = 0;

    for (std::size_t index = 0; index < octets.size(); ++index) {
        std::size_t end = text.find('.', begin);
        if (index + 1 < octets.size()) {
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
        } else {
            if (end != std::string_view::npos) {
                return std::nullopt;
            }
            end = text.size();
        }

        const auto token = text.substr(begin, end - begin);
        if (token.empty() || token.size() > 3) {
            return std::nullopt;
        }

        unsigned value = 0;
        const auto [parsed_end, error] = std::from_chars(
            token.data(),
            token.data() + token.size(),
            value,
            10);
        if (error != std::errc{} ||
            parsed_end != token.data() + token.size() ||
            value > 255) {
            return std::nullopt;
        }

        octets[index] = static_cast<std::uint8_t>(value);
        begin = end + 1;
    }

    return octets;
}

bool IsDottedDecimalIpv4(std::string_view text) noexcept {
    return ParseDottedDecimalIpv4(text).has_value();
}

} // namespace gc::nesys_service
