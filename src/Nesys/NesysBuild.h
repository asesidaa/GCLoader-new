#pragma once
#include <cstdint>
namespace gc::nesys_service {
enum class NesysBuild : std::uint8_t { service_297, service_2861 };
enum class NesysImageVariant : std::uint8_t { original, locally_verified };
}
