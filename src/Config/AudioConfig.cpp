// SPDX-License-Identifier: CC0-1.0

#include "Config/AudioConfig.h"

#include <climits>
#include <cstddef>

namespace gc::config {

namespace {

bool IsContinuationByte(unsigned char value) noexcept {
    return value >= 0x80U && value <= 0xBFU;
}

bool IsValidUtf8(std::string_view value) noexcept {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1 >= value.size() ||
                !IsContinuationByte(
                    static_cast<unsigned char>(value[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }

        if (first >= 0xE0U && first <= 0xEFU) {
            if (index + 2 >= value.size()) {
                return false;
            }
            const auto second =
                static_cast<unsigned char>(value[index + 1]);
            const auto third =
                static_cast<unsigned char>(value[index + 2]);
            const bool valid_second =
                first == 0xE0U
                    ? second >= 0xA0U && second <= 0xBFU
                    : first == 0xEDU
                        ? second >= 0x80U && second <= 0x9FU
                        : IsContinuationByte(second);
            if (!valid_second || !IsContinuationByte(third)) {
                return false;
            }
            index += 3;
            continue;
        }

        if (first >= 0xF0U && first <= 0xF4U) {
            if (index + 3 >= value.size()) {
                return false;
            }
            const auto second =
                static_cast<unsigned char>(value[index + 1]);
            const auto third =
                static_cast<unsigned char>(value[index + 2]);
            const auto fourth =
                static_cast<unsigned char>(value[index + 3]);
            const bool valid_second =
                first == 0xF0U
                    ? second >= 0x90U && second <= 0xBFU
                    : first == 0xF4U
                        ? second >= 0x80U && second <= 0x8FU
                        : IsContinuationByte(second);
            if (!valid_second || !IsContinuationByte(third) ||
                !IsContinuationByte(fourth)) {
                return false;
            }
            index += 4;
            continue;
        }

        return false;
    }
    return true;
}

} // namespace

const char* AudioBackendName(AudioBackend backend) noexcept {
    switch (backend) {
    case AudioBackend::directsound:
        return "directsound";
    case AudioBackend::wasapi_exclusive:
        return "wasapi_exclusive";
    case AudioBackend::asio:
        return "asio";
    }
    return "unknown";
}

std::expected<void, std::string> ValidateAudioBackendSettings(
    AudioBackend backend,
    std::string_view asio_driver_name,
    std::uint32_t asio_buffer_frames,
    std::uint32_t asio_output_base_channel) noexcept {
    try {
        if (backend == AudioBackend::directsound ||
            backend == AudioBackend::wasapi_exclusive) {
            return {};
        }
        if (backend != AudioBackend::asio) {
            return std::unexpected(
                "Invalid [experimental].audio_backend");
        }
        if (asio_driver_name.empty()) {
            return std::unexpected(
                "Invalid [experimental].asio_driver_name; "
                "ASIO requires a nonempty driver name");
        }
        if (asio_driver_name.size() > 1024) {
            return std::unexpected(
                "Invalid [experimental].asio_driver_name; "
                "expected at most 1024 UTF-8 bytes");
        }
        if (!IsValidUtf8(asio_driver_name)) {
            return std::unexpected(
                "Invalid [experimental].asio_driver_name; "
                "expected valid UTF-8");
        }
        if (asio_buffer_frames == 0 ||
            asio_buffer_frames > static_cast<std::uint32_t>(LONG_MAX)) {
            return std::unexpected(
                "Invalid [experimental].asio_buffer_frames; "
                "expected an integer from 1 through LONG_MAX");
        }
        if (asio_output_base_channel >
            static_cast<std::uint32_t>(LONG_MAX - 1)) {
            return std::unexpected(
                "Invalid [experimental].asio_output_base_channel; "
                "expected an integer from 0 through LONG_MAX - 1");
        }
        return {};
    } catch (...) {
        return std::unexpected(
            "Audio backend configuration validation failed unexpectedly");
    }
}

} // namespace gc::config
