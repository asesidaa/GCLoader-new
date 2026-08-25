#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/AudioSettings.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace gc::config
{
    using AudioBackend = audio::AudioBackend;

    [[nodiscard]] const char* AudioBackendName(AudioBackend backend) noexcept;

    [[nodiscard]] std::expected<void, std::string>
    ValidateAudioBackendSettings(
        AudioBackend backend,
        std::string_view asio_driver_name,
        std::uint32_t asio_buffer_frames,
        std::uint32_t asio_output_base_channel) noexcept;
} // namespace gc::config
