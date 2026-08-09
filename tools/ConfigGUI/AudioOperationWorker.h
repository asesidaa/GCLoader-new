#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanelClient.h"
#include "Audio/Asio/AsioProbeClient.h"
#include "Config/ConfigDocument.h"
#include "Config/config.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

class AudioOperationWorker final {
public:
    enum class Operation : std::uint8_t {
        idle,
        inspection,
        control_panel,
        save,
    };

    AudioOperationWorker() noexcept;
    AudioOperationWorker(
        std::unique_ptr<gc::audio::IAsioProbeClient> probe_client,
        std::unique_ptr<gc::audio::IAsioControlPanelClient> panel_client,
        gc::config::AtomicConfigWriteActions write_actions) noexcept;
    ~AudioOperationWorker();

    AudioOperationWorker(const AudioOperationWorker&) = delete;
    AudioOperationWorker& operator=(const AudioOperationWorker&) = delete;

    [[nodiscard]] std::expected<void, std::string> StartInspection(
        const gc::audio::AsioProbeRequest& request) noexcept;
    [[nodiscard]] std::expected<void, std::string> StartControlPanel(
        const gc::audio::AsioControlPanelRequest& request) noexcept;
    [[nodiscard]] std::expected<void, std::string> StartSave(
        const std::filesystem::path& path,
        const InputConfig& config) noexcept;

    [[nodiscard]] std::optional<gc::audio::AsioProbeResult>
        TakeInspection();
    [[nodiscard]] std::optional<std::expected<
        gc::audio::AsioControlPanelCompletion,
        gc::audio::AsioFailure>>
        TakeControlPanel();
    [[nodiscard]] std::optional<std::expected<void, std::string>>
        TakeSave();

    [[nodiscard]] Operation operation() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    void Shutdown() noexcept;

private:
    [[nodiscard]] bool ReadyToStart() const noexcept;
    void FinishTake() noexcept;

    Operation operation_{Operation::idle};
    std::thread worker_;
    std::atomic<bool> completed_{false};
    HANDLE panel_cancellation_event_{};
    std::unique_ptr<gc::audio::IAsioProbeClient> probe_client_;
    std::unique_ptr<gc::audio::IAsioControlPanelClient> panel_client_;
    gc::config::AtomicConfigWriteActions write_actions_{};
    std::optional<gc::audio::AsioProbeResult> inspection_result_;
    std::optional<std::expected<
        gc::audio::AsioControlPanelCompletion,
        gc::audio::AsioFailure>> panel_result_;
    std::optional<std::expected<void, std::string>> save_result_;
};
