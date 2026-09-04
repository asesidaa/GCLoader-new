#pragma once
#include "Patches/GameVersion/FeatureCapability.h"
#include <optional>
namespace gc::game_version {
template <class Profile, class Build, class Variant>
using ProfileSelector = std::optional<Profile> (*)(Build, Variant) noexcept;
}
