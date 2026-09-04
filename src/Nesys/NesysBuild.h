#pragma once
#include <cstdint>
namespace gc::nesys_service {
enum class NesysBuild : std::uint8_t { current_supported };
enum class NesysImageVariant : std::uint8_t { original, locally_verified };
}
