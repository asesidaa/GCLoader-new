#include "RegistryConfig.h"

#include <limits>

namespace gc::registry_config {

bool IsRegistryDword(std::int64_t value) noexcept {
    return value >= 0 &&
        static_cast<std::uint64_t>(value) <=
            std::numeric_limits<std::uint32_t>::max();
}

bool IsRegistryLogLevel(std::int64_t value) noexcept {
    return value >= 0 && value <= 3;
}

bool IsRegistryPath(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 259;
}

RegistryValidationResult ValidateRegistryConfig(
    const RegistryConfig& config) noexcept {
    const auto& nesys = config.nesys();
    return {
        IsRegistryDword(nesys.game_kind()),
        IsRegistryDword(nesys.event_next_time()),
        IsRegistryDword(nesys.condition_time()),
        IsRegistryLogLevel(nesys.log_level()),
        IsRegistryPath(nesys.news_path()),
        IsRegistryPath(nesys.event_path()),
        IsRegistryPath(nesys.log_path()),
    };
}

const char* FirstRegistryValidationError(
    const RegistryValidationResult& validation) noexcept {
    if (!validation.game_kind) {
        return "[registry.nesys].game_kind must be an unsigned 32-bit integer";
    }
    if (!validation.event_next_time) {
        return "[registry.nesys].event_next_time must be an unsigned 32-bit integer";
    }
    if (!validation.condition_time) {
        return "[registry.nesys].condition_time must be an unsigned 32-bit integer";
    }
    if (!validation.log_level) {
        return "[registry.nesys].log_level must be an integer from 0 through 3";
    }
    if (!validation.news_path) {
        return "[registry.nesys].news_path must contain 1 through 259 bytes";
    }
    if (!validation.event_path) {
        return "[registry.nesys].event_path must contain 1 through 259 bytes";
    }
    if (!validation.log_path) {
        return "[registry.nesys].log_path must contain 1 through 259 bytes";
    }
    return nullptr;
}

} // namespace gc::registry_config
