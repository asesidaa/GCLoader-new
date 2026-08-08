#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"
#include "Audio/Asio/AsioTypes.h"

#include <Windows.h>

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <span>

namespace gc::audio {

class AsioSession final {
public:
    static std::expected<std::unique_ptr<AsioSession>, AsioFailure>
    Prepare(
        AsioDriverRegistration registration,
        std::unique_ptr<IAsioDriver> driver,
        const AsioStreamRequest& request,
        HWND system_reference,
        AsioProbeMode mode,
        bool restore_sample_rate) noexcept;

    ~AsioSession();
    AsioSession(const AsioSession&) = delete;
    AsioSession& operator=(const AsioSession&) = delete;

    [[nodiscard]] std::expected<void, AsioFailure>
    CreateOutputBuffers(ASIOCallbacks* callbacks) noexcept;
    [[nodiscard]] std::expected<void, AsioFailure> Start() noexcept;
    [[nodiscard]] std::expected<void, AsioFailure> Stop() noexcept;
    [[nodiscard]] std::expected<void, AsioFailure> Close() noexcept;

    [[nodiscard]] const AsioCapabilityReport& report() const noexcept;
    [[nodiscard]] std::span<ASIOBufferInfo> buffers() noexcept;
    [[nodiscard]] IAsioDriver& driver() noexcept;

private:
    AsioSession(
        AsioDriverRegistration registration,
        std::unique_ptr<IAsioDriver> driver,
        bool restore_sample_rate) noexcept;

    [[nodiscard]] std::expected<void, AsioFailure> PrepareDriver(
        const AsioStreamRequest& request,
        HWND system_reference,
        AsioProbeMode mode);
    [[nodiscard]] std::expected<void, AsioFailure>
    RequireCreatingThread() const;

    AsioCapabilityReport report_;
    std::unique_ptr<IAsioDriver> driver_;
    std::array<ASIOBufferInfo, 2> buffers_{};
    DWORD creating_thread_id_{};
    bool restore_sample_rate_{};
    bool sample_rate_changed_{};
    bool buffers_created_{};
    bool started_{};
    bool closed_{};
    std::optional<AsioFailure> cleanup_failure_;
};

} // namespace gc::audio
