#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace gc::audio {

struct AsioRegistryValue {
    std::wstring subkey_name;
    std::wstring clsid_text;
};

class IAsioRegistrySource {
public:
    virtual ~IAsioRegistrySource() = default;

    virtual std::expected<std::vector<AsioRegistryValue>, AsioFailure>
    Read32BitRegistrations() noexcept = 0;
};

[[nodiscard]] std::expected<
    std::vector<AsioDriverRegistration>,
    AsioFailure>
EnumerateAsioDrivers(IAsioRegistrySource& source) noexcept;

[[nodiscard]] std::expected<AsioDriverRegistration, AsioFailure>
ResolveAsioDriver(
    IAsioRegistrySource& source,
    std::string_view utf8_name) noexcept;

struct AsioRegistryActions {
    void* context{};
    std::expected<std::vector<AsioRegistryValue>, AsioFailure> (*read)(
        void* context,
        HKEY root,
        std::wstring_view path,
        REGSAM access) noexcept{};
};

[[nodiscard]] AsioRegistryActions
ProductionAsioRegistryActions() noexcept;

class ProductionAsioRegistrySource final : public IAsioRegistrySource {
public:
    explicit ProductionAsioRegistrySource(
        AsioRegistryActions actions =
            ProductionAsioRegistryActions()) noexcept;

    std::expected<std::vector<AsioRegistryValue>, AsioFailure>
    Read32BitRegistrations() noexcept override;

private:
    AsioRegistryActions actions_;
};

} // namespace gc::audio
