#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace gc::game_version {
struct Sha256Digest final {
    std::array<std::byte, 32> bytes{};
    friend bool operator==(const Sha256Digest&, const Sha256Digest&) = default;
};
enum class DetectionProof : std::uint8_t { exact_known_hash, complete_local_contract };
struct LoadedImageIdentity final {
    std::filesystem::path path;
    Sha256Digest sha256;
    std::uint64_t file_size{};
    std::uint16_t machine{};
    std::uint32_t time_date_stamp{};
    std::uint32_t preferred_image_base{};
    std::uint32_t size_of_image{};
};
enum class IdentityStage : std::uint8_t { module_path, open_file, file_size, pe_headers,
    hash_provider, hash_create, file_read, hash_update, hash_finish, allocation };
struct IdentityError final {
    IdentityStage stage{};
    DWORD win32_error{};
    LONG cng_status{};
};
template <class Build, class Variant>
struct BuildSelection final {
    Build build;
    Variant variant;
    DetectionProof proof;
    LoadedImageIdentity identity;
};
[[nodiscard]] std::expected<LoadedImageIdentity, IdentityError>
ReadLoadedExecutableIdentity(HMODULE module) noexcept;
} // namespace gc::game_version
