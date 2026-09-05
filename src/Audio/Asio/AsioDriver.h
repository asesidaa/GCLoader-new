#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <expected>
#include <memory>
#include <span>
#include <string>

namespace gc::audio
{
    [[nodiscard]] std::string AsioDisplayTextToUtf8(
        std::span<const char> bytes) noexcept;

    class IAsioDriver
    {
    public:
        virtual ~IAsioDriver() = default;

        virtual ASIOBool Init(HWND system_reference) noexcept = 0;
        virtual void GetDriverName(char (&name)[32]) noexcept = 0;
        virtual long GetDriverVersion() noexcept = 0;
        virtual void GetErrorMessage(char (&message)[124]) noexcept = 0;
        virtual ASIOError Start() noexcept = 0;
        virtual ASIOError Stop() noexcept = 0;
        virtual ASIOError GetChannels(long*, long*) noexcept = 0;
        virtual ASIOError GetBufferSize(
            long*, long*, long*, long*) noexcept = 0;
        virtual ASIOError GetSampleRate(ASIOSampleRate*) noexcept = 0;
        virtual ASIOError GetChannelInfo(ASIOChannelInfo*) noexcept = 0;
        virtual ASIOError CreateBuffers(
            ASIOBufferInfo*, long, long, const ASIOCallbacks*) noexcept = 0;
        virtual ASIOError DisposeBuffers() noexcept = 0;
        virtual ASIOError ControlPanel() noexcept = 0;
        virtual ASIOError OutputReady() noexcept = 0;
        virtual ASIOError Exit() noexcept = 0;
    };

    class IAsioDriverFactory
    {
    public:
        virtual ~IAsioDriverFactory() = default;

        virtual std::expected<std::unique_ptr<IAsioDriver>, AsioFailure>
        Create(const CLSID& clsid) noexcept = 0;
    };

    class ProductionAsioDriverFactory final : public IAsioDriverFactory
    {
    public:
        std::expected<std::unique_ptr<IAsioDriver>, AsioFailure>
        Create(const CLSID& clsid) noexcept override;

    };
} // namespace gc::audio
