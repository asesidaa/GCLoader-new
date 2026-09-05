#include "Patches/GameVersion/ImageIdentity.h"
#include "Platform/Win32/UniqueHandle.h"

#include <bcrypt.h>
#include "Patches/RuntimeImage/RuntimeImage.h"
#include <vector>

namespace gc::game_version {
namespace {
struct IdentityResources final {
    platform::win32::UniqueHandle file;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    ~IdentityResources() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

}

std::expected<LoadedImageIdentity, IdentityError>
ReadLoadedExecutableIdentity(HMODULE module) noexcept {
    try {
        if (!module) return std::unexpected(IdentityError{IdentityStage::module_path, ERROR_INVALID_HANDLE});
        LoadedImageIdentity identity;
        std::vector<wchar_t> path(256);
        for (;;) {
            SetLastError(ERROR_SUCCESS);
            const auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (!count) return std::unexpected(IdentityError{IdentityStage::module_path, GetLastError()});
            if (count < path.size()) {
                identity.path = std::filesystem::path{path.data(), path.data() + count};
                break;
            }
            if (path.size() >= 32768)
                return std::unexpected(IdentityError{IdentityStage::module_path, ERROR_FILENAME_EXCED_RANGE});
            path.resize(path.size() * 2);
        }

        const auto headers = runtime_image::ReadLoadedImageHeaders(module);
        if (!headers)
            return std::unexpected(IdentityError{IdentityStage::pe_headers, ERROR_BAD_EXE_FORMAT});
        identity.machine = headers->machine;
        identity.time_date_stamp = headers->time_date_stamp;
        identity.preferred_image_base = headers->preferred_image_base;
        identity.size_of_image = headers->size_of_image;

        IdentityResources resources;
        resources.file.reset(CreateFileW(identity.path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!resources.file)
            return std::unexpected(IdentityError{IdentityStage::open_file, GetLastError()});
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(resources.file.get(), &length))
            return std::unexpected(IdentityError{IdentityStage::file_size, GetLastError()});
        if (length.QuadPart <= 0)
            return std::unexpected(IdentityError{IdentityStage::file_size, ERROR_BAD_EXE_FORMAT});
        identity.file_size = static_cast<std::uint64_t>(length.QuadPart);

        auto status = BCryptOpenAlgorithmProvider(&resources.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0)
            return std::unexpected(IdentityError{IdentityStage::hash_provider, 0, status});
        // CNG owns its hash-object buffer until BCryptDestroyHash.
        status = BCryptCreateHash(resources.algorithm, &resources.hash, nullptr, 0, nullptr, 0, 0);
        if (status < 0)
            return std::unexpected(IdentityError{IdentityStage::hash_create, 0, status});
        std::array<UCHAR, 16384> buffer{};
        std::uint64_t total{};
        for (;;) {
            DWORD count{};
            if (!ReadFile(resources.file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
                return std::unexpected(IdentityError{IdentityStage::file_read, GetLastError()});
            if (!count) break;
            total += count;
            if (total > identity.file_size)
                return std::unexpected(IdentityError{IdentityStage::file_read, ERROR_FILE_INVALID});
            status = BCryptHashData(resources.hash, buffer.data(), count, 0);
            if (status < 0)
                return std::unexpected(IdentityError{IdentityStage::hash_update, 0, status});
        }
        if (total != identity.file_size)
            return std::unexpected(IdentityError{IdentityStage::file_read, ERROR_FILE_INVALID});
        status = BCryptFinishHash(resources.hash,
            reinterpret_cast<PUCHAR>(identity.sha256.bytes.data()),
            static_cast<ULONG>(identity.sha256.bytes.size()), 0);
        if (status < 0)
            return std::unexpected(IdentityError{IdentityStage::hash_finish, 0, status});
        return identity;
    } catch (...) {
        return std::unexpected(IdentityError{IdentityStage::allocation, ERROR_NOT_ENOUGH_MEMORY});
    }
}
} // namespace gc::game_version
