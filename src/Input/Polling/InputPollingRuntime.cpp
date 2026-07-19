#include "Input/Polling/InputPollingRuntime.h"

#include "Config/config.h"
#include "Input/Polling/ForegroundPolicy.h"
#include "Input/Polling/InputMapper.h"
#include "Input/Polling/InputWorkerWait.h"
#include "Input/Types/PhysicalKey.h"
#include "Input/Win32/ControllerBindingEvaluator.h"
#include "Input/Win32/ControllerCatalog.h"
#include "Input/Win32/PhysicalKeyWin32.h"
#include "Input/Win32/RawHidController.h"
#include "Input/Win32/RawInputPacket.h"
#include "Input/Win32/Win32InputWindow.h"
#include "Input/Win32/XInputApi.h"
#include "Input/Win32/XInputController.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "plog/Log.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace gc::input {
namespace {

using Clock = std::chrono::steady_clock;

std::atomic<std::uint32_t> g_published_input{0};

struct RuntimeState {
    std::mutex lifecycle_mutex;
    std::condition_variable startup_condition;
    std::thread worker;
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
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(GetLastError());
}

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty())
    {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            count,
            nullptr,
            nullptr) != count)
    {
        return {};
    }
    return result;
}

const char* InputModeName(InputMode mode) noexcept
{
    return mode == InputMode::Keyboard ? "Keyboard" : "Controller";
}

const char* ControllerBackendName(ControllerBackend backend) noexcept
{
    return backend == ControllerBackend::XInput ? "XInput" : "RawHid";
}

std::vector<KeyboardBinding> KeyboardBindings(
    const gc::config::NativeKeyboardConfig& keyboard)
{
    return {
        {LogicalAction::LeftBoosterUp, keyboard.left_booster_up()},
        {LogicalAction::LeftBoosterDown, keyboard.left_booster_down()},
        {LogicalAction::LeftBoosterLeft, keyboard.left_booster_left()},
        {LogicalAction::LeftBoosterRight, keyboard.left_booster_right()},
        {LogicalAction::LeftBoosterButton, keyboard.left_booster_button()},
        {LogicalAction::RightBoosterUp, keyboard.right_booster_up()},
        {LogicalAction::RightBoosterDown, keyboard.right_booster_down()},
        {LogicalAction::RightBoosterLeft, keyboard.right_booster_left()},
        {LogicalAction::RightBoosterRight, keyboard.right_booster_right()},
        {LogicalAction::RightBoosterButton, keyboard.right_booster_button()},
        {LogicalAction::Service1, keyboard.service1()},
        {LogicalAction::Service2, keyboard.service2()},
        {LogicalAction::Service3, keyboard.service3()},
        {LogicalAction::P1Start, keyboard.p1_start()},
        {LogicalAction::P2Start, keyboard.p2_start()},
        {LogicalAction::P2Service, keyboard.p2_service()},
        {LogicalAction::Test, keyboard.test()},
    };
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

class NativeInputWorker final : public RawInputMessageSink {
public:
    explicit NativeInputWorker(HANDLE stop_event)
        : stop_event_(stop_event),
          foreground_api_{
              .get_foreground_window = GetForegroundWindow,
              .get_window_thread_process_id = GetWindowThreadProcessId,
              .current_process_id = GetCurrentProcessId(),
          }
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

        const auto& manager = ConfigManager::instance();
        poll_hz_ = manager.GetInputPollHertz();
        input_mode_ = manager.GetInputMode();
        press_percent_ = manager.GetAxisPressThresholdPercent();
        release_percent_ = manager.GetAxisReleaseThresholdPercent();
        keyboard_bindings_ = KeyboardBindings(manager.GetKeyboardConfig());
        controller_bindings_ = manager.GetControllerConfig().bindings();
        controller_identity_ = ControllerIdentity{
            .backend = manager.GetControllerConfig().backend(),
            .device_id = manager.GetControllerConfig().device_id(),
        };

        mapper_.emplace(
            input_mode_, keyboard_bindings_, controller_bindings_);
        auto evaluator = ControllerBindingEvaluator::Create(
            controller_bindings_, press_percent_, release_percent_);
        if (!evaluator)
        {
            return std::unexpected(evaluator.error());
        }
        evaluator_.emplace(std::move(*evaluator));

        const auto& keyboard = manager.GetKeyboardConfig();
        const auto test_label = WideToUtf8(PhysicalKeyLabel(keyboard.test()));
        PLOG_INFO << "Input config mode=" << InputModeName(input_mode_)
                  << " poll_hz=" << poll_hz_
                  << " press_percent=" << press_percent_
                  << " release_percent=" << release_percent_
                  << " test_token=" << FormatPhysicalKey(keyboard.test())
                  << " test_label=" << test_label;
        PLOG_INFO << "Input controller backend="
                  << ControllerBackendName(controller_identity_.backend)
                  << " identity=" << controller_identity_.device_id
                  << " binding_count=" << controller_bindings_.size();

        window_ = std::make_unique<Win32InputWindow>(*this);
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
        try
        {
            const auto packet = packets_.Read(handle);
            if (!packet)
            {
                LogPacketError(packet.error());
                return;
            }

            const bool foreground = CheckForeground();
            const RAWINPUT& input = **packet;
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
                mapper_->ApplyKeyboardTransition(
                    transition->key, transition->pressed);
                Publish();
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
                    ApplyControllerState(*raw_hid_);
                }
            }
        }
        catch (const std::exception& error)
        {
            LogPacketError(error.what());
        }
        catch (...)
        {
            LogPacketError("unknown Raw Input exception");
        }
    }

    void OnRawInputDeviceChange(
        WPARAM change,
        HANDLE device) noexcept override
    {
        try
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
        catch (const std::exception& error)
        {
            PLOG_ERROR << "Input device-change handling failed: "
                       << error.what();
        }
        catch (...)
        {
            PLOG_ERROR << "Input device-change handling failed: unknown exception";
        }
    }

    void Shutdown() noexcept
    {
        if (shut_down_)
        {
            return;
        }
        shut_down_ = true;
        try
        {
            if (mapper_)
            {
                mapper_->ClearAll();
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
        catch (...)
        {
            g_published_input.store(0, std::memory_order_release);
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
        for (std::size_t index = 0; index < controller_bindings_.size(); ++index)
        {
            if (const auto valid =
                    controller->ValidateBinding(controller_bindings_[index]);
                !valid)
            {
                PLOG_ERROR << "Raw HID input disabled: binding[" << index
                           << "] unavailable: " << valid.error();
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

    void ApplyControllerState(const ControllerStateView& view)
    {
        const auto states = evaluator_->Update(view);
        mapper_->ApplyControllerBindingStates(states);
        Publish();
    }

    bool CheckForeground()
    {
        const bool foreground = IsCurrentProcessForeground(foreground_api_);
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

    void Publish()
    {
        const std::uint32_t next = mapper_ ? mapper_->GetInput() : 0;
        const std::uint32_t previous =
            g_published_input.exchange(next, std::memory_order_acq_rel);
        if (previous != next)
        {
            PLOG_INFO << "Input snapshot fastio=0x" << std::hex << next
                      << std::dec;
        }
    }

    void LogPacketError(std::string_view error) noexcept
    {
        try
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
            else
            {
                ++suppressed_packet_errors_;
            }
        }
        catch (...)
        {
        }
    }

    HANDLE stop_event_{};
    HANDLE timer_{};
    std::unique_ptr<Win32InputWindow> window_;
    RawInputPacketBuffer packets_;
    ForegroundApi foreground_api_;
    ForegroundTransitionTracker foreground_tracker_;
    std::vector<KeyboardBinding> keyboard_bindings_;
    std::vector<DigitalControlBinding> controller_bindings_;
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
    bool raw_match_state_logged_{};
    bool shut_down_{};
};

void WorkerMain(RuntimeState& state, HANDLE stop_event) noexcept
{
    NativeInputWorker worker(stop_event);
    bool startup_signaled = false;
    try
    {
        const auto initialized = worker.Initialize();
        if (!initialized)
        {
            g_published_input.store(0, std::memory_order_release);
            SignalStartup(state, false, initialized.error());
            startup_signaled = true;
            return;
        }

        SignalStartup(state, true, {});
        startup_signaled = true;
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
    catch (const std::exception& error)
    {
        worker.Shutdown();
        g_published_input.store(0, std::memory_order_release);
        if (!startup_signaled)
        {
            SignalStartup(state, false, error.what());
        }
        else
        {
            PLOG_ERROR << "Input polling worker terminated: " << error.what();
        }
    }
    catch (...)
    {
        worker.Shutdown();
        g_published_input.store(0, std::memory_order_release);
        if (!startup_signaled)
        {
            SignalStartup(
                state, false, "Input worker initialization failed");
        }
        else
        {
            PLOG_ERROR << "Input polling worker terminated: unknown exception";
        }
    }
}

} // namespace

InputPollingOpenResult OpenInputPollingRuntime()
{
    auto& state = Runtime();
    std::unique_lock lock(state.lifecycle_mutex);
    state.startup_condition.wait(lock, [&state] {
        return !state.starting;
    });
    if (state.open_count != 0)
    {
        ++state.open_count;
        return {true, {}};
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
        state.worker = std::thread(
            [&state, stop_event = state.stop_event] {
                WorkerMain(state, stop_event);
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
            std::string("Create input worker thread failed: ") + error.what()};
    }

    state.startup_condition.wait(lock, [&state] {
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
    try
    {
        auto& state = Runtime();
        std::unique_lock lock(state.lifecycle_mutex);
        state.startup_condition.wait(lock, [&state] {
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
    catch (...)
    {
        g_published_input.store(0, std::memory_order_release);
    }
}

std::uint32_t ReadPublishedInput() noexcept
{
    return g_published_input.load(std::memory_order_acquire);
}

} // namespace gc::input
