#include "Config/Validation/ExperimentalValidation.h"
#include "Config/Validation/CommonValidation.h"
#include "Config/DeclaredEnum.h"
#include "Patches/Framerate/FrameratePolicy.h"
#include <climits>
#include <limits>

namespace gc::config::validation {
namespace {
using TargetFpsValidator = rfl::Validator<
    std::uint32_t,
    rfl::Minimum<framerate::kMinimumTargetFps>,
    rfl::Maximum<framerate::kMaximumTargetFps>>;
using NonZeroWasapiBufferValidator =
rfl::Validator<std::uint32_t, rfl::Minimum<1>>;

struct ValidAsioDriverTextRule
{
    [[maybe_unused]] static rfl::Result<std::string> validate(
        const std::string& value) noexcept
    {
        if (value.size() <= 1024 && IsValidUtf8(value))
        {
            return value;
        }
        return rfl::error(
            "ASIO driver name must be at most 1024 valid UTF-8 bytes");
    }
};

using AsioDriverTextValidator =
rfl::Validator<std::string, ValidAsioDriverTextRule>;


}
ExperimentalValidationResult ValidateExperimental(
    const ConfigDocument& document, bool poll_valid, ValidationContext& context) {
    auto& errors = context.errors;
    const auto target_fps = static_cast<std::uint32_t>(
        document.experimental().target_fps());
    ValidateLeaf<TargetFpsValidator>(
        target_fps,
        ConfigPath{"experimental", "target_fps"},
        ConfigErrorCode::out_of_range,
        "expected an integer from 60 through 500",
        errors);

    const auto widescreen_width = static_cast<std::uint64_t>(
        document.experimental().widescreen_window_width());
    const auto widescreen_height = static_cast<std::uint64_t>(
        document.experimental().widescreen_window_height());
    const auto widescreen_hud_placement =
        document.experimental().widescreen_hud_placement();
    if (!IsDeclaredEnumValue(widescreen_hud_placement))
    {
        errors.push_back({
            .path = ConfigPath{
                "experimental",
                "widescreen_hud_placement",
            },
            .code = ConfigErrorCode::unsupported_value,
            .message =
                "widescreen HUD placement must be left, center, or right",
        });
    }
    const bool widescreen_width_valid =
        widescreen_width >= 720 &&
        widescreen_width <=
            static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    if (!widescreen_width_valid)
    {
        errors.push_back({
            .path = ConfigPath{
                "experimental",
                "widescreen_window_width",
            },
            .code = ConfigErrorCode::out_of_range,
            .message =
                "widescreen width must be from 720 through INT_MAX",
        });
    }
    const bool widescreen_height_valid =
        widescreen_height == 1280;
    if (!widescreen_height_valid)
    {
        errors.push_back({
            .path = ConfigPath{
                "experimental",
                "widescreen_window_height",
            },
            .code = ConfigErrorCode::out_of_range,
            .message = "widescreen height must be exactly 1280",
        });
    }
    if (widescreen_width_valid && widescreen_height_valid &&
        widescreen_width >
            std::numeric_limits<std::uint32_t>::max() /
                widescreen_height)
    {
        errors.push_back({
            .path = ConfigPath{
                "experimental",
                "widescreen_window_width",
            },
            .code = ConfigErrorCode::out_of_range,
            .message = "widescreen pixel-area arithmetic overflows",
            .related_paths = {
                ConfigPath{
                    "experimental",
                    "widescreen_window_height",
                },
            },
        });
    }

    const auto audio_backend =
        document.experimental().audio_backend();
    const bool audio_backend_valid =
        IsDeclaredEnumValue(audio_backend);
    if (!audio_backend_valid)
    {
        errors.push_back({
            .path =
            ConfigPath{"experimental", "audio_backend"},
            .code = ConfigErrorCode::unsupported_value,
            .message = "unsupported audio backend",
        });
    }
    if (audio_backend == audio::AudioBackend::wasapi_exclusive)
    {
        ValidateLeaf<NonZeroWasapiBufferValidator>(
            static_cast<std::uint32_t>(
                document.experimental()
                        .wasapi_exclusive_buffer_ms()),
            ConfigPath{
                "experimental",
                "wasapi_exclusive_buffer_ms",
            },
            ConfigErrorCode::out_of_range,
            "WASAPI buffer duration must be greater than zero",
            errors);
    }
    if (audio_backend == audio::AudioBackend::asio)
    {
        if (document.experimental().asio_driver_name().empty())
        {
            errors.push_back({
                .path = ConfigPath{
                    "experimental",
                    "asio_driver_name",
                },
                .code = ConfigErrorCode::required_value,
                .message = "ASIO requires a driver name",
            });
        }
        else
        {
            ValidateLeaf<AsioDriverTextValidator>(
                document.experimental().asio_driver_name(),
                ConfigPath{
                    "experimental",
                    "asio_driver_name",
                },
                ConfigErrorCode::invalid_encoding,
                "ASIO driver name must be valid bounded UTF-8",
                errors);
        }
        const auto frames = static_cast<std::uint32_t>(
            document.experimental().asio_buffer_frames());
        if (frames == 0 ||
            frames > static_cast<std::uint32_t>(LONG_MAX))
        {
            errors.push_back({
                .path = ConfigPath{
                    "experimental",
                    "asio_buffer_frames",
                },
                .code = ConfigErrorCode::out_of_range,
                .message = "ASIO buffer frames are out of range",
            });
        }
        const auto channel = static_cast<std::uint32_t>(
            document.experimental().asio_output_base_channel());
        if (channel >
            static_cast<std::uint32_t>(LONG_MAX - 1))
        {
            errors.push_back({
                .path = ConfigPath{
                    "experimental",
                    "asio_output_base_channel",
                },
                .code = ConfigErrorCode::out_of_range,
                .message = "ASIO output channel is out of range",
            });
        }
    }
    if (document.experimental().enable_absolute_time_judgement())
    {
        if (audio_backend_valid &&
            audio_backend !=
            audio::AudioBackend::wasapi_exclusive &&
            audio_backend != audio::AudioBackend::asio)
        {
            errors.push_back({
                .path =
                ConfigPath{"experimental", "audio_backend"},
                .code = ConfigErrorCode::unmet_dependency,
                .message =
                "absolute judgement requires WASAPI or ASIO",
                .related_paths = {
                    ConfigPath{
                        "experimental",
                        "enable_absolute_time_judgement",
                    },
                },
            });
        }
        if (poll_valid && document.input_poll_hz() != 1000)
        {
            errors.push_back({
                .path = ConfigPath{"input_poll_hz"},
                .code = ConfigErrorCode::unmet_dependency,
                .message =
                "absolute judgement requires 1000 Hz input",
                .related_paths = {
                    ConfigPath{
                        "experimental",
                        "enable_absolute_time_judgement",
                    },
                },
            });
        }
    }


    return {target_fps, widescreen_width, widescreen_height, widescreen_hud_placement, audio_backend};
}
}
