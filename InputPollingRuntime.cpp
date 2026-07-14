#include "InputPollingRuntime.h"

#include "InputManager.h"
#include "config.h"
#include "plog/Log.h"

#include <Windows.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_NOIMPL
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace gc::input {
namespace {

constexpr Uint64 kNanosecondsPerSecond = 1'000'000'000;
SDL_AtomicInt g_published_input{};

struct RuntimeState {
    RuntimeState()
        : lifecycle_mutex(SDL_CreateMutex())
    {
    }

    SDL_Mutex* lifecycle_mutex = nullptr;
    SDL_Semaphore* startup_semaphore = nullptr;
    SDL_Thread* worker = nullptr;
    SDL_AtomicInt stop{};
    unsigned int open_count = 0;
    std::uint32_t poll_hz = 1000;
    bool startup_success = false;
    std::string startup_error;
};

struct WorkerResources {
    bool sdl_initialized = false;
    SDL_Window* window = nullptr;
    std::unique_ptr<InputManager> input_manager;

    ~WorkerResources()
    {
        input_manager.reset();
        if (window != nullptr)
        {
            SDL_DestroyWindow(window);
        }
        if (sdl_initialized)
        {
            SDL_Quit();
        }
    }
};

RuntimeState& runtime_state()
{
    // Avoid running SDL teardown or joining a worker from static destruction.
    static RuntimeState* state = new RuntimeState();
    return *state;
}

std::string make_startup_error(const char* stage, const char* detail)
{
    std::string message(stage);
    message += ": ";
    message += detail != nullptr && detail[0] != '\0'
        ? detail
        : "unknown error";
    return message;
}

void signal_startup(
    RuntimeState& state,
    bool success,
    std::string message)
{
    state.startup_success = success;
    state.startup_error = std::move(message);
    SDL_SignalSemaphore(state.startup_semaphore);
}

void drain_events_and_publish(InputManager& input_manager)
{
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        input_manager.HandleEvent(event);
    }

    SDL_SetAtomicInt(
        &g_published_input,
        static_cast<int>(input_manager.GetInput()));
}

int SDLCALL input_worker(void* context)
{
    auto& state = *static_cast<RuntimeState*>(context);
    WorkerResources resources;
    bool startup_was_signaled = false;

    const auto fail_startup = [&](const char* stage, const char* detail) {
        SDL_SetAtomicInt(&g_published_input, 0);
        signal_startup(
            state,
            false,
            make_startup_error(stage, detail));
        startup_was_signaled = true;
    };

    try
    {
        if (!SetThreadPriority(
                GetCurrentThread(),
                THREAD_PRIORITY_ABOVE_NORMAL))
        {
            PLOG_WARNING
                << "Input polling thread remains at normal priority; "
                << "SetThreadPriority failed with " << GetLastError();
        }
        else
        {
            PLOG_INFO << "Input polling thread priority is ABOVE_NORMAL";
        }

        const HWND game_window = FindWindowA("GameWare", "GameWare");
        if (game_window == nullptr)
        {
            fail_startup(
                "FindWindowA",
                "GameWare window was not found");
            return 1;
        }

        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");

        if (!SDL_Init(
                SDL_INIT_JOYSTICK |
                SDL_INIT_GAMEPAD |
                SDL_INIT_EVENTS |
                SDL_INIT_VIDEO))
        {
            fail_startup("SDL_Init", SDL_GetError());
            return 1;
        }
        resources.sdl_initialized = true;

        const auto mappings_path =
            std::filesystem::current_path() / "gamecontrollerdb.txt";
        if (std::filesystem::exists(mappings_path) &&
            SDL_AddGamepadMappingsFromFile(
                mappings_path.string().c_str()) < 0)
        {
            PLOG_WARNING << "Could not load gamecontrollerdb.txt: "
                         << SDL_GetError();
        }

        SDL_SetGamepadEventsEnabled(true);
        SDL_SetJoystickEventsEnabled(true);

        const SDL_PropertiesID properties = SDL_CreateProperties();
        if (properties == 0)
        {
            fail_startup("SDL_CreateProperties", SDL_GetError());
            return 1;
        }

        if (!SDL_SetPointerProperty(
                properties,
                SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER,
                game_window))
        {
            const std::string detail = SDL_GetError();
            SDL_DestroyProperties(properties);
            fail_startup("SDL_SetPointerProperty", detail.c_str());
            return 1;
        }

        resources.window = SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (resources.window == nullptr)
        {
            fail_startup(
                "SDL_CreateWindowWithProperties",
                SDL_GetError());
            return 1;
        }

        resources.input_manager = std::make_unique<InputManager>();
        drain_events_and_publish(*resources.input_manager);
        signal_startup(state, true, {});
        startup_was_signaled = true;

        PLOG_INFO << "Input polling started at "
                  << state.poll_hz << " Hz";

        const Uint64 period_ns =
            kNanosecondsPerSecond / state.poll_hz;
        Uint64 next_deadline = SDL_GetTicksNS();

        while (SDL_GetAtomicInt(&state.stop) == 0)
        {
            next_deadline += period_ns;
            Uint64 now = SDL_GetTicksNS();
            if (now >= next_deadline)
            {
                const Uint64 missed_periods =
                    ((now - next_deadline) / period_ns) + 1;
                next_deadline += missed_periods * period_ns;
            }

            now = SDL_GetTicksNS();
            if (now < next_deadline)
            {
                SDL_DelayNS(next_deadline - now);
            }

            if (SDL_GetAtomicInt(&state.stop) != 0)
            {
                break;
            }
            drain_events_and_publish(*resources.input_manager);
        }

        SDL_SetAtomicInt(&g_published_input, 0);
        return 0;
    }
    catch (const std::exception& error)
    {
        SDL_SetAtomicInt(&g_published_input, 0);
        if (!startup_was_signaled)
        {
            fail_startup("Input worker initialization", error.what());
        }
        else
        {
            PLOG_ERROR << "Input polling worker stopped: "
                       << error.what();
        }
        return 1;
    }
    catch (...)
    {
        SDL_SetAtomicInt(&g_published_input, 0);
        if (!startup_was_signaled)
        {
            fail_startup(
                "Input worker initialization",
                "unknown C++ exception");
        }
        else
        {
            PLOG_ERROR
                << "Input polling worker stopped: unknown exception";
        }
        return 1;
    }
}

}

InputPollingOpenResult OpenInputPollingRuntime()
{
    auto& state = runtime_state();
    if (state.lifecycle_mutex == nullptr)
    {
        return {
            false,
            make_startup_error("SDL_CreateMutex", SDL_GetError())};
    }

    SDL_LockMutex(state.lifecycle_mutex);
    if (state.open_count != 0)
    {
        ++state.open_count;
        SDL_UnlockMutex(state.lifecycle_mutex);
        return {true, {}};
    }

    state.startup_success = false;
    state.startup_error.clear();
    state.poll_hz = ConfigManager::instance().GetInputPollHertz();
    SDL_SetAtomicInt(&state.stop, 0);
    SDL_SetAtomicInt(&g_published_input, 0);

    state.startup_semaphore = SDL_CreateSemaphore(0);
    if (state.startup_semaphore == nullptr)
    {
        const auto message =
            make_startup_error("SDL_CreateSemaphore", SDL_GetError());
        SDL_UnlockMutex(state.lifecycle_mutex);
        return {false, message};
    }

    state.worker =
        SDL_CreateThread(input_worker, "GCLoader Input Polling", &state);
    if (state.worker == nullptr)
    {
        const auto message =
            make_startup_error("SDL_CreateThread", SDL_GetError());
        SDL_DestroySemaphore(state.startup_semaphore);
        state.startup_semaphore = nullptr;
        SDL_UnlockMutex(state.lifecycle_mutex);
        return {false, message};
    }

    SDL_WaitSemaphore(state.startup_semaphore);
    SDL_DestroySemaphore(state.startup_semaphore);
    state.startup_semaphore = nullptr;

    if (!state.startup_success)
    {
        const std::string message = state.startup_error;
        SDL_WaitThread(state.worker, nullptr);
        state.worker = nullptr;
        SDL_SetAtomicInt(&g_published_input, 0);
        SDL_UnlockMutex(state.lifecycle_mutex);
        return {false, message};
    }

    state.open_count = 1;
    SDL_UnlockMutex(state.lifecycle_mutex);
    return {true, {}};
}

void CloseInputPollingRuntime() noexcept
{
    auto& state = runtime_state();
    if (state.lifecycle_mutex == nullptr)
    {
        return;
    }

    SDL_LockMutex(state.lifecycle_mutex);
    if (state.open_count == 0)
    {
        SDL_UnlockMutex(state.lifecycle_mutex);
        return;
    }

    --state.open_count;
    if (state.open_count != 0)
    {
        SDL_UnlockMutex(state.lifecycle_mutex);
        return;
    }

    SDL_SetAtomicInt(&state.stop, 1);
    if (state.worker != nullptr)
    {
        SDL_WaitThread(state.worker, nullptr);
        state.worker = nullptr;
    }
    SDL_SetAtomicInt(&g_published_input, 0);
    SDL_UnlockMutex(state.lifecycle_mutex);
}

std::uint32_t ReadPublishedInput() noexcept
{
    return static_cast<std::uint32_t>(
        SDL_GetAtomicInt(&g_published_input));
}

}
