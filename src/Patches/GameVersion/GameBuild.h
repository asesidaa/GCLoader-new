#pragma once
#include <cstdint>
namespace gc::game_version {
enum class GameBuild : std::uint8_t { groove_coaster_471, groove_coaster_206 };
enum class GameImageVariant : std::uint8_t { clean, legacy_patched, locally_verified };
}
