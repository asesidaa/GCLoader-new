// SPDX-License-Identifier: CC0-1.0

#include "AsioLogoTexture.h"

#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>

#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

template <typename Interface>
class ComPtr {
public:
    ~ComPtr() {
        if (value_ != nullptr) {
            value_->Release();
        }
    }
    Interface** put() noexcept {
        return &value_;
    }
    Interface* get() const noexcept {
        return value_;
    }
    Interface* operator->() const noexcept {
        return value_;
    }

private:
    Interface* value_{};
};

std::string HresultFailure(const char* operation, HRESULT result) {
    std::ostringstream text;
    text << operation << " failed with HRESULT 0x"
         << std::hex << std::uppercase
         << static_cast<std::uint32_t>(result);
    return text.str();
}

} // namespace

AsioLogoTexture::~AsioLogoTexture() {
    Reset();
}

std::expected<void, std::string> AsioLogoTexture::Load(
    ID3D11Device* borrowed_device,
    const std::filesystem::path& png_path) noexcept {
    Reset();
    try {
        if (borrowed_device == nullptr) {
            return std::unexpected(
                "ASIO logo requires an open D3D11 device");
        }
        if (png_path.empty()) {
            return std::unexpected("ASIO logo path is empty");
        }

        ComPtr<IWICImagingFactory> factory;
        auto result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.put()));
        if (FAILED(result)) {
            return std::unexpected(HresultFailure(
                "CoCreateInstance(WICImagingFactory)",
                result));
        }

        ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromFilename(
            png_path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            decoder.put());
        if (FAILED(result)) {
            return std::unexpected(HresultFailure(
                "WIC CreateDecoderFromFilename(ASIO logo)",
                result));
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, frame.put());
        if (FAILED(result)) {
            return std::unexpected(HresultFailure(
                "WIC GetFrame(ASIO logo)",
                result));
        }

        ComPtr<IWICFormatConverter> converter;
        result = factory->CreateFormatConverter(converter.put());
        if (FAILED(result)) {
            return std::unexpected(HresultFailure(
                "WIC CreateFormatConverter(ASIO logo)",
                result));
        }
        result = converter->Initialize(
            frame.get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(result)) {
            return std::unexpected(HresultFailure(
                "WIC RGBA conversion(ASIO logo)",
                result));
        }

        UINT width{};
        UINT height{};
        result = converter->GetSize(&width, &height);
        if (FAILED(result) || width == 0 || height == 0) {
            return std::unexpected(FAILED(result)
                ? HresultFailure("WIC GetSize(ASIO logo)", result)
                : "ASIO logo has zero dimensions");
        }
        constexpr std::uint64_t bytes_per_pixel = 4;
        if (width > std::numeric_limits<UINT>::max() / bytes_per_pixel) {
            return std::unexpected("ASIO logo row size overflows");
        }
        const auto stride = static_cast<UINT>(width * bytes_per_pixel);
        const auto byte_count =
            static_cast<std::uint64_t>(stride) * height;
        if (byte_count > std::numeric_limits<UINT>::max() ||
            byte_count > std::numeric_limits<std::size_t>::max()) {
            return std::unexpected("ASIO logo pixel size overflows");
        }
        std::vector<std::uint8_t> rgba(
            static_cast<std::size_t>(byte_count));
        result = converter->CopyPixels(
            nullptr,
            stride,
            static_cast<UINT>(byte_count),
            rgba.data());
        if (FAILED(result)) {
            return std::unexpected(HresultFailure(
                "WIC CopyPixels(ASIO logo)",
                result));
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA pixels{
            rgba.data(),
            stride,
            static_cast<UINT>(byte_count),
        };
        ComPtr<ID3D11Texture2D> texture;
        result = borrowed_device->CreateTexture2D(
            &description,
            &pixels,
            texture.put());
        if (FAILED(result)) {
            return std::unexpected(HresultFailure(
                "D3D11 CreateTexture2D(ASIO logo)",
                result));
        }
        result = borrowed_device->CreateShaderResourceView(
            texture.get(),
            nullptr,
            &view_);
        if (FAILED(result)) {
            Reset();
            return std::unexpected(HresultFailure(
                "D3D11 CreateShaderResourceView(ASIO logo)",
                result));
        }
        width_ = width;
        height_ = height;
        return {};
    } catch (const std::exception& error) {
        Reset();
        return std::unexpected(
            "ASIO logo load failed: " + std::string{error.what()});
    } catch (...) {
        Reset();
        return std::unexpected("ASIO logo load failed unexpectedly");
    }
}

void AsioLogoTexture::Reset() noexcept {
    if (view_ != nullptr) {
        view_->Release();
        view_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

ID3D11ShaderResourceView* AsioLogoTexture::view() const noexcept {
    return view_;
}

std::uint32_t AsioLogoTexture::width() const noexcept {
    return width_;
}

std::uint32_t AsioLogoTexture::height() const noexcept {
    return height_;
}
