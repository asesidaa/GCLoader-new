// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriverCatalog.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace gc::audio {

namespace {

AsioFailure RegistryFailure(
    std::string detail,
    LSTATUS result = ERROR_SUCCESS) {
    return {
        .stage = AsioFailureStage::registry,
        .domain = result == ERROR_SUCCESS
            ? AsioResultDomain::none
            : AsioResultDomain::win32,
        .result = result,
        .detail = std::move(detail),
    };
}

class RegistryKey final {
public:
    RegistryKey() = default;
    explicit RegistryKey(HKEY value) noexcept : value_(value) {}
    ~RegistryKey() {
        if (value_ != nullptr) {
            RegCloseKey(value_);
        }
    }

    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    [[nodiscard]] HKEY get() const noexcept {
        return value_;
    }

private:
    HKEY value_{};
};

std::expected<std::wstring, AsioFailure> ReadClsidText(HKEY key) {
    DWORD type{};
    DWORD bytes{};
    LSTATUS status = RegQueryValueExW(
        key,
        L"CLSID",
        nullptr,
        &type,
        nullptr,
        &bytes);
    if (status != ERROR_SUCCESS) {
        return std::unexpected(RegistryFailure(
            "Could not query ASIO registration CLSID size",
            status));
    }
    if (type != REG_SZ || bytes == 0 ||
        bytes % sizeof(wchar_t) != 0) {
        return std::unexpected(RegistryFailure(
            "ASIO registration CLSID must be a nonempty REG_SZ"));
    }

    std::vector<wchar_t> buffer(
        bytes / sizeof(wchar_t) + 1,
        L'\0');
    DWORD actual_bytes = bytes;
    status = RegQueryValueExW(
        key,
        L"CLSID",
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(buffer.data()),
        &actual_bytes);
    if (status != ERROR_SUCCESS) {
        return std::unexpected(RegistryFailure(
            "Could not read ASIO registration CLSID",
            status));
    }
    if (type != REG_SZ || actual_bytes % sizeof(wchar_t) != 0) {
        return std::unexpected(RegistryFailure(
            "ASIO registration CLSID changed type while reading"));
    }

    const auto end = std::ranges::find(buffer, L'\0');
    if (end == buffer.begin()) {
        return std::unexpected(RegistryFailure(
            "ASIO registration CLSID is empty"));
    }
    return std::wstring(buffer.begin(), end);
}

std::expected<std::vector<AsioRegistryValue>, AsioFailure>
ReadProductionRegistry(
    void*,
    HKEY root,
    std::wstring_view path,
    REGSAM access) noexcept {
    try {
        const std::wstring path_text{path};
        HKEY raw_root{};
        const LSTATUS open_status = RegOpenKeyExW(
            root,
            path_text.c_str(),
            0,
            access,
            &raw_root);
        if (open_status == ERROR_FILE_NOT_FOUND ||
            open_status == ERROR_PATH_NOT_FOUND) {
            return std::vector<AsioRegistryValue>{};
        }
        if (open_status != ERROR_SUCCESS) {
            return std::unexpected(RegistryFailure(
                "Could not open the 32-bit HKLM ASIO registry key",
                open_status));
        }
        RegistryKey asio_root{raw_root};

        DWORD subkey_count{};
        DWORD maximum_name_length{};
        const LSTATUS query_status = RegQueryInfoKeyW(
            asio_root.get(),
            nullptr,
            nullptr,
            nullptr,
            &subkey_count,
            &maximum_name_length,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (query_status != ERROR_SUCCESS) {
            return std::unexpected(RegistryFailure(
                "Could not inspect the 32-bit ASIO registry key",
                query_status));
        }

        std::vector<AsioRegistryValue> values;
        values.reserve(subkey_count);
        std::vector<wchar_t> name(
            static_cast<std::size_t>(maximum_name_length) + 1,
            L'\0');
        for (DWORD index = 0; index < subkey_count; ++index) {
            DWORD name_length = static_cast<DWORD>(name.size());
            const LSTATUS enumerate_status = RegEnumKeyExW(
                asio_root.get(),
                index,
                name.data(),
                &name_length,
                nullptr,
                nullptr,
                nullptr,
                nullptr);
            if (enumerate_status == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (enumerate_status != ERROR_SUCCESS) {
                return std::unexpected(RegistryFailure(
                    "Could not enumerate a 32-bit ASIO registration",
                    enumerate_status));
            }
            const std::wstring subkey_name(
                name.data(),
                static_cast<std::size_t>(name_length));

            HKEY raw_registration{};
            const LSTATUS child_status = RegOpenKeyExW(
                asio_root.get(),
                subkey_name.c_str(),
                0,
                access,
                &raw_registration);
            if (child_status != ERROR_SUCCESS) {
                return std::unexpected(RegistryFailure(
                    "Could not open a 32-bit ASIO registration",
                    child_status));
            }
            RegistryKey registration{raw_registration};
            auto clsid = ReadClsidText(registration.get());
            if (!clsid) {
                return std::unexpected(std::move(clsid.error()));
            }
            values.push_back({
                .subkey_name = subkey_name,
                .clsid_text = std::move(*clsid),
            });
        }
        return values;
    } catch (const std::exception& error) {
        return std::unexpected(RegistryFailure(
            "ASIO registry enumeration failed: " +
            std::string{error.what()}));
    } catch (...) {
        return std::unexpected(RegistryFailure(
            "ASIO registry enumeration failed unexpectedly"));
    }
}

std::expected<std::string, AsioFailure> WideToUtf8(
    std::wstring_view value) {
    if (value.empty() ||
        value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return std::unexpected(RegistryFailure(
            "ASIO registry name must be nonempty valid Unicode"));
    }
    const int input_size = static_cast<int>(value.size());
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        input_size,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return std::unexpected(RegistryFailure(
            "ASIO registry name is not valid UTF-16",
            GetLastError()));
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            input_size,
            result.data(),
            required,
            nullptr,
            nullptr) != required) {
        return std::unexpected(RegistryFailure(
            "ASIO registry name could not be encoded as UTF-8",
            GetLastError()));
    }
    return result;
}

std::expected<std::wstring, AsioFailure> Utf8ToWide(
    std::string_view value) {
    if (value.empty() ||
        value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return std::unexpected(RegistryFailure(
            "Configured ASIO registry name must be nonempty valid UTF-8"));
    }
    const int input_size = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        input_size,
        nullptr,
        0);
    if (required <= 0) {
        return std::unexpected(RegistryFailure(
            "Configured ASIO registry name is not valid UTF-8",
            GetLastError()));
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            input_size,
            result.data(),
            required) != required) {
        return std::unexpected(RegistryFailure(
            "Configured ASIO registry name could not be decoded",
            GetLastError()));
    }
    return result;
}

int CompareOrdinal(
    std::wstring_view left,
    std::wstring_view right,
    BOOL ignore_case) noexcept {
    return CompareStringOrdinal(
        left.data(),
        static_cast<int>(left.size()),
        right.data(),
        static_cast<int>(right.size()),
        ignore_case);
}

struct CatalogEntry {
    AsioDriverRegistration registration;
    std::wstring wide_name;
};

} // namespace

std::expected<std::vector<AsioDriverRegistration>, AsioFailure>
EnumerateAsioDrivers(IAsioRegistrySource& source) noexcept {
    try {
        auto raw_values = source.Read32BitRegistrations();
        if (!raw_values) {
            return std::unexpected(std::move(raw_values.error()));
        }

        std::vector<CatalogEntry> entries;
        entries.reserve(raw_values->size());
        for (auto& raw : *raw_values) {
            auto utf8_name = WideToUtf8(raw.subkey_name);
            if (!utf8_name) {
                return std::unexpected(std::move(utf8_name.error()));
            }
            CLSID clsid{};
            const HRESULT clsid_result = CLSIDFromString(
                raw.clsid_text.c_str(),
                &clsid);
            if (FAILED(clsid_result)) {
                return std::unexpected(AsioFailure{
                    .stage = AsioFailureStage::clsid,
                    .domain = AsioResultDomain::hresult,
                    .result = static_cast<std::int64_t>(clsid_result),
                    .detail = "ASIO registration has an invalid CLSID",
                });
            }
            entries.push_back({
                .registration = {
                    .registry_name = std::move(*utf8_name),
                    .clsid = clsid,
                },
                .wide_name = std::move(raw.subkey_name),
            });
        }

        std::ranges::sort(entries, [](const auto& left,
                                     const auto& right) {
            const int folded = CompareOrdinal(
                left.wide_name,
                right.wide_name,
                TRUE);
            if (folded != CSTR_EQUAL) {
                return folded == CSTR_LESS_THAN;
            }
            const int exact = CompareOrdinal(
                left.wide_name,
                right.wide_name,
                FALSE);
            return exact == CSTR_LESS_THAN;
        });

        for (std::size_t index = 1; index < entries.size(); ++index) {
            if (CompareOrdinal(
                    entries[index - 1].wide_name,
                    entries[index].wide_name,
                    TRUE) == CSTR_EQUAL) {
                return std::unexpected(RegistryFailure(
                    "32-bit ASIO registry contains duplicate "
                    "case-insensitive driver names"));
            }
        }

        std::vector<AsioDriverRegistration> registrations;
        registrations.reserve(entries.size());
        for (auto& entry : entries) {
            registrations.push_back(std::move(entry.registration));
        }
        return registrations;
    } catch (const std::exception& error) {
        return std::unexpected(RegistryFailure(
            "ASIO catalog construction failed: " +
            std::string{error.what()}));
    } catch (...) {
        return std::unexpected(RegistryFailure(
            "ASIO catalog construction failed unexpectedly"));
    }
}

std::expected<AsioDriverRegistration, AsioFailure>
ResolveAsioDriver(
    IAsioRegistrySource& source,
    std::string_view utf8_name) noexcept {
    try {
        auto requested = Utf8ToWide(utf8_name);
        if (!requested) {
            return std::unexpected(std::move(requested.error()));
        }
        auto registrations = EnumerateAsioDrivers(source);
        if (!registrations) {
            return std::unexpected(std::move(registrations.error()));
        }
        for (const auto& registration : *registrations) {
            auto registered_name = Utf8ToWide(
                registration.registry_name);
            if (!registered_name) {
                return std::unexpected(
                    std::move(registered_name.error()));
            }
            if (CompareOrdinal(*registered_name, *requested, TRUE) ==
                CSTR_EQUAL) {
                return registration;
            }
        }
        return std::unexpected(RegistryFailure(
            "Configured ASIO driver is not registered in the 32-bit view: " +
            std::string{utf8_name}));
    } catch (const std::exception& error) {
        return std::unexpected(RegistryFailure(
            "ASIO driver lookup failed: " +
            std::string{error.what()}));
    } catch (...) {
        return std::unexpected(RegistryFailure(
            "ASIO driver lookup failed unexpectedly"));
    }
}

AsioRegistryActions ProductionAsioRegistryActions() noexcept {
    return {
        .read = &ReadProductionRegistry,
    };
}

ProductionAsioRegistrySource::ProductionAsioRegistrySource(
    AsioRegistryActions actions) noexcept
    : actions_(actions) {}

std::expected<std::vector<AsioRegistryValue>, AsioFailure>
ProductionAsioRegistrySource::Read32BitRegistrations() noexcept {
    if (actions_.read == nullptr) {
        return std::unexpected(RegistryFailure(
            "ASIO registry actions are incomplete"));
    }
    return actions_.read(
        actions_.context,
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\ASIO",
        KEY_READ | KEY_WOW64_32KEY);
}

} // namespace gc::audio
