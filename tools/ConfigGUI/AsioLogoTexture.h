#pragma once
// SPDX-License-Identifier: CC0-1.0

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

class AsioLogoTexture final {
public:
    AsioLogoTexture() = default;
    ~AsioLogoTexture();
    AsioLogoTexture(const AsioLogoTexture&) = delete;
    AsioLogoTexture& operator=(const AsioLogoTexture&) = delete;

    [[nodiscard]] std::expected<void, std::string> Load(
        ID3D11Device* borrowed_device,
        const std::filesystem::path& png_path) noexcept;
    void Reset() noexcept;

    [[nodiscard]] ID3D11ShaderResourceView* view() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;

private:
    ID3D11ShaderResourceView* view_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
};
