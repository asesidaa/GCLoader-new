#include "Patches/GameVersion/KnownImages.h"

namespace gc::game_version {
namespace {
consteval Sha256Digest Digest(const char (&text)[65]) {
    Sha256Digest result;
    const auto nibble = [](char value) -> unsigned {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        throw "Invalid SHA-256 literal";
    };
    for (std::size_t i = 0; i < result.bytes.size(); ++i)
        result.bytes[i] = static_cast<std::byte>(nibble(text[2*i]) * 16 + nibble(text[2*i+1]));
    return result;
}
// PE facts read from the three source evidence executables, 2026-09-05.
constexpr KnownGameImage kGameImages[]{
    {GameBuild::groove_coaster_471, GameImageVariant::clean,
     Digest("795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF"),
     3691008, IMAGE_FILE_MACHINE_I386, 0x00400000, 0x00433000, 0x5FA90825},
    {GameBuild::groove_coaster_471, GameImageVariant::legacy_patched,
     Digest("FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522"),
     3691008, IMAGE_FILE_MACHINE_I386, 0x00400000, 0x00433000, 0x5FA90825},
};
constexpr KnownNesysImage kNesysImages[]{
    {nesys_service::NesysBuild::service_297, nesys_service::NesysImageVariant::original,
     Digest("487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2"),
     368640, IMAGE_FILE_MACHINE_I386, 0x00400000, 0x0005C000, 0x5AB4CFB7},
};
}
std::span<const KnownGameImage> KnownGameImages() noexcept { return kGameImages; }
std::span<const KnownNesysImage> KnownNesysImages() noexcept { return kNesysImages; }
}
