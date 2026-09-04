#pragma once
#include "Patches/GameVersion/GameBuild.h"
#include "Patches/GameVersion/ImageIdentity.h"
#include "Nesys/NesysBuild.h"
#include <span>

namespace gc::game_version {
template <class Build, class Variant>
struct KnownImageDescriptor final {
    Build build;
    Variant variant;
    Sha256Digest sha256;
    std::uint64_t file_size{};
    std::uint16_t machine{IMAGE_FILE_MACHINE_I386};
    std::uint32_t preferred_image_base{};
    std::uint32_t size_of_image{};
    std::uint32_t time_date_stamp{};
};
using KnownGameImage = KnownImageDescriptor<GameBuild, GameImageVariant>;
using KnownNesysImage = KnownImageDescriptor<nesys_service::NesysBuild, nesys_service::NesysImageVariant>;
[[nodiscard]] std::span<const KnownGameImage> KnownGameImages() noexcept;
[[nodiscard]] std::span<const KnownNesysImage> KnownNesysImages() noexcept;
}
