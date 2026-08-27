// SPDX-License-Identifier: CC0-1.0

#include "AudioOperationWorker.h"

#include "AudioBackendEditorModel.h"

#include <Windows.h>

#include <exception>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace
{
    gc::audio::AsioFailure WorkerFailure(std::string detail)
    {
        return {
            .stage = gc::audio::AsioFailureStage::process_launch,
            .domain = gc::audio::AsioResultDomain::none,
            .detail = std::move(detail),
        };
    }
} // namespace

AudioOperationWorker::AudioOperationWorker() noexcept
    : write_actions_(
        gc::config::ProductionAtomicConfigWriteActions())
{
    try
    {
        probe_client_ = std::make_unique<gc::audio::AsioProbeClient>();
        panel_client_ =
            std::make_unique<gc::audio::AsioControlPanelClient>();
    }
    catch (...)
    {
        probe_client_.reset();
        panel_client_.reset();
    }
    panel_cancellation_event_ = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        nullptr);
}

AudioOperationWorker::AudioOperationWorker(
    std::unique_ptr<gc::audio::IAsioProbeClient> probe_client,
    std::unique_ptr<gc::audio::IAsioControlPanelClient> panel_client,
    const gc::config::AtomicConfigWriteActions& write_actions) noexcept
    : probe_client_(std::move(probe_client)),
      panel_client_(std::move(panel_client)),
      write_actions_(write_actions)
{
    panel_cancellation_event_ = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        nullptr);
}

AudioOperationWorker::~AudioOperationWorker()
{
    Shutdown();
    if (panel_cancellation_event_ != nullptr)
    {
        CloseHandle(panel_cancellation_event_);
    }
}

bool AudioOperationWorker::ReadyToStart() const noexcept
{
    return operation_ == Operation::idle && !worker_.joinable();
}

std::expected<void, std::string>
AudioOperationWorker::StartInspection(
    const gc::audio::AsioProbeRequest& request) noexcept
{
    if (!ReadyToStart())
    {
        return std::unexpected("An audio operation is already running");
    }
    if (probe_client_ == nullptr)
    {
        return std::unexpected("ASIO probe client is unavailable");
    }
    try
    {
        inspection_result_.reset();
        completed_.store(false, std::memory_order_relaxed);
        operation_ = Operation::inspection;
        worker_ = std::thread([this, request]
        {
            try
            {
                inspection_result_.emplace(probe_client_->Run(
                    request,
                    gc::audio::kDefaultAsioProbeTimeout));
            }
            catch (const std::exception& error)
            {
                inspection_result_.emplace(std::unexpected(WorkerFailure(
                    "Could not run ASIO inspection worker: " +
                    std::string{error.what()})));
            }
            catch (...)
            {
                inspection_result_.emplace(std::unexpected(WorkerFailure(
                    "Could not run ASIO inspection worker")));
            }
            completed_.store(true, std::memory_order_release);
        });
        return {};
    }
    catch (const std::exception& error)
    {
        operation_ = Operation::idle;
        return std::unexpected(
            "Could not start ASIO inspection worker: " +
            std::string{error.what()});
    }
    catch (...)
    {
        operation_ = Operation::idle;
        return std::unexpected(
            "Could not start ASIO inspection worker");
    }
}

std::expected<void, std::string>
AudioOperationWorker::StartControlPanel(
    const gc::audio::AsioControlPanelRequest& request) noexcept
{
    if (!ReadyToStart())
    {
        return std::unexpected("An audio operation is already running");
    }
    if (panel_client_ == nullptr)
    {
        return std::unexpected("ASIO control-panel client is unavailable");
    }
    if (panel_cancellation_event_ == nullptr)
    {
        return std::unexpected(
            "ASIO control-panel cancellation event is unavailable");
    }
    if (!ResetEvent(panel_cancellation_event_))
    {
        return std::unexpected(std::format(
            "Could not reset ASIO control-panel cancellation event: {}",
            GetLastError()));
    }
    try
    {
        panel_result_.reset();
        completed_.store(false, std::memory_order_relaxed);
        operation_ = Operation::control_panel;
        worker_ = std::thread([this, request]
        {
            try
            {
                panel_result_.emplace(panel_client_->Run(
                    request,
                    panel_cancellation_event_));
            }
            catch (const std::exception& error)
            {
                panel_result_.emplace(std::unexpected(WorkerFailure(
                    "Could not run ASIO control-panel worker: " +
                    std::string{error.what()})));
            }
            catch (...)
            {
                panel_result_.emplace(std::unexpected(WorkerFailure(
                    "Could not run ASIO control-panel worker")));
            }
            completed_.store(true, std::memory_order_release);
        });
        return {};
    }
    catch (const std::exception& error)
    {
        operation_ = Operation::idle;
        return std::unexpected(
            "Could not start ASIO control-panel worker: " +
            std::string{error.what()});
    }
    catch (...)
    {
        operation_ = Operation::idle;
        return std::unexpected(
            "Could not start ASIO control-panel worker");
    }
}

std::expected<void, std::string> AudioOperationWorker::StartSave(
    const std::filesystem::path& path,
    const gc::config::ConfigDocument& config) noexcept
{
    if (!ReadyToStart())
    {
        return std::unexpected("An audio operation is already running");
    }
    if (probe_client_ == nullptr)
    {
        return std::unexpected("ASIO probe client is unavailable");
    }
    try
    {
        auto owned_path = path;
        auto owned_config = config;
        save_result_.reset();
        completed_.store(false, std::memory_order_relaxed);
        operation_ = Operation::save;
        worker_ = std::thread(
            [this,
                path = std::move(owned_path),
                config = std::move(owned_config)]
            {
                try
                {
                    save_result_.emplace(ValidateAndWriteConfig(
                        path,
                        config,
                        *probe_client_,
                        write_actions_));
                }
                catch (const std::exception& error)
                {
                    save_result_.emplace(std::unexpected(
                        "Config save worker failed: " +
                        std::string{error.what()}));
                }
                catch (...)
                {
                    save_result_.emplace(std::unexpected(
                        "Config save worker failed unexpectedly"));
                }
                completed_.store(true, std::memory_order_release);
            });
        return {};
    }
    catch (const std::exception& error)
    {
        operation_ = Operation::idle;
        return std::unexpected(
            "Could not start config save worker: " +
            std::string{error.what()});
    }
    catch (...)
    {
        operation_ = Operation::idle;
        return std::unexpected("Could not start config save worker");
    }
}

void AudioOperationWorker::FinishTake() noexcept
{
    worker_.join();
    operation_ = Operation::idle;
    completed_.store(false, std::memory_order_relaxed);
}

std::optional<gc::audio::AsioProbeResult>
AudioOperationWorker::TakeInspection()
{
    if (operation_ != Operation::inspection ||
        !completed_.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }
    FinishTake();
    auto result = std::move(inspection_result_);
    inspection_result_.reset();
    return result;
}

std::optional<std::expected<
    gc::audio::AsioControlPanelCompletion,
    gc::audio::AsioFailure>>
AudioOperationWorker::TakeControlPanel()
{
    if (operation_ != Operation::control_panel ||
        !completed_.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }
    FinishTake();
    auto result = std::move(panel_result_);
    panel_result_.reset();
    return result;
}

std::optional<std::expected<void, std::string>>
AudioOperationWorker::TakeSave()
{
    if (operation_ != Operation::save ||
        !completed_.load(std::memory_order_acquire))
    {
        return std::nullopt;
    }
    FinishTake();
    auto result = std::move(save_result_);
    save_result_.reset();
    return result;
}

AudioOperationWorker::Operation
AudioOperationWorker::operation() const noexcept
{
    return operation_;
}

bool AudioOperationWorker::busy() const noexcept
{
    return operation_ != Operation::idle;
}

void AudioOperationWorker::Shutdown() noexcept
{
    if (operation_ == Operation::control_panel &&
        panel_cancellation_event_ != nullptr)
    {
        SetEvent(panel_cancellation_event_);
    }
    if (worker_.joinable())
    {
        worker_.join();
    }
    operation_ = Operation::idle;
    completed_.store(false, std::memory_order_relaxed);
}
