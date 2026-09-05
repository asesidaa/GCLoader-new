#include "Input/Polling/InputPollingRuntime.h"
#include "Platform/Win32/Utf.h"

#include "Input/Polling/ForegroundPolicy.h"
#include "Input/Polling/GameplayTransitionJournal.h"
#include "Input/Polling/InputMapper.h"
#include "Input/Polling/InputWorkerWait.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/ControllerBindingEvaluator.h"
#include "Input/Win32/ControllerCatalog.h"
#include "Input/Win32/PhysicalKeyWin32.h"
#include "Input/Win32/RawInputRegistrationGuard.h"
#include "Input/Win32/RawHidController.h"
#include "Input/Win32/RawInputPacket.h"
#include "Input/Win32/Win32InputWindow.h"
#include "Input/Win32/XInputApi.h"
#include "Input/Win32/XInputController.h"
#include "Logging/SessionLog.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"

#include <Windows.h>
#include <timeapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdlib>
#include <expected>
#include <format>
// ReSharper disable once CppUnusedIncludeDirective
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "plog/Log.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace gc::input
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        std::atomic<std::uint32_t> g_published_input{0};

        [[noreturn]] void FatalInputPublicationQpc(
            const BOOL qpc_result,
            const std::int64_t qpc_ticks) noexcept
        {
            using gc::absolute_judgement::AbsoluteJudgementFatalPredicate;
            using gc::absolute_judgement::AbsoluteJudgementFatalPredicateName;
            constexpr auto predicate =
                AbsoluteJudgementFatalPredicate::QueryPerformanceCounterFailed;
            std::array < char, 768 > log{};
            const auto formatted = std::format_to_n(
                log.data(),
                log.size() - 1,
                "AbsoluteJudgement: input-worker-fatal predicate_id={} predicate={}"
                " expression=QueryPerformanceCounter_returned_FALSE_or_nonpositive"
                " api_result={} qpc_ticks={}",
                static_cast<unsigned>(predicate),
                AbsoluteJudgementFatalPredicateName(predicate),
                qpc_result,
                qpc_ticks);
            const auto size = (std::min)(
                static_cast<std::size_t>(formatted.size), log.size() - 1);
            log[size] = '\0';
            PLOG_FATAL << std::string_view(log.data(), size);
            gc::session_log::FlushActiveProcessLog();
            MessageBoxW(
                nullptr,
                L"GCLoader stopped because QueryPerformanceCounter failed in the "
                L"1000 Hz input publisher. Keep loader-log.txt for the exact record.",
                L"GCLoader absolute-time input fatal error",
                MB_OK | MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
            SetLastError(ERROR_SUCCESS);
            const auto terminated = TerminateProcess(GetCurrentProcess(), 0xA7);
            const auto last_error = GetLastError();
            PLOG_FATAL << std::format(
                "AbsoluteJudgement: input-worker-fatal predicate_id={} predicate={}"
                " return_value={} last_error={}",
                static_cast<unsigned>(
                    AbsoluteJudgementFatalPredicate::TerminateProcessReturned),
                AbsoluteJudgementFatalPredicateName(
                    AbsoluteJudgementFatalPredicate::TerminateProcessReturned),
                terminated,
                last_error);
            gc::session_log::FlushActiveProcessLog();
            RaiseFailFastException(nullptr, nullptr, 0);
            std::abort();
        }

        struct RuntimeState
        {
            std::mutex lifecycle_mutex;
            std::condition_variable startup_condition;
            std::thread worker;
            std::optional<InputSettings> settings;
            HANDLE stop_event{};
            std::uint32_t open_count{};
            bool starting{};
            bool startup_ready{};
            bool startup_success{};
            std::string startup_error;
        };

        RuntimeState& Runtime()
        {
            // The loader controls final shutdown. Avoid static-destruction thread joins.
            static RuntimeState* state = new RuntimeState();
            return *state;
        }

        std::string Win32Failure(const char* operation)
        {
            return std::format(
                "{} failed with Win32 error {}", operation, GetLastError());
        }

        std::string WideToUtf8(std::wstring_view value)
        {
            return gc::platform::win32::WideToUtf8(value).value_or(std::string{});
        }

        const char* InputModeName(InputMode mode) noexcept
        {
            return mode == InputMode::Keyboard ? "Keyboard" : "Controller";
        }

        const char* ControllerBackendName(ControllerBackend backend) noexcept
        {
            return backend == ControllerBackend::XInput ? "XInput" : "RawHid";
        }

        void SignalStartup(
            RuntimeState& state,
            bool success,
            std::string message)
        {
            {
                std::lock_guard lock(state.lifecycle_mutex);
                state.startup_success = success;
                state.startup_error = std::move(message);
                state.startup_ready = true;
            }
            state.startup_condition.notify_all();
        }

        class NativeInputWorker final : public RawInputMessageSink
        {
        public:
            NativeInputWorker(HANDLE stop_event, InputSettings settings)
                : settings_(std::move(settings)),
                  stop_event_(stop_event),
                  current_process_id_(GetCurrentProcessId())
            {
            }

            ~NativeInputWorker() override
            {
                Shutdown();
            }

            std::expected<void, std::string> Initialize()
            {
                MSG queue_message{};
                PeekMessageW(
                    &queue_message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

                const DWORD worker_id = GetCurrentThreadId();
                if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL))
                {
                    PLOG_WARNING << "Input worker id=" << worker_id
                        << " priority request ABOVE_NORMAL failed error="
                        << GetLastError();
                }
                else
                {
                    PLOG_INFO << "Input worker id=" << worker_id
                        << " priority=ABOVE_NORMAL";
                }

                poll_hz_ = settings_.poll_hz();
                absolute_publication_enabled_ =
                    settings_.absolute_publication_enabled();
                input_mode_ = settings_.mode();
                press_percent_ = settings_.press_percent();
                release_percent_ = settings_.release_percent();
                keyboard_bindings_.assign(
                    settings_.keyboard().begin(), settings_.keyboard().end());
                std::visit(
                    [this]<typename ControllerSettings>(
                    const ControllerSettings& controller)
                    {
                        controller_bindings_.assign(
                            controller.bindings().begin(),
                            controller.bindings().end());
                        if constexpr (std::is_same_v<
                            ControllerSettings,
                            XInputControllerSettings>)
                        {
                            controller_identity_ = {
                                .backend = ControllerBackend::XInput,
                                .device_id = std::to_string(controller.slot()),
                            };
                        }
                        else
                        {
                            controller_identity_ = {
                                .backend = ControllerBackend::RawHid,
                                .device_id = controller.device_path(),
                            };
                        }
                    },
                    settings_.controller());

                mapper_.emplace(
                    input_mode_, keyboard_bindings_, controller_bindings_);
                auto evaluator = ControllerBindingEvaluator::Create(
                    controller_bindings_, press_percent_, release_percent_);
                if (!evaluator)
                {
                    return std::unexpected(evaluator.error());
                }
                evaluator_.emplace(std::move(*evaluator));

                const auto test = std::ranges::find(
                    keyboard_bindings_, LogicalAction::Test, &KeyboardBinding::action);
                if (test == keyboard_bindings_.end())
                {
                    return std::unexpected(
                        "Compiled input settings have no test binding");
                }
                const auto test_label = WideToUtf8(PhysicalKeyLabel(test->key));
                PLOG_INFO << "Input config mode=" << InputModeName(input_mode_)
                    << " poll_hz=" << poll_hz_
                    << " press_percent=" << press_percent_
                    << " release_percent=" << release_percent_
                    << " test_token=" << FormatPhysicalKey(test->key)
                    << " test_label=" << test_label;
                PLOG_INFO << "Input controller backend="
                    << ControllerBackendName(controller_identity_.backend)
                    << " identity=" << controller_identity_.device_id
                    << " binding_count=" << controller_bindings_.size();

                window_ = std::make_unique<Win32InputWindow>(
                    *this, RegisterOwnedRawInputDevices);
                const auto window_result = window_->Create(GetModuleHandleW(nullptr));
                if (!window_result)
                {
                    return std::unexpected(window_result.error());
                }
                PLOG_INFO << "Input Raw Input window hwnd=" << window_->hwnd()
                    << " owner_thread=" << window_->owner_thread_id()
                    << " requested_flags=INPUTSINK|DEVNOTIFY"
                    << " effective=INPUTSINK legacy=true usages=06,05,04,08";

                const auto timer_result = CreatePollingTimer();
                if (!timer_result)
                {
                    return std::unexpected(timer_result.error());
                }

                InitializeController();
                mapper_->ClearAll();
                BeginGameplayTransitionEpoch(GameplayMaskFromFastIo(0));
                gameplay_epoch_begun_ = true;
                Publish();
                CheckForeground();
                return {};
            }

            std::expected<void, std::string> Run()
            {
                for (;;)
                {
                    const auto wake = WaitForInputWorkerWake(stop_event_, timer_);
                    if (!wake)
                    {
                        return std::unexpected(wake.error());
                    }

                    if (*wake == InputWorkerWake::Stop)
                    {
                        return {};
                    }
                    if (*wake == InputWorkerWake::Quit)
                    {
                        return std::unexpected(
                            "Input worker received unexpected WM_QUIT");
                    }

                    OnTimer();
                }
            }

            void OnRawInput(HRAWINPUT handle) noexcept override
            {
                const std::uint32_t raw_message_queue_age_ms =
                    GetTickCount() - static_cast<DWORD>(GetMessageTime());
                const auto packet = packets_.Read(handle);
                if (!packet)
                {
                    LogPacketError(packet.error());
                    return;
                }

                const RAWINPUT& input = **packet;
                const bool foreground = CheckForeground();
                if (!foreground)
                {
                    return;
                }

                if (input.header.dwType == RIM_TYPEKEYBOARD)
                {
                    const auto transition = DecodeRawKeyboard(input.data.keyboard);
                    if (!transition)
                    {
                        return;
                    }
                    PLOG_DEBUG << "Input raw keyboard token="
                        << FormatPhysicalKey(transition->key)
                        << " pressed=" << transition->pressed;
                    mapper_->ApplyKeyboardTransition(
                        transition->key, transition->pressed);
                    Publish(raw_message_queue_age_ms);
                    return;
                }

                if (input.header.dwType == RIM_TYPEHID && raw_hid_)
                {
                    const auto changed = raw_hid_->Apply(
                        input.header.hDevice, input.data.hid);
                    if (!changed)
                    {
                        LogPacketError(changed.error());
                        return;
                    }
                    if (*changed)
                    {
                        ApplyControllerState(
                            *raw_hid_, raw_message_queue_age_ms);
                    }
                }
            }

            void OnRawInputDeviceChange(
                WPARAM change,
                HANDLE device) noexcept override
            {
                PLOG_INFO << "Input device change="
                    << (change == GIDC_ARRIVAL ? "arrival" : "removal")
                    << " handle=" << device;
                if (xinput_)
                {
                    xinput_->RequestReconnectProbe();
                }
                if (controller_identity_.backend == ControllerBackend::RawHid)
                {
                    ReopenRawHid();
                }
            }

            void Shutdown() noexcept
            {
                if (shut_down_)
                {
                    return;
                }
                shut_down_ = true;
                if (mapper_)
                {
                    mapper_->ClearAll();
                    Publish();
                }
                if (gameplay_epoch_begun_)
                {
                    gameplay_epoch_begun_ = false;
                    EndGameplayTransitionEpoch();
                }
                if (evaluator_)
                {
                    evaluator_->Clear();
                }
                g_published_input.store(0, std::memory_order_release);
                raw_hid_.reset();
                xinput_.reset();
                if (timer_ != nullptr)
                {
                    CancelWaitableTimer(timer_);
                    CloseHandle(timer_);
                    timer_ = nullptr;
                }
                if (window_)
                {
                    window_->Destroy();
                    window_.reset();
                }
            }

        private:
            std::expected<void, std::string> CreatePollingTimer()
            {
                timer_ = CreateWaitableTimerExW(
                    nullptr,
                    nullptr,
                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                    TIMER_ALL_ACCESS);
                bool high_resolution = timer_ != nullptr;
                if (timer_ == nullptr)
                {
                    const DWORD high_resolution_error = GetLastError();
                    timer_ = CreateWaitableTimerExW(
                        nullptr, nullptr, 0, TIMER_ALL_ACCESS);
                    if (timer_ == nullptr)
                    {
                        return std::unexpected(
                            Win32Failure("CreateWaitableTimerExW"));
                    }
                    PLOG_WARNING << "High-resolution input timer unavailable error="
                        << high_resolution_error
                        << "; using standard waitable timer";
                }

                const LONG period_ms = static_cast<LONG>(1000 / poll_hz_);
                LARGE_INTEGER due_time{};
                due_time.QuadPart = -static_cast<LONGLONG>(period_ms) * 10'000;
                if (!SetWaitableTimer(
                    timer_, &due_time, period_ms, nullptr, nullptr, FALSE))
                {
                    return std::unexpected(Win32Failure("SetWaitableTimer"));
                }
                PLOG_INFO << "Input timer period_ms=" << period_ms
                    << " high_resolution=" << high_resolution;
                return {};
            }

            void InitializeController()
            {
                if (controller_identity_.backend == ControllerBackend::XInput)
                {
                    InitializeXInput();
                }
                else
                {
                    ReopenRawHid();
                }
            }

            void InitializeXInput()
            {
                const auto api = LoadSystemXInput();
                if (!api)
                {
                    PLOG_ERROR << "Controller input disabled: " << api.error();
                    return;
                }
                const std::uint32_t slot = static_cast<std::uint32_t>(
                    controller_identity_.device_id[0] - '0');
                auto controller = XInputController::Create(slot, std::move(*api));
                if (!controller)
                {
                    PLOG_ERROR << "Controller input disabled: "
                        << controller.error();
                    return;
                }
                xinput_.emplace(std::move(*controller));
                const auto poll = xinput_->Poll();
                if (!poll)
                {
                    PLOG_ERROR << "XInput initial poll failed: " << poll.error();
                }
                PLOG_INFO << "XInput dll=" << WideToUtf8(xinput_->loaded_name())
                    << " slot=" << xinput_->slot()
                    << " connected=" << xinput_->connected();
                if (!xinput_->connected())
                {
                    PLOG_WARNING << "Configured XInput slot is unavailable; "
                        << "keyboard/system input remains enabled";
                }
            }

            void ReopenRawHid()
            {
                const bool was_matched = raw_hid_.has_value();
                raw_hid_.reset();
                raw_device_handle_ = nullptr;
                evaluator_->Clear();
                mapper_->ClearController();
                Publish();

                const auto devices = EnumerateRawHidDevices();
                if (!devices)
                {
                    PLOG_ERROR << "Raw HID enumeration failed: " << devices.error();
                    return;
                }
                const auto* selected = FindExactRawHidDevice(
                    *devices, controller_identity_.device_id);
                if (selected == nullptr)
                {
                    if (was_matched || !raw_match_state_logged_)
                    {
                        PLOG_WARNING << "Configured Raw HID path unavailable: "
                            << controller_identity_.device_id
                            << "; keyboard/system input remains enabled";
                    }
                    raw_match_state_logged_ = true;
                    return;
                }

                auto controller = RawHidController::Open(*selected);
                if (!controller)
                {
                    PLOG_ERROR << "Raw HID input disabled for exact path="
                        << controller_identity_.device_id
                        << " error=" << controller.error();
                    raw_match_state_logged_ = true;
                    return;
                }
                const ControllerStateView& controller_view = *controller;
                for (std::size_t index = 0; index < controller_bindings_.size(); ++index)
                {
                    if (!controller_view.Activation(controller_bindings_[index]))
                    {
                        PLOG_ERROR << "Raw HID input disabled: binding[" << index
                            << "] unavailable for the selected device";
                        raw_match_state_logged_ = true;
                        return;
                    }
                }

                raw_device_handle_ = selected->raw_device;
                raw_hid_.emplace(std::move(*controller));
                if (!was_matched || !raw_match_state_logged_)
                {
                    PLOG_INFO << "Raw HID matched exact path=" << selected->device_path
                        << " vid=0x" << std::hex << selected->vendor_id
                        << " pid=0x" << selected->product_id
                        << " usage=0x" << selected->usage_page
                        << ":0x" << selected->usage << std::dec
                        << " handle=" << raw_device_handle_
                        << " control_count=" << raw_hid_->controls().size();
                }
                raw_match_state_logged_ = true;
            }

            void OnTimer()
            {
                if (!CheckForeground())
                {
                    return;
                }
                if (!xinput_)
                {
                    return;
                }

                const bool was_connected = xinput_->connected();
                const auto changed = xinput_->Poll();
                if (!changed)
                {
                    LogPacketError(changed.error());
                    return;
                }
                if (was_connected != xinput_->connected())
                {
                    PLOG_INFO << "XInput slot=" << xinput_->slot()
                        << " connected=" << xinput_->connected();
                }
                if (*changed)
                {
                    ApplyControllerState(*xinput_);
                }
            }

            void ApplyControllerState(
                const ControllerStateView& view,
                const std::optional<std::uint32_t> raw_message_queue_age_ms =
                    std::nullopt)
            {
                const auto states = evaluator_->Update(view);
                mapper_->ApplyControllerBindingStates(states);
                Publish(raw_message_queue_age_ms);
            }

            bool CheckForeground()
            {
                const HWND window = GetForegroundWindow();
                DWORD process_id{};
                const bool foreground = window != nullptr &&
                    current_process_id_ != 0 &&
                    GetWindowThreadProcessId(window, &process_id) != 0 &&
                    process_id == current_process_id_;
                const auto transition = foreground_tracker_.Update(foreground);
                if (transition.changed)
                {
                    PLOG_INFO << "Input foreground=" << foreground;
                }
                if (transition.clear_input)
                {
                    mapper_->ClearAll();
                    evaluator_->Clear();
                    if (xinput_)
                    {
                        xinput_->Clear();
                    }
                    if (raw_hid_)
                    {
                        raw_hid_->Clear();
                    }
                    Publish();
                }
                return foreground;
            }

            // Publishing mutates the process-wide snapshot and transition transport.
            // ReSharper disable once CppMemberFunctionMayBeConst
            void Publish(
                const std::optional<std::uint32_t> raw_message_queue_age_ms =
                    std::nullopt)
            {
                const std::uint32_t next = mapper_ ? mapper_->GetInput() : 0;
                gc::timing::AbsoluteHostTime observed_time{};
                if (absolute_publication_enabled_)
                {
                    LARGE_INTEGER observed_qpc_ticks{};
                    const auto qpc_result =
                        QueryPerformanceCounter(&observed_qpc_ticks);
                    if (!qpc_result || observed_qpc_ticks.QuadPart <= 0)
                    {
                        FatalInputPublicationQpc(
                            qpc_result, observed_qpc_ticks.QuadPart);
                    }
                    observed_time = {
                        .qpc_ticks = observed_qpc_ticks.QuadPart,
                        .multimedia_time_ms = timeGetTime(),
                    };
                }
                const std::uint32_t previous =
                    g_published_input.exchange(next, std::memory_order_acq_rel);
                if (previous != next)
                {
                    if (absolute_publication_enabled_)
                    {
                        PublishGameplayTransition(
                            previous,
                            next,
                            observed_time,
                            raw_message_queue_age_ms);
                    }
                    PLOG_DEBUG << "Input snapshot fastio=0x" << std::hex << next
                        << std::dec;
                }
            }

            void LogPacketError(std::string_view error) noexcept
            {
                const auto now = Clock::now();
                if (!last_packet_error_ ||
                    now - *last_packet_error_ >= std::chrono::seconds(1))
                {
                    PLOG_WARNING << "Input packet/poll error: " << error
                        << " suppressed=" << suppressed_packet_errors_;
                    last_packet_error_ = now;
                    suppressed_packet_errors_ = 0;
                }
                else if (suppressed_packet_errors_ !=
                    (std::numeric_limits<std::uint32_t>::max)())
                {
                    ++suppressed_packet_errors_;
                }
            }

            InputSettings settings_;
            HANDLE stop_event_{};
            HANDLE timer_{};
            std::unique_ptr<Win32InputWindow> window_;
            RawInputPacketBuffer packets_;
            const DWORD current_process_id_;
            ForegroundTransitionTracker foreground_tracker_;
            std::vector<KeyboardBinding> keyboard_bindings_;
            std::vector<ControllerBinding> controller_bindings_;
            ControllerIdentity controller_identity_;
            std::optional<InputMapper> mapper_;
            std::optional<ControllerBindingEvaluator> evaluator_;
            std::optional<XInputController> xinput_;
            std::optional<RawHidController> raw_hid_;
            HANDLE raw_device_handle_{};
            std::optional<Clock::time_point> last_packet_error_;
            std::uint32_t suppressed_packet_errors_{};
            std::uint32_t poll_hz_{1000};
            std::uint32_t press_percent_{50};
            std::uint32_t release_percent_{40};
            InputMode input_mode_{InputMode::Keyboard};
            bool absolute_publication_enabled_{};
            bool gameplay_epoch_begun_{};
            bool raw_match_state_logged_{};
            bool shut_down_{};
        };

        void WorkerMain(
            RuntimeState& state,
            HANDLE stop_event,
            InputSettings settings) noexcept
        {
            NativeInputWorker worker(stop_event, std::move(settings));
            const auto initialized = worker.Initialize();
            if (!initialized)
            {
                g_published_input.store(0, std::memory_order_release);
                SignalStartup(state, false, initialized.error());
                return;
            }

            SignalStartup(state, true, {});
            PLOG_INFO << "Input polling worker started";

            const auto run_result = worker.Run();
            worker.Shutdown();
            g_published_input.store(0, std::memory_order_release);
            if (!run_result)
            {
                PLOG_ERROR << "Input polling worker stopped unexpectedly: "
                    << run_result.error();
            }
            else
            {
                PLOG_INFO << "Input polling worker stopped";
            }
        }
    } // namespace

    std::expected<void, std::string>
    ConfigureInputPollingRuntime(InputSettings settings) noexcept
    {
        try
        {
            auto& state = Runtime();
            std::lock_guard lock(state.lifecycle_mutex);
            if (state.starting || state.open_count != 0)
            {
                return std::unexpected(
                    "Input polling runtime is already starting or open");
            }
            if (state.settings)
            {
                return std::unexpected(
                    "Input polling runtime is already configured");
            }
            state.settings.emplace(std::move(settings));
            return {};
        }
        catch (const std::exception& error)
        {
            return std::unexpected(
                "Could not configure input polling runtime: " +
                std::string{error.what()});
        }
        catch (...)
        {
            return std::unexpected(
                "Could not configure input polling runtime");
        }
    }

    InputPollingOpenResult OpenInputPollingRuntime()
    {
        auto& state = Runtime();
        std::unique_lock lock(state.lifecycle_mutex);
        state.startup_condition.wait(lock, [&state]
        {
            return !state.starting;
        });
        if (state.open_count != 0)
        {
            ++state.open_count;
            return {true, {}};
        }
        if (!state.settings)
        {
            return {
                false,
                "Input polling runtime was not configured before open",
            };
        }

        state.startup_ready = false;
        state.startup_success = false;
        state.startup_error.clear();
        state.starting = true;
        g_published_input.store(0, std::memory_order_release);

        state.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (state.stop_event == nullptr)
        {
            state.starting = false;
            state.startup_condition.notify_all();
            return {false, Win32Failure("CreateEventW(input stop)")};
        }

        try
        {
            auto worker_settings = *state.settings;
            state.worker = std::thread(
                [&state,
                    stop_event = state.stop_event,
                    settings = std::move(worker_settings)]() mutable
                {
                    WorkerMain(state, stop_event, std::move(settings));
                });
        }
        catch (const std::exception& error)
        {
            CloseHandle(state.stop_event);
            state.stop_event = nullptr;
            state.starting = false;
            state.startup_condition.notify_all();
            return {
                false,
                "Could not start input polling worker: " +
                std::string{error.what()},
            };
        }
        catch (...)
        {
            CloseHandle(state.stop_event);
            state.stop_event = nullptr;
            state.starting = false;
            state.startup_condition.notify_all();
            return {false, "Could not start input polling worker"};
        }

        state.startup_condition.wait(lock, [&state]
        {
            return state.startup_ready;
        });
        if (!state.startup_success)
        {
            const std::string error = state.startup_error;
            if (state.worker.joinable())
            {
                state.worker.join();
            }
            CloseHandle(state.stop_event);
            state.stop_event = nullptr;
            g_published_input.store(0, std::memory_order_release);
            state.starting = false;
            state.startup_condition.notify_all();
            return {false, error};
        }

        state.open_count = 1;
        state.starting = false;
        state.startup_condition.notify_all();
        return {true, {}};
    }

    void CloseInputPollingRuntime() noexcept
    {
        auto& state = Runtime();
        std::unique_lock lock(state.lifecycle_mutex);
        state.startup_condition.wait(lock, [&state]
        {
            return !state.starting;
        });
        if (state.open_count == 0)
        {
            return;
        }
        --state.open_count;
        if (state.open_count != 0)
        {
            return;
        }

        if (state.stop_event != nullptr)
        {
            SetEvent(state.stop_event);
        }
        if (state.worker.joinable())
        {
            state.worker.join();
        }
        if (state.stop_event != nullptr)
        {
            CloseHandle(state.stop_event);
            state.stop_event = nullptr;
        }
        g_published_input.store(0, std::memory_order_release);
    }

    std::uint32_t ReadPublishedInput() noexcept
    {
        return g_published_input.load(std::memory_order_acquire);
    }
} // namespace gc::input
