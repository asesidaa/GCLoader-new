#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::audio
{
    enum class AudioBackend : std::uint8_t
    {
        directsound,
        wasapi_exclusive,
        asio,
    };

    [[nodiscard]] constexpr std::string_view AudioBackendName(
        AudioBackend backend) noexcept
    {
        switch (backend)
        {
        case AudioBackend::directsound:
            return "directsound";
        case AudioBackend::wasapi_exclusive:
            return "wasapi_exclusive";
        case AudioBackend::asio:
            return "asio";
        }
        return "unknown";
    }

    struct DirectSoundSettings final
    {
    };

    class WasapiExclusiveSettings final
    {
    public:
        [[nodiscard]] std::uint32_t buffer_ms() const noexcept
        {
            return buffer_ms_;
        }

    private:
        explicit WasapiExclusiveSettings(std::uint32_t buffer_ms) noexcept
            : buffer_ms_(buffer_ms)
        {
        }

        friend class gc::config::ConfigCompiler;
        std::uint32_t buffer_ms_{};
    };

    class AsioSettings final
    {
    public:
        [[nodiscard]] const std::string& driver_name() const noexcept
        {
            return driver_name_;
        }

        [[nodiscard]] std::uint32_t buffer_frames() const noexcept
        {
            return buffer_frames_;
        }

        [[nodiscard]] std::uint32_t output_base_channel() const noexcept
        {
            return output_base_channel_;
        }

    private:
        AsioSettings(
            std::string driver_name,
            std::uint32_t buffer_frames,
            std::uint32_t output_base_channel)
            : driver_name_(std::move(driver_name)),
              buffer_frames_(buffer_frames),
              output_base_channel_(output_base_channel)
        {
        }

        friend class gc::config::ConfigCompiler;
        std::string driver_name_;
        std::uint32_t buffer_frames_{};
        std::uint32_t output_base_channel_{};
    };

    using AudioBackendSettings = std::variant<
        DirectSoundSettings,
        WasapiExclusiveSettings,
        AsioSettings>;

    class AudioSettings final
    {
    public:
        [[nodiscard]] AudioBackend backend() const noexcept
        {
            return backend_;
        }

        [[nodiscard]] const AudioBackendSettings& selection() const noexcept
        {
            return selection_;
        }

        [[nodiscard]] bool exact_clock_required() const noexcept
        {
            return exact_clock_required_;
        }

    private:
        AudioSettings(
            AudioBackend backend,
            AudioBackendSettings selection,
            bool exact_clock_required)
            : backend_(backend),
              selection_(std::move(selection)),
              exact_clock_required_(exact_clock_required)
        {
        }

        friend class gc::config::ConfigCompiler;
        AudioBackend backend_{};
        AudioBackendSettings selection_;
        bool exact_clock_required_{};
    };
} // namespace gc::audio
