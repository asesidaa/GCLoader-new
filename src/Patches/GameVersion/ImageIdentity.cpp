#include "Patches/GameVersion/ImageIdentity.h"

#include <bcrypt.h>
#include <limits>
#include <vector>

namespace gc::game_version {
namespace {
struct IdentityResources final {
    HANDLE file{INVALID_HANDLE_VALUE};
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    ~IdentityResources() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
};
template <typename T>
bool ReadHeader(std::uintptr_t address, T& value) noexcept {
    SIZE_T count{};
    return address <= std::numeric_limits<std::uintptr_t>::max() - sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                          &value, sizeof(T), &count) && count == sizeof(T);
}
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

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS32 nt{};
        if (!ReadHeader(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
            static_cast<std::uintptr_t>(dos.e_lfanew) > std::numeric_limits<std::uintptr_t>::max() - base ||
            !ReadHeader(base + static_cast<std::uintptr_t>(dos.e_lfanew), nt) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            !nt.OptionalHeader.SizeOfImage ||
            nt.OptionalHeader.SizeOfImage > std::numeric_limits<std::uintptr_t>::max() - base ||
            static_cast<std::uint32_t>(dos.e_lfanew) > nt.OptionalHeader.SizeOfImage ||
            sizeof(nt) > nt.OptionalHeader.SizeOfImage - static_cast<std::uint32_t>(dos.e_lfanew))
            return std::unexpected(IdentityError{IdentityStage::pe_headers, ERROR_BAD_EXE_FORMAT});
        identity.machine = nt.FileHeader.Machine;
        identity.time_date_stamp = nt.FileHeader.TimeDateStamp;
        identity.preferred_image_base = nt.OptionalHeader.ImageBase;
        identity.size_of_image = nt.OptionalHeader.SizeOfImage;

        IdentityResources resources;
        resources.file = CreateFileW(identity.path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (resources.file == INVALID_HANDLE_VALUE)
            return std::unexpected(IdentityError{IdentityStage::open_file, GetLastError()});
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(resources.file, &length))
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
            if (!ReadFile(resources.file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
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
