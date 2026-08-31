// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"

#include <algorithm>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr char kHexDigits[] = "0123456789ABCDEF";

        std::string EscapeBytes(std::span<const char> bytes)
        {
            std::string escaped;
            escaped.reserve(bytes.size() * 4);
            for (const char value : bytes)
            {
                const auto byte = static_cast<unsigned char>(value);
                escaped.push_back('\\');
                escaped.push_back('x');
                escaped.push_back(kHexDigits[byte >> 4U]);
                escaped.push_back(kHexDigits[byte & 0x0FU]);
            }
            return escaped;
        }

        class AsioDriver final : public IAsioDriver
        {
        public:
            AsioDriver() = default;
            ~AsioDriver() override = default;

            void Adopt(IASIO* driver) noexcept
            {
                driver_ = driver;
            }

            ASIOBool Init(HWND system_reference) noexcept override
            {
                return driver_->init(system_reference);
            }

            void GetDriverName(char (&name)[32]) noexcept override
            {
                std::ranges::fill(name, '\0');
                driver_->getDriverName(name);
            }

            long GetDriverVersion() noexcept override
            {
                return driver_->getDriverVersion();
            }

            void GetErrorMessage(char (&message)[124]) noexcept override
            {
                std::ranges::fill(message, '\0');
                driver_->getErrorMessage(message);
            }

            ASIOError Start() noexcept override
            {
                return driver_->start();
            }

            ASIOError Stop() noexcept override
            {
                return driver_->stop();
            }

            ASIOError GetChannels(long* inputs, long* outputs) noexcept override
            {
                return driver_->getChannels(inputs, outputs);
            }

            ASIOError GetBufferSize(
                long* minimum,
                long* maximum,
                long* preferred,
                long* granularity) noexcept override
            {
                return driver_->getBufferSize(
                    minimum,
                    maximum,
                    preferred,
                    granularity);
            }

            ASIOError GetSampleRate(ASIOSampleRate* rate) noexcept override
            {
                return driver_->getSampleRate(rate);
            }

            ASIOError GetChannelInfo(ASIOChannelInfo* info) noexcept override
            {
                if (info != nullptr)
                {
                    std::ranges::fill(info->name, '\0');
                }
                return driver_->getChannelInfo(info);
            }

            ASIOError CreateBuffers(
                ASIOBufferInfo* buffers,
                long channel_count,
                long buffer_frames,
                const ASIOCallbacks* callbacks) noexcept override
            {
                return driver_->createBuffers(
                    buffers,
                    channel_count,
                    buffer_frames,
                    const_cast<ASIOCallbacks*>(callbacks));
            }

            ASIOError DisposeBuffers() noexcept override
            {
                return driver_->disposeBuffers();
            }

            ASIOError ControlPanel() noexcept override
            {
                return driver_->controlPanel();
            }

            ASIOError OutputReady() noexcept override
            {
                return driver_->outputReady();
            }

            ASIOError Exit() noexcept override
            {
                if (driver_ == nullptr)
                {
                    return ASE_NotPresent;
                }
                auto* const driver = std::exchange(driver_, nullptr);
                driver->Release();
                return ASE_OK;
            }

        private:
            IASIO* driver_{};
        };

        HRESULT ProductionCreateInstance(
            void*,
            REFCLSID class_id,
            LPUNKNOWN outer,
            DWORD class_context,
            REFIID interface_id,
            void** output) noexcept
        {
            return CoCreateInstance(
                class_id,
                outer,
                class_context,
                interface_id,
                output);
        }

        AsioFailure ComFailure(HRESULT result, std::string detail)
        {
            return {
                .stage = AsioFailureStage::com,
                .domain = AsioResultDomain::hresult,
                .result = static_cast<std::int64_t>(result),
                .detail = std::move(detail),
            };
        }
    } // namespace

    std::string AsioDisplayTextToUtf8(
        std::span<const char> bytes) noexcept
    {
        try
        {
            if (bytes.empty())
            {
                return {};
            }
            const auto terminator = std::ranges::find(bytes, '\0');
            const std::span<const char> bounded{
                bytes.data(),
                static_cast<std::size_t>(terminator - bytes.begin())
            };
            if (bounded.empty())
            {
                return {};
            }
            if (bounded.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
            {
                return EscapeBytes(bounded);
            }
            const int source_size = static_cast<int>(bounded.size());
            const int wide_size = MultiByteToWideChar(
                CP_ACP,
                MB_ERR_INVALID_CHARS,
                bounded.data(),
                source_size,
                nullptr,
                0);
            if (wide_size <= 0)
            {
                return EscapeBytes(bounded);
            }
            std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
            if (MultiByteToWideChar(
                CP_ACP,
                MB_ERR_INVALID_CHARS,
                bounded.data(),
                source_size,
                wide.data(),
                wide_size) != wide_size)
            {
                return EscapeBytes(bounded);
            }

            const int utf8_size = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                wide.data(),
                wide_size,
                nullptr,
                0,
                nullptr,
                nullptr);
            if (utf8_size <= 0)
            {
                return EscapeBytes(bounded);
            }
            std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
            if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                wide.data(),
                wide_size,
                utf8.data(),
                utf8_size,
                nullptr,
                nullptr) != utf8_size)
            {
                return EscapeBytes(bounded);
            }
            return utf8;
        }
        catch (...)
        {
            try
            {
                return EscapeBytes(bytes);
            }
            catch (...)
            {
                return {};
            }
        }
    }

    AsioComActions ProductionAsioComActions() noexcept
    {
        return {
            .create_instance = &ProductionCreateInstance,
        };
    }

    ProductionAsioDriverFactory::ProductionAsioDriverFactory(
        AsioComActions actions) noexcept
        : actions_(actions)
    {
    }

    std::expected<std::unique_ptr<IAsioDriver>, AsioFailure>
    ProductionAsioDriverFactory::Create(const CLSID& clsid) noexcept
    {
        if (actions_.create_instance == nullptr)
        {
            return std::unexpected(ComFailure(
                E_POINTER,
                "ASIO COM actions are incomplete"));
        }

        std::unique_ptr<AsioDriver> wrapped;
        try
        {
            wrapped = std::make_unique<AsioDriver>();
        }
        catch (...)
        {
            return std::unexpected(ComFailure(
                E_OUTOFMEMORY,
                "Could not allocate the ASIO driver wrapper"));
        }

        void* raw{};
        const HRESULT result = actions_.create_instance(
            actions_.context,
            clsid,
            nullptr,
            CLSCTX_INPROC_SERVER,
            clsid,
            &raw);
        if (FAILED(result))
        {
            return std::unexpected(ComFailure(
                result,
                "CoCreateInstance failed for the registered 32-bit ASIO driver"));
        }
        if (raw == nullptr)
        {
            return std::unexpected(ComFailure(
                E_POINTER,
                "CoCreateInstance returned success with no IASIO interface"));
        }

        wrapped->Adopt(static_cast<IASIO*>(raw));
        return std::unique_ptr<IAsioDriver>{std::move(wrapped)};
    }
} // namespace gc::audio
