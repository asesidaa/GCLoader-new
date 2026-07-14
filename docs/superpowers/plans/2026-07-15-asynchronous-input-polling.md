# Asynchronous Input Polling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace register-read-driven SDL input handling with a focus-aware, fixed-rate worker that publishes the latest complete FastIO snapshot at 125, 250, 500, or 1000 Hz.

**Architecture:** A worker-owned InputPollingRuntime initializes SDL, drains all queued events, advances logical input state, and publishes one 32-bit word through SDL_AtomicInt. InputManager translates SDL events into a platform-free InputSnapshotState, while iDmacDrvRegisterRead becomes a single atomic load for FIO_NODE_0_INPUT. The first device open synchronously waits for worker startup, the final close joins it, and no worker lifecycle work occurs in DllMain.

**Tech Stack:** C++20, SDL 3.2.12 static APIs, Win32 FindWindowA and SetThreadPriority, reflect-cpp TOML, ImGui, CMake/Ninja, MSVC x86, CTest.

**Design:** [Asynchronous Input Polling Design](../specs/2026-07-15-asynchronous-input-polling-design.md)

## Global Constraints

- Accept exactly 125, 250, 500, and 1000 Hz; use 1000 Hz for generated/source defaults.
- input_poll_hz is a required top-level TOML field. Missing or unsupported values fail clearly; there is no compatibility default for an old file.
- Request THREAD_PRIORITY_ABOVE_NORMAL. Failure to raise priority is nonfatal and logged once.
- Use SDL_CreateThread, SDL_WaitThread, SDL_Semaphore, SDL_AtomicInt, SDL_GetTicksNS, and SDL_DelayNS. Do not add a custom Win32 timer or a timer callback thread.
- Use FindWindowA only to find the GameWare HWND and SetThreadPriority only for the exact above-normal priority request.
- Run SDL initialization, attached-window handling, event polling, InputManager construction/destruction, gamepad access, and SDL_Quit on the worker thread.
- Start on the first iDmacDrvOpen, reference-count later opens, and join on the final iDmacDrvClose. Do not start or join a thread in DllMain.
- Drain the complete SDL event queue on every sample and publish a complete FastIO word using an atomic store.
- If press and release are both drained in one sample, publish the final released state; do not stretch the press across snapshots.
- Keyboard gameplay input is active only while GameWare has focus. Explicitly clear all keyboard sources on SDL_EVENT_WINDOW_FOCUS_LOST.
- Keep keyboard, gamepad-button, and gamepad-axis sources separate so release of one source cannot erase another held source.
- No connected gamepad is nonfatal; keep keyboard/system input active and wait for SDL hotplug.
- Preserve Arcade/Switch semantics, system-key behavior, FastIO register values, and RFID/card-read handling.
- Keep GUI_main.cpp binding labels and their current field relationships unchanged; they already express logical booster directions correctly.
- Correct source defaults through the existing FastIO-named fields: left booster is W/S/A/D and D-pad, right booster is arrows, left/right sticks remain their respective booster axes.
- H:\\gc\\artifacts\\GCLoader is the source/commit tree. H:\\gc is runtime/operator state; add the required rate there without overwriting explicit bindings.
- Automated evidence is configuration tests, one focused state test, builds, and CTest. The user owns manual game launch and gameplay acceptance.

---

## File Structure

| File | Responsibility |
|---|---|
| InputSnapshotState.h/.cpp | Platform-free logical source state and logical-to-FastIO composition |
| tests/InputSnapshotStateTests.cpp | Exact direction mapping, hold/release, source overlap, and clearing tests |
| InputManager.h/.cpp | SDL event-to-logical-input translation and gamepad ownership |
| InputPollingRuntime.h/.cpp | Worker startup, cadence, atomic publication, reference counting, and shutdown |
| iDmacDrv32.cpp | Driver open/close delegation and snapshot-only register reads |
| config.h/.cpp | Required polling-rate type, validation, getter, and corrected defaults |
| config.toml | Source example with 1000 Hz and corrected default bindings |
| GUI_main.cpp | Fixed polling-rate selector; existing binding rows remain unchanged |
| tests/ConfigFeatureTests.cpp | Strict rate contract, round-trip, and corrected-default regression coverage |
| CMakeLists.txt | Runtime/state sources and focused test target |

### Task 1: Lock Down the Polling Configuration and Correct Defaults

**Files:**
- Modify: config.h:26-119, 195-200
- Modify: config.cpp:24-43
- Modify: config.toml:1-40
- Modify: GUI_main.cpp:1-20, 330-354
- Modify: tests/ConfigFeatureTests.cpp:17-60, 212-320, 344-370, 640-812

**Interfaces:**
- Consumes: Existing reflect-cpp InputConfig parsing and GUI serialization.
- Produces: InputPollHertzConfigValue, IsSupportedInputPollHertz(InputPollHertzConfigValue), ValidateInputPollHertz(InputPollHertzConfigValue), InputConfig::input_poll_hz, and ConfigManager::GetInputPollHertz().

- [ ] **Step 1: Add failing strict-rate and default-mapping assertions**

In tests/ConfigFeatureTests.cpp, add the required field to kRequiredConfigPrefix and replace the binding portion with these exact values:

~~~toml
axis_threshold = 16384
gamepad_index = 0
input_poll_hz = 1000
input_mode = 'Keyboard'
gameplay_input_style = 'Arcade'

[gamepad]
p1_axis_horizontal = 'leftx'
p1_axis_vertical = 'lefty'
p1_button1 = 'south'
p1_dpad_down = 'dpad_left'
p1_dpad_left = 'invalid'
p1_dpad_right = 'invalid'
p1_dpad_up = 'dpad_up'
p2_axis_horizontal = 'rightx'
p2_axis_vertical = 'righty'
p2_button1 = 'east'
p2_button_down = 'dpad_right'
p2_button_left = 'invalid'
p2_button_right = 'invalid'
p2_button_up = 'dpad_down'

[keyboard]
p1_button1 = 'space'
p1_down = 'a'
p1_left = 'up'
p1_right = 'left'
p1_start = '1'
p1_up = 'w'
p2_button1 = 'k'
p2_down = 'd'
p2_left = 'down'
p2_right = 'right'
p2_service = 'f2'
p2_start = '2'
p2_up = 's'
service1 = 'f1'
service2 = 'i'
service3 = 'p'
test = 't'
~~~

Add a gamepad-button assertion helper beside expect_key:

~~~cpp
int expect_gamepad_button(
    SDL_GamepadButton actual,
    SDL_GamepadButton expected,
    const char* name)
{
    if (actual == expected) {
        return 0;
    }
    std::cerr << name << ": expected " << static_cast<int>(expected)
              << ", got " << static_cast<int>(actual) << '\n';
    return 1;
}

int expect_gamepad_axis(
    SDL_GamepadAxis actual,
    SDL_GamepadAxis expected,
    const char* name)
{
    if (actual == expected) {
        return 0;
    }
    std::cerr << name << ": expected " << static_cast<int>(expected)
              << ", got " << static_cast<int>(actual) << '\n';
    return 1;
}

int expect_poll_rate_validation(
    InputPollHertzConfigValue value,
    bool expected_valid,
    const char* name)
{
    try {
        ValidateInputPollHertz(value);
        if (expected_valid) {
            return 0;
        }
    } catch (const std::runtime_error&) {
        if (!expected_valid) {
            return 0;
        }
    }
    std::cerr << name << ": validation result differed from expectation\n";
    return 1;
}
~~~

After generated_defaults and generated_toml are created, add:

~~~cpp
failures += expect_u32(
    upgraded_defaults.input_poll_hz(),
    1000,
    "upgraded default input_poll_hz");
failures += expect_u32(
    generated_defaults.input_poll_hz(),
    1000,
    "constructed ConfigGUI input_poll_hz");
failures += expect_bool(
    generated_toml.find("input_poll_hz = 1000") != std::string::npos,
    true,
    "generated TOML input_poll_hz");

for (const auto rate : std::array<InputPollHertzConfigValue, 4>{
         125, 250, 500, 1000}) {
    failures += expect_poll_rate_validation(rate, true, "supported input poll rate");
}
failures += expect_poll_rate_validation(0, false, "zero input poll rate");
failures += expect_poll_rate_validation(333, false, "unsupported input poll rate");
failures += expect_poll_rate_validation(2000, false, "too-high input poll rate");

failures += expect_parse_failure(
    replace_once(
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
        "input_poll_hz = 1000\n",
        ""),
    "missing input_poll_hz");

const auto custom_poll_config = parse_config(replace_once(
    std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
    "input_poll_hz = 1000",
    "input_poll_hz = 250"));
failures += expect_u32(
    custom_poll_config.input_poll_hz(),
    250,
    "custom input_poll_hz");
const auto reparsed_poll_config =
    parse_config(rfl::toml::write(custom_poll_config));
failures += expect_u32(
    reparsed_poll_config.input_poll_hz(),
    250,
    "input_poll_hz TOML round trip");

failures += expect_key(
    generated_defaults.keyboard().p1_up(), SDLK_W, "default left booster up");
failures += expect_key(
    generated_defaults.keyboard().p2_up(), SDLK_S, "default left booster down");
failures += expect_key(
    generated_defaults.keyboard().p1_down(), SDLK_A, "default left booster left");
failures += expect_key(
    generated_defaults.keyboard().p2_down(), SDLK_D, "default left booster right");
failures += expect_key(
    generated_defaults.keyboard().p1_left(), SDLK_UP, "default right booster up");
failures += expect_key(
    generated_defaults.keyboard().p2_left(), SDLK_DOWN, "default right booster down");
failures += expect_key(
    generated_defaults.keyboard().p1_right(), SDLK_LEFT, "default right booster left");
failures += expect_key(
    generated_defaults.keyboard().p2_right(), SDLK_RIGHT, "default right booster right");
failures += expect_key(
    generated_defaults.keyboard().p1_button1(),
    SDLK_SPACE,
    "default left booster center button");
failures += expect_key(
    generated_defaults.keyboard().p2_button1(),
    SDLK_K,
    "default right booster center button");

failures += expect_gamepad_button(
    generated_defaults.gamepad().p1_dpad_up(),
    SDL_GAMEPAD_BUTTON_DPAD_UP,
    "default left booster dpad up");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p2_button_up(),
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    "default left booster dpad down");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p1_dpad_down(),
    SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    "default left booster dpad left");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p2_button_down(),
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    "default left booster dpad right");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p1_dpad_left(),
    SDL_GAMEPAD_BUTTON_INVALID,
    "default right booster up button disabled");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p2_button_left(),
    SDL_GAMEPAD_BUTTON_INVALID,
    "default right booster down button disabled");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p1_dpad_right(),
    SDL_GAMEPAD_BUTTON_INVALID,
    "default right booster left button disabled");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p2_button_right(),
    SDL_GAMEPAD_BUTTON_INVALID,
    "default right booster right button disabled");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p1_button1(),
    SDL_GAMEPAD_BUTTON_SOUTH,
    "default left booster center button");
failures += expect_gamepad_button(
    generated_defaults.gamepad().p2_button1(),
    SDL_GAMEPAD_BUTTON_EAST,
    "default right booster center button");
failures += expect_gamepad_axis(
    generated_defaults.gamepad().p1_axis_horizontal(),
    SDL_GAMEPAD_AXIS_LEFTX,
    "default left booster horizontal axis");
failures += expect_gamepad_axis(
    generated_defaults.gamepad().p1_axis_vertical(),
    SDL_GAMEPAD_AXIS_LEFTY,
    "default left booster vertical axis");
failures += expect_gamepad_axis(
    generated_defaults.gamepad().p2_axis_horizontal(),
    SDL_GAMEPAD_AXIS_RIGHTX,
    "default right booster horizontal axis");
failures += expect_gamepad_axis(
    generated_defaults.gamepad().p2_axis_vertical(),
    SDL_GAMEPAD_AXIS_RIGHTY,
    "default right booster vertical axis");
~~~

- [ ] **Step 2: Run the config test and verify the new contract fails**

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests && build-msvc32-latest\ConfigFeatureTests.exe'
~~~

Expected: compilation fails because InputPollHertzConfigValue, ValidateInputPollHertz, and input_poll_hz do not exist, proving the assertions precede implementation.

- [ ] **Step 3: Add the strict numeric type, validation, getter, and corrected C++ defaults**

In config.h, use unsigned long so reflect-cpp does not select the SDL_Keycode string reflector used by std::uint32_t:

~~~cpp
using InputPollHertzConfigValue = unsigned long;
static_assert(sizeof(InputPollHertzConfigValue) == sizeof(std::uint32_t));

inline constexpr bool IsSupportedInputPollHertz(
    InputPollHertzConfigValue value) noexcept
{
    return value == 125 || value == 250 || value == 500 || value == 1000;
}

inline void ValidateInputPollHertz(InputPollHertzConfigValue value)
{
    if (!IsSupportedInputPollHertz(value)) {
        throw std::runtime_error(
            "Invalid input_poll_hz; expected one of 125, 250, 500, or 1000");
    }
}
~~~

Change KeyboardConfig direction defaults to:

~~~cpp
rfl::Rename<"p1_up", SDL_Keycode> p1_up = SDLK_W;
rfl::Rename<"p1_down", SDL_Keycode> p1_down = SDLK_A;
rfl::Rename<"p1_left", SDL_Keycode> p1_left = SDLK_UP;
rfl::Rename<"p1_right", SDL_Keycode> p1_right = SDLK_LEFT;
rfl::Rename<"p1_button1", SDL_Keycode> p1_button1 = SDLK_SPACE;

rfl::Rename<"p2_up", SDL_Keycode> p2_up = SDLK_S;
rfl::Rename<"p2_down", SDL_Keycode> p2_down = SDLK_D;
rfl::Rename<"p2_left", SDL_Keycode> p2_left = SDLK_DOWN;
rfl::Rename<"p2_right", SDL_Keycode> p2_right = SDLK_RIGHT;
rfl::Rename<"p2_button1", SDL_Keycode> p2_button1 = SDLK_K;
~~~

Change GamepadConfig direction-button defaults to:

~~~cpp
rfl::Rename<"p1_dpad_up", SDL_GamepadButton> p1_dpad_up =
    SDL_GAMEPAD_BUTTON_DPAD_UP;
rfl::Rename<"p1_dpad_down", SDL_GamepadButton> p1_dpad_down =
    SDL_GAMEPAD_BUTTON_DPAD_LEFT;
rfl::Rename<"p1_dpad_left", SDL_GamepadButton> p1_dpad_left =
    SDL_GAMEPAD_BUTTON_INVALID;
rfl::Rename<"p1_dpad_right", SDL_GamepadButton> p1_dpad_right =
    SDL_GAMEPAD_BUTTON_INVALID;
rfl::Rename<"p1_button1", SDL_GamepadButton> p1_button1 =
    SDL_GAMEPAD_BUTTON_SOUTH;

rfl::Rename<"p2_button_up", SDL_GamepadButton> p2_button_up =
    SDL_GAMEPAD_BUTTON_DPAD_DOWN;
rfl::Rename<"p2_button_down", SDL_GamepadButton> p2_button_down =
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
rfl::Rename<"p2_button_left", SDL_GamepadButton> p2_button_left =
    SDL_GAMEPAD_BUTTON_INVALID;
rfl::Rename<"p2_button_right", SDL_GamepadButton> p2_button_right =
    SDL_GAMEPAD_BUTTON_INVALID;
~~~

Add the required field to InputConfig between gamepad_index and input_mode:

~~~cpp
rfl::Rename<"gamepad_index", int> gamepad_index = 0;
rfl::Rename<"axis_threshold", Sint16> axis_threshold = 16384;
rfl::Rename<"input_poll_hz", InputPollHertzConfigValue> input_poll_hz = 1000;
rfl::Rename<"input_mode", InputMode> input_mode = InputMode::Keyboard;
~~~

Add this ConfigManager getter:

~~~cpp
std::uint32_t GetInputPollHertz() const
{
    return static_cast<std::uint32_t>(config.input_poll_hz.value());
}
~~~

In config.cpp, validate immediately after a successful TOML read and before NESYS/registry validation:

~~~cpp
ValidateInputPollHertz(result.value().input_poll_hz());
~~~

- [ ] **Step 4: Update the source TOML without changing the GUI binding semantics**

Make the top of config.toml and its two binding tables exactly:

~~~toml
axis_threshold = 16384
gamepad_index = 0
input_poll_hz = 1000
input_mode = 'Keyboard'
gameplay_input_style = 'Arcade'

[gamepad]
p1_axis_horizontal = 'leftx'
p1_axis_vertical = 'lefty'
p1_button1 = 'south'
p1_dpad_down = 'dpad_left'
p1_dpad_left = 'invalid'
p1_dpad_right = 'invalid'
p1_dpad_up = 'dpad_up'
p2_axis_horizontal = 'rightx'
p2_axis_vertical = 'righty'
p2_button1 = 'east'
p2_button_down = 'dpad_right'
p2_button_left = 'invalid'
p2_button_right = 'invalid'
p2_button_up = 'dpad_down'

[keyboard]
p1_button1 = 'space'
p1_down = 'a'
p1_left = 'up'
p1_right = 'left'
p1_start = '1'
p1_up = 'w'
p2_button1 = 'k'
p2_down = 'd'
p2_left = 'down'
p2_right = 'right'
p2_service = 'f2'
p2_start = '2'
p2_up = 's'
service1 = 'f1'
service2 = 'i'
service3 = 'p'
test = 't'
card_read = 'f4'
~~~

Leave GUI_main.cpp lines 612-675 unchanged. Those rows already map logical labels to the correct FastIO-named fields.

- [ ] **Step 5: Add a fixed-rate ConfigGUI combo**

Add algorithm, array, and iterator includes if absent, then insert this after Gameplay Input Style:

~~~cpp
constexpr std::array<InputPollHertzConfigValue, 4> kInputPollRates{
    125, 250, 500, 1000};
constexpr const char* kInputPollRateLabels[]{
    "125 Hz", "250 Hz", "500 Hz", "1000 Hz"};

auto& input_poll_hz = g_config.input_poll_hz();
const auto rate_it = std::find(
    kInputPollRates.begin(), kInputPollRates.end(), input_poll_hz);
int current_rate = rate_it == kInputPollRates.end()
    ? 3
    : static_cast<int>(std::distance(kInputPollRates.begin(), rate_it));
if (ImGui::Combo(
        "Input Polling Rate",
        &current_rate,
        kInputPollRateLabels,
        IM_ARRAYSIZE(kInputPollRateLabels))) {
    input_poll_hz = kInputPollRates[static_cast<std::size_t>(current_rate)];
    g_config_dirty = true;
}
~~~

- [ ] **Step 6: Run focused config and GUI verification**

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests ConfigGUI && build-msvc32-latest\ConfigFeatureTests.exe'
~~~

Expected: both targets build and ConfigFeatureTests prints its existing success line with exit code 0.

- [ ] **Step 7: Commit the configuration slice**

~~~powershell
git add -- config.h config.cpp config.toml GUI_main.cpp tests/ConfigFeatureTests.cpp
git commit -m "feat: add strict input polling configuration"
~~~

### Task 2: Add Platform-Free Logical Snapshot State

**Files:**
- Create: InputSnapshotState.h
- Create: InputSnapshotState.cpp
- Create: tests/InputSnapshotStateTests.cpp
- Modify: CMakeLists.txt:142-172, 396-413

**Interfaces:**
- Consumes: No SDL or Win32 types.
- Produces: gc::input::LogicalInput, gc::input::InputSource, gc::input::GameplaySource, gc::input::InputSnapshotState::Set, ClearKeyboard, ClearGamepad, and Compose.

- [ ] **Step 1: Write the focused state test**

Create tests/InputSnapshotStateTests.cpp:

~~~cpp
#include "InputSnapshotState.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <utility>

namespace {

int expect_word(
    std::uint32_t actual,
    std::uint32_t expected,
    const char* name)
{
    if (actual == expected) {
        return 0;
    }
    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return 1;
}

}

int main()
{
    using namespace gc::input;
    int failures = 0;

    constexpr std::array direction_cases{
        std::pair{LogicalInput::LeftBoosterUp, FastIoBits::P1_UP},
        std::pair{LogicalInput::LeftBoosterDown, FastIoBits::P2_UP},
        std::pair{LogicalInput::LeftBoosterLeft, FastIoBits::P1_DOWN},
        std::pair{LogicalInput::LeftBoosterRight, FastIoBits::P2_DOWN},
        std::pair{LogicalInput::RightBoosterUp, FastIoBits::P1_LEFT},
        std::pair{LogicalInput::RightBoosterDown, FastIoBits::P2_LEFT},
        std::pair{LogicalInput::RightBoosterLeft, FastIoBits::P1_RIGHT},
        std::pair{LogicalInput::RightBoosterRight, FastIoBits::P2_RIGHT},
        std::pair{LogicalInput::LeftBoosterButton, FastIoBits::P1_BUTTON_1},
        std::pair{LogicalInput::RightBoosterButton, FastIoBits::P2_BUTTON_1}};

    for (const auto& [logical, fast_io] : direction_cases) {
        InputSnapshotState state;
        state.Set(logical, InputSource::Keyboard, true);
        failures += expect_word(
            state.Compose(GameplaySource::Keyboard),
            fast_io,
            "logical direction to FastIO");
    }

    constexpr std::array system_cases{
        std::pair{LogicalInput::Service1, FastIoBits::P1_SERVICE_F1},
        std::pair{LogicalInput::Service2, FastIoBits::P1_SERVICE_I},
        std::pair{LogicalInput::Service3, FastIoBits::P1_SERVICE_P},
        std::pair{LogicalInput::P1Start, FastIoBits::P1_START},
        std::pair{LogicalInput::P2Start, FastIoBits::P2_START},
        std::pair{LogicalInput::P2Service, FastIoBits::P2_SERVICE},
        std::pair{LogicalInput::Test, FastIoBits::TEST_MODE}};
    for (const auto& [logical, fast_io] : system_cases) {
        InputSnapshotState state;
        state.Set(logical, InputSource::Keyboard, true);
        failures += expect_word(
            state.Compose(GameplaySource::Gamepad),
            fast_io,
            "system input to FastIO");
    }

    InputSnapshotState held;
    held.Set(LogicalInput::LeftBoosterUp, InputSource::Keyboard, true);
    failures += expect_word(
        held.Compose(GameplaySource::Keyboard),
        FastIoBits::P1_UP,
        "pressed key");
    failures += expect_word(
        held.Compose(GameplaySource::Keyboard),
        FastIoBits::P1_UP,
        "held key remains pressed");
    held.Set(LogicalInput::LeftBoosterUp, InputSource::Keyboard, false);
    failures += expect_word(
        held.Compose(GameplaySource::Keyboard), 0, "released key");

    InputSnapshotState combined;
    combined.Set(
        LogicalInput::LeftBoosterLeft, InputSource::GamepadButton, true);
    combined.Set(
        LogicalInput::LeftBoosterLeft, InputSource::GamepadAxis, true);
    combined.Set(
        LogicalInput::LeftBoosterLeft, InputSource::GamepadButton, false);
    failures += expect_word(
        combined.Compose(GameplaySource::Gamepad),
        FastIoBits::P1_DOWN,
        "axis survives button release");
    combined.Set(
        LogicalInput::LeftBoosterLeft, InputSource::GamepadAxis, false);
    failures += expect_word(
        combined.Compose(GameplaySource::Gamepad),
        0,
        "direction clears after both gamepad sources release");

    combined.Set(
        LogicalInput::RightBoosterButton, InputSource::GamepadButton, true);
    combined.ClearGamepad();
    failures += expect_word(
        combined.Compose(GameplaySource::Gamepad),
        0,
        "gamepad disconnect clears gamepad sources");

    InputSnapshotState system_keys;
    system_keys.Set(LogicalInput::Test, InputSource::Keyboard, true);
    system_keys.Set(LogicalInput::P2Start, InputSource::Keyboard, true);
    failures += expect_word(
        system_keys.Compose(GameplaySource::Gamepad),
        FastIoBits::TEST_MODE | FastIoBits::P2_START,
        "system keyboard works in gamepad mode");
    system_keys.ClearKeyboard();
    failures += expect_word(
        system_keys.Compose(GameplaySource::Gamepad),
        0,
        "focus loss clears keyboard sources");

    if (failures != 0) {
        return 1;
    }
    std::cout << "InputSnapshotStateTests passed\n";
    return 0;
}
~~~

Register the test target:

~~~cmake
add_executable(InputSnapshotStateTests
        InputSnapshotState.cpp
        tests/InputSnapshotStateTests.cpp
)
target_include_directories(InputSnapshotStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME InputSnapshotStateTests COMMAND InputSnapshotStateTests)
~~~

- [ ] **Step 2: Run the new test and verify it fails**

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target InputSnapshotStateTests'
~~~

Expected: CMake/Ninja fails because InputSnapshotState.h and InputSnapshotState.cpp do not exist.

- [ ] **Step 3: Define logical inputs, independent sources, and FastIO constants**

Create InputSnapshotState.h:

~~~cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gc::input {

namespace FastIoBits {
inline constexpr std::uint32_t P1_SERVICE_I = 1u << 1;
inline constexpr std::uint32_t P1_SERVICE_F1 = 1u << 2;
inline constexpr std::uint32_t P1_SERVICE_P = 1u << 3;
inline constexpr std::uint32_t P1_START = 1u << 4;
inline constexpr std::uint32_t P2_START = 1u << 5;
inline constexpr std::uint32_t TEST_MODE = 1u << 6;
inline constexpr std::uint32_t P1_UP = 1u << 8;
inline constexpr std::uint32_t P2_UP = 1u << 9;
inline constexpr std::uint32_t P1_DOWN = 1u << 10;
inline constexpr std::uint32_t P2_DOWN = 1u << 11;
inline constexpr std::uint32_t P1_LEFT = 1u << 12;
inline constexpr std::uint32_t P2_LEFT = 1u << 13;
inline constexpr std::uint32_t P1_RIGHT = 1u << 14;
inline constexpr std::uint32_t P2_RIGHT = 1u << 15;
inline constexpr std::uint32_t P1_BUTTON_1 = 1u << 16;
inline constexpr std::uint32_t P2_BUTTON_1 = 1u << 17;
inline constexpr std::uint32_t P2_SERVICE = 1u << 2;
}

enum class LogicalInput : std::uint8_t {
    LeftBoosterUp,
    LeftBoosterDown,
    LeftBoosterLeft,
    LeftBoosterRight,
    LeftBoosterButton,
    RightBoosterUp,
    RightBoosterDown,
    RightBoosterLeft,
    RightBoosterRight,
    RightBoosterButton,
    Service1,
    Service2,
    Service3,
    P1Start,
    P2Start,
    P2Service,
    Test,
    Count
};

enum class InputSource : std::uint8_t {
    Keyboard,
    GamepadButton,
    GamepadAxis,
    Count
};

enum class GameplaySource : std::uint8_t {
    Keyboard,
    Gamepad
};

class InputSnapshotState {
public:
    void Set(LogicalInput input, InputSource source, bool pressed) noexcept;
    void ClearKeyboard() noexcept;
    void ClearGamepad() noexcept;
    std::uint32_t Compose(GameplaySource source) const noexcept;

private:
    static constexpr std::size_t kLogicalInputCount =
        static_cast<std::size_t>(LogicalInput::Count);
    static constexpr std::size_t kInputSourceCount =
        static_cast<std::size_t>(InputSource::Count);

    std::array<std::array<bool, kLogicalInputCount>, kInputSourceCount>
        sources_{};
};

}
~~~

- [ ] **Step 4: Implement composition and source-specific clearing**

Create InputSnapshotState.cpp:

~~~cpp
#include "InputSnapshotState.h"

#include <algorithm>
#include <array>

namespace gc::input {
namespace {

constexpr std::size_t index(LogicalInput input) noexcept
{
    return static_cast<std::size_t>(input);
}

constexpr std::size_t index(InputSource source) noexcept
{
    return static_cast<std::size_t>(source);
}

constexpr bool is_gameplay(LogicalInput input) noexcept
{
    return input <= LogicalInput::RightBoosterButton;
}

constexpr std::array<std::uint32_t, static_cast<std::size_t>(LogicalInput::Count)>
    kFastIoBits{
        FastIoBits::P1_UP,
        FastIoBits::P2_UP,
        FastIoBits::P1_DOWN,
        FastIoBits::P2_DOWN,
        FastIoBits::P1_BUTTON_1,
        FastIoBits::P1_LEFT,
        FastIoBits::P2_LEFT,
        FastIoBits::P1_RIGHT,
        FastIoBits::P2_RIGHT,
        FastIoBits::P2_BUTTON_1,
        FastIoBits::P1_SERVICE_F1,
        FastIoBits::P1_SERVICE_I,
        FastIoBits::P1_SERVICE_P,
        FastIoBits::P1_START,
        FastIoBits::P2_START,
        FastIoBits::P2_SERVICE,
        FastIoBits::TEST_MODE};

}

void InputSnapshotState::Set(
    LogicalInput input,
    InputSource source,
    bool pressed) noexcept
{
    if (input == LogicalInput::Count || source == InputSource::Count) {
        return;
    }
    sources_[index(source)][index(input)] = pressed;
}

void InputSnapshotState::ClearKeyboard() noexcept
{
    sources_[index(InputSource::Keyboard)].fill(false);
}

void InputSnapshotState::ClearGamepad() noexcept
{
    sources_[index(InputSource::GamepadButton)].fill(false);
    sources_[index(InputSource::GamepadAxis)].fill(false);
}

std::uint32_t InputSnapshotState::Compose(GameplaySource source) const noexcept
{
    std::uint32_t result = 0;
    for (std::size_t logical_index = 0;
         logical_index < kLogicalInputCount;
         ++logical_index) {
        const auto logical = static_cast<LogicalInput>(logical_index);
        bool pressed = false;
        if (!is_gameplay(logical) || source == GameplaySource::Keyboard) {
            pressed =
                sources_[index(InputSource::Keyboard)][logical_index];
        } else {
            pressed =
                sources_[index(InputSource::GamepadButton)][logical_index] ||
                sources_[index(InputSource::GamepadAxis)][logical_index];
        }
        if (pressed) {
            result |= kFastIoBits[logical_index];
        }
    }
    return result;
}

}
~~~

Add InputSnapshotState.cpp to SOURCES in CMakeLists.txt so the DLL will consume the same implementation tested above.

- [ ] **Step 5: Run the state test**

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target InputSnapshotStateTests && build-msvc32-latest\InputSnapshotStateTests.exe'
~~~

Expected: InputSnapshotStateTests passed.

- [ ] **Step 6: Commit the logical-state slice**

~~~powershell
git add -- CMakeLists.txt InputSnapshotState.h InputSnapshotState.cpp tests/InputSnapshotStateTests.cpp
git commit -m "feat: model logical input snapshots"
~~~

### Task 3: Make InputManager a Worker-Confined SDL Adapter

**Files:**
- Modify: InputManager.h:1-130
- Modify: InputManager.cpp:1-520

**Interfaces:**
- Consumes: InputSnapshotState::Set, ClearKeyboard, ClearGamepad, Compose and existing ConfigManager binding getters.
- Produces: InputManager::HandleEvent(const SDL_Event&) and InputManager::GetInput() as worker-only operations with no global SDL shutdown.

- [ ] **Step 1: Replace raw FastIO state with InputSnapshotState**

In InputManager.h, remove the InputBits namespace and replace the complete header with:

~~~cpp
#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

#include "InputSnapshotState.h"
#include "config.h"

class InputManager {
public:
    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void HandleEvent(const SDL_Event& event);
    std::uint32_t GetInput() const noexcept;
    void ReinitializeGamepad();

private:
    void LoadConfig();
    void OpenGamepad(SDL_JoystickID instance_id);
    void CloseGamepad();
    void UpdateAxisState(SDL_GamepadAxis axis, Sint16 value);
    void UpdateButtonState(SDL_GamepadButton button, bool pressed);
    void UpdateKeyState(SDL_Keycode key, bool pressed);

    SDL_Keycode keyP1Up = SDLK_UNKNOWN;
    SDL_Keycode keyP1Down = SDLK_UNKNOWN;
    SDL_Keycode keyP1Left = SDLK_UNKNOWN;
    SDL_Keycode keyP1Right = SDLK_UNKNOWN;
    SDL_Keycode keyP1Button1 = SDLK_UNKNOWN;
    SDL_Keycode keyP2Up = SDLK_UNKNOWN;
    SDL_Keycode keyP2Down = SDLK_UNKNOWN;
    SDL_Keycode keyP2Left = SDLK_UNKNOWN;
    SDL_Keycode keyP2Right = SDLK_UNKNOWN;
    SDL_Keycode keyP2Button1 = SDLK_UNKNOWN;
    SDL_Keycode keyTest = SDLK_UNKNOWN;
    SDL_Keycode keyService1 = SDLK_UNKNOWN;
    SDL_Keycode keyService2 = SDLK_UNKNOWN;
    SDL_Keycode keyService3 = SDLK_UNKNOWN;
    SDL_Keycode keyP1Start = SDLK_UNKNOWN;
    SDL_Keycode keyP2Start = SDLK_UNKNOWN;
    SDL_Keycode keyP2Service = SDLK_UNKNOWN;

    SDL_GamepadButton gpButtonP1Up = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Down = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Left = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Right = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP1Button1 = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Up = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Down = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Left = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Right = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadButton gpButtonP2Button1 = SDL_GAMEPAD_BUTTON_INVALID;

    SDL_GamepadAxis gpAxisP1Horizontal = SDL_GAMEPAD_AXIS_INVALID;
    SDL_GamepadAxis gpAxisP1Vertical = SDL_GAMEPAD_AXIS_INVALID;
    SDL_GamepadAxis gpAxisP2Horizontal = SDL_GAMEPAD_AXIS_INVALID;
    SDL_GamepadAxis gpAxisP2Vertical = SDL_GAMEPAD_AXIS_INVALID;

    Sint16 m_axisThreshold = 16384;
    int m_targetGamepadIndex = 0;
    InputMode m_inputMode = InputMode::Keyboard;
    SDL_Gamepad* m_gamepad = nullptr;
    SDL_JoystickID m_gamepadInstanceId = 0;
    gc::input::InputSnapshotState m_snapshotState;
};
~~~

- [ ] **Step 2: Remove SDL global shutdown from InputManager**

Replace the destructor with:

~~~cpp
InputManager::~InputManager()
{
    CloseGamepad();
}
~~~

CloseGamepad must clear both gamepad sources after closing or when no device is open:

~~~cpp
void InputManager::CloseGamepad()
{
    if (m_gamepad != nullptr) {
        PLOG_INFO << "Closing gamepad " << m_gamepadInstanceId;
        SDL_CloseGamepad(m_gamepad);
        m_gamepad = nullptr;
        m_gamepadInstanceId = 0;
    }
    m_snapshotState.ClearGamepad();
}
~~~

- [ ] **Step 3: Translate keyboard fields through logical booster names**

Replace UpdateKeyState with:

~~~cpp
void InputManager::UpdateKeyState(SDL_Keycode key, bool pressed)
{
    using enum gc::input::InputSource;
    using enum gc::input::LogicalInput;

    if (key == keyService1) {
        m_snapshotState.Set(Service1, Keyboard, pressed);
    } else if (key == keyService2) {
        m_snapshotState.Set(Service2, Keyboard, pressed);
    } else if (key == keyService3) {
        m_snapshotState.Set(Service3, Keyboard, pressed);
    } else if (key == keyP1Start) {
        m_snapshotState.Set(P1Start, Keyboard, pressed);
    } else if (key == keyP2Start) {
        m_snapshotState.Set(P2Start, Keyboard, pressed);
    } else if (key == keyTest) {
        m_snapshotState.Set(Test, Keyboard, pressed);
    } else if (key == keyP2Service) {
        m_snapshotState.Set(P2Service, Keyboard, pressed);
    }

    if (m_inputMode != InputMode::Keyboard) {
        return;
    }

    if (key == keyP1Up) {
        m_snapshotState.Set(LeftBoosterUp, Keyboard, pressed);
    } else if (key == keyP2Up) {
        m_snapshotState.Set(LeftBoosterDown, Keyboard, pressed);
    } else if (key == keyP1Down) {
        m_snapshotState.Set(LeftBoosterLeft, Keyboard, pressed);
    } else if (key == keyP2Down) {
        m_snapshotState.Set(LeftBoosterRight, Keyboard, pressed);
    } else if (key == keyP1Button1) {
        m_snapshotState.Set(LeftBoosterButton, Keyboard, pressed);
    } else if (key == keyP1Left) {
        m_snapshotState.Set(RightBoosterUp, Keyboard, pressed);
    } else if (key == keyP2Left) {
        m_snapshotState.Set(RightBoosterDown, Keyboard, pressed);
    } else if (key == keyP1Right) {
        m_snapshotState.Set(RightBoosterLeft, Keyboard, pressed);
    } else if (key == keyP2Right) {
        m_snapshotState.Set(RightBoosterRight, Keyboard, pressed);
    } else if (key == keyP2Button1) {
        m_snapshotState.Set(RightBoosterButton, Keyboard, pressed);
    }
}
~~~

This preserves system keys in both modes while treating p1/p2 field names only as FastIO labels.

- [ ] **Step 4: Keep gamepad button and axis sources independent**

Replace UpdateButtonState with:

~~~cpp
void InputManager::UpdateButtonState(
    SDL_GamepadButton button,
    bool pressed)
{
    if (m_inputMode != InputMode::Gamepad) {
        return;
    }

    using enum gc::input::LogicalInput;
    constexpr auto source = gc::input::InputSource::GamepadButton;

    if (button == gpButtonP1Up) {
        m_snapshotState.Set(LeftBoosterUp, source, pressed);
    } else if (button == gpButtonP2Up) {
        m_snapshotState.Set(LeftBoosterDown, source, pressed);
    } else if (button == gpButtonP1Down) {
        m_snapshotState.Set(LeftBoosterLeft, source, pressed);
    } else if (button == gpButtonP2Down) {
        m_snapshotState.Set(LeftBoosterRight, source, pressed);
    } else if (button == gpButtonP1Button1) {
        m_snapshotState.Set(LeftBoosterButton, source, pressed);
    } else if (button == gpButtonP1Left) {
        m_snapshotState.Set(RightBoosterUp, source, pressed);
    } else if (button == gpButtonP2Left) {
        m_snapshotState.Set(RightBoosterDown, source, pressed);
    } else if (button == gpButtonP1Right) {
        m_snapshotState.Set(RightBoosterLeft, source, pressed);
    } else if (button == gpButtonP2Right) {
        m_snapshotState.Set(RightBoosterRight, source, pressed);
    } else if (button == gpButtonP2Button1) {
        m_snapshotState.Set(RightBoosterButton, source, pressed);
    }
}
~~~

Replace UpdateAxisState with configured-axis handling:

~~~cpp
void InputManager::UpdateAxisState(SDL_GamepadAxis axis, Sint16 value)
{
    if (m_inputMode != InputMode::Gamepad) {
        return;
    }

    using enum gc::input::LogicalInput;
    constexpr auto source = gc::input::InputSource::GamepadAxis;
    const bool negative = value < -m_axisThreshold;
    const bool positive = value > m_axisThreshold;

    if (axis == gpAxisP1Vertical) {
        m_snapshotState.Set(LeftBoosterUp, source, negative);
        m_snapshotState.Set(LeftBoosterDown, source, positive);
    } else if (axis == gpAxisP1Horizontal) {
        m_snapshotState.Set(LeftBoosterLeft, source, negative);
        m_snapshotState.Set(LeftBoosterRight, source, positive);
    } else if (axis == gpAxisP2Vertical) {
        m_snapshotState.Set(RightBoosterUp, source, negative);
        m_snapshotState.Set(RightBoosterDown, source, positive);
    } else if (axis == gpAxisP2Horizontal) {
        m_snapshotState.Set(RightBoosterLeft, source, negative);
        m_snapshotState.Set(RightBoosterRight, source, positive);
    }
}
~~~

- [ ] **Step 5: Make focus loss explicit and remove event hot logging**

Keep device-added/device-removed lifecycle logging, delete the handled_events counter, sdl_event_type_name helper, and all GC120FPS_INPUT event/state diagnostics, and make these event cases exact:

~~~cpp
case SDL_EVENT_KEY_DOWN:
    if (!event.key.repeat) {
        UpdateKeyState(event.key.key, true);
    }
    break;
case SDL_EVENT_KEY_UP:
    UpdateKeyState(event.key.key, false);
    break;
case SDL_EVENT_WINDOW_FOCUS_LOST:
    m_snapshotState.ClearKeyboard();
    break;
case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    UpdateButtonState(
        static_cast<SDL_GamepadButton>(event.gbutton.button), true);
    break;
case SDL_EVENT_GAMEPAD_BUTTON_UP:
    UpdateButtonState(
        static_cast<SDL_GamepadButton>(event.gbutton.button), false);
    break;
case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    UpdateAxisState(
        static_cast<SDL_GamepadAxis>(event.gaxis.axis),
        event.gaxis.value);
    break;
~~~

On SDL_EVENT_GAMEPAD_REMOVED, continue using CloseGamepad so disconnect clears both gamepad sources before any later publication.

- [ ] **Step 6: Compose the worker-confined snapshot**

Replace GetInput with:

~~~cpp
std::uint32_t InputManager::GetInput() const noexcept
{
    const auto gameplay_source = m_inputMode == InputMode::Keyboard
        ? gc::input::GameplaySource::Keyboard
        : gc::input::GameplaySource::Gamepad;
    return m_snapshotState.Compose(gameplay_source);
}
~~~

- [ ] **Step 7: Build the adapter against the existing open path**

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target InputSnapshotStateTests iDmacDrv32'
~~~

Expected: InputSnapshotStateTests and iDmacDrv32 build. At this checkpoint iDmacDrv32 still polls on demand; the build only proves the adapter refactor before worker integration.

- [ ] **Step 8: Commit the SDL-adapter slice**

~~~powershell
git add -- InputManager.h InputManager.cpp
git commit -m "refactor: isolate SDL input event state"
~~~

### Task 4: Add the SDL-Owned Polling Runtime

**Files:**
- Create: InputPollingRuntime.h
- Create: InputPollingRuntime.cpp
- Modify: CMakeLists.txt:142-172

**Interfaces:**
- Consumes: ConfigManager::GetInputPollHertz(), InputManager::HandleEvent(), and InputManager::GetInput().
- Produces: gc::input::InputPollingOpenResult, OpenInputPollingRuntime(), CloseInputPollingRuntime(), and ReadPublishedInput().

- [ ] **Step 1: Define the lifecycle API**

Create InputPollingRuntime.h:

~~~cpp
#pragma once

#include <cstdint>
#include <string>

namespace gc::input {

struct InputPollingOpenResult {
    bool success = false;
    std::string message;
};

InputPollingOpenResult OpenInputPollingRuntime();
void CloseInputPollingRuntime() noexcept;
std::uint32_t ReadPublishedInput() noexcept;

}
~~~

- [ ] **Step 2: Add worker-owned resources and startup synchronization**

Create InputPollingRuntime.cpp with these includes and internal state:

~~~cpp
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

struct RuntimeState {
    RuntimeState()
        : lifecycle_mutex(SDL_CreateMutex())
    {
    }

    SDL_Mutex* lifecycle_mutex = nullptr;
    SDL_Semaphore* startup_semaphore = nullptr;
    SDL_Thread* worker = nullptr;
    SDL_AtomicInt stop{};
    SDL_AtomicInt published_input{};
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
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        if (sdl_initialized) {
            SDL_Quit();
        }
    }
};

RuntimeState& runtime_state()
{
    static RuntimeState* state = new RuntimeState();
    return *state;
}

std::string startup_error(const char* stage, const char* detail)
{
    std::string message(stage);
    message += ": ";
    message += detail != nullptr && detail[0] != '\0' ? detail : "unknown error";
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

void drain_events_and_publish(
    RuntimeState& state,
    InputManager& input_manager)
{
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        input_manager.HandleEvent(event);
    }
    SDL_SetAtomicInt(
        &state.published_input,
        static_cast<int>(input_manager.GetInput()));
}
~~~

The function-local state is deliberately process-lifetime state: it avoids a static destructor performing joins or SDL teardown during DLL unload. Normal cleanup remains the final iDmacDrvClose.

- [ ] **Step 3: Implement worker initialization, exact priority, polling, and cleanup**

Continue InputPollingRuntime.cpp with:

~~~cpp
int SDLCALL input_worker(void* context)
{
    auto& state = *static_cast<RuntimeState*>(context);
    WorkerResources resources;
    bool startup_was_signaled = false;

    const auto fail_startup = [&](const char* stage, const char* detail) {
        SDL_SetAtomicInt(&state.published_input, 0);
        signal_startup(state, false, startup_error(stage, detail));
        startup_was_signaled = true;
    };

    try {
        if (!SetThreadPriority(
                GetCurrentThread(),
                THREAD_PRIORITY_ABOVE_NORMAL)) {
            PLOG_WARNING
                << "Input polling thread remains at normal priority; "
                << "SetThreadPriority failed with " << GetLastError();
        } else {
            PLOG_INFO << "Input polling thread priority is ABOVE_NORMAL";
        }

        const HWND game_window = FindWindowA("GameWare", "GameWare");
        if (game_window == nullptr) {
            fail_startup("FindWindowA", "GameWare window was not found");
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
                SDL_INIT_VIDEO)) {
            fail_startup("SDL_Init", SDL_GetError());
            return 1;
        }
        resources.sdl_initialized = true;

        const auto mappings_path =
            std::filesystem::current_path() / "gamecontrollerdb.txt";
        if (std::filesystem::exists(mappings_path) &&
            SDL_AddGamepadMappingsFromFile(
                mappings_path.string().c_str()) < 0) {
            PLOG_WARNING << "Could not load gamecontrollerdb.txt: "
                         << SDL_GetError();
        }

        SDL_SetGamepadEventsEnabled(true);
        SDL_SetJoystickEventsEnabled(true);

        const SDL_PropertiesID properties = SDL_CreateProperties();
        if (properties == 0) {
            fail_startup("SDL_CreateProperties", SDL_GetError());
            return 1;
        }
        if (!SDL_SetPointerProperty(
                properties,
                SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER,
                game_window)) {
            const std::string detail = SDL_GetError();
            SDL_DestroyProperties(properties);
            fail_startup("SDL_SetPointerProperty", detail.c_str());
            return 1;
        }
        resources.window = SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (resources.window == nullptr) {
            fail_startup("SDL_CreateWindowWithProperties", SDL_GetError());
            return 1;
        }

        resources.input_manager = std::make_unique<InputManager>();
        drain_events_and_publish(state, *resources.input_manager);
        signal_startup(state, true, {});
        startup_was_signaled = true;

        PLOG_INFO << "Input polling started at " << state.poll_hz << " Hz";
        const Uint64 period_ns = kNanosecondsPerSecond / state.poll_hz;
        Uint64 next_deadline = SDL_GetTicksNS();

        while (SDL_GetAtomicInt(&state.stop) == 0) {
            drain_events_and_publish(state, *resources.input_manager);
            next_deadline += period_ns;

            const Uint64 now = SDL_GetTicksNS();
            if (now < next_deadline) {
                SDL_DelayNS(next_deadline - now);
            } else {
                const Uint64 missed_periods =
                    ((now - next_deadline) / period_ns) + 1;
                next_deadline += missed_periods * period_ns;
            }
        }

        SDL_SetAtomicInt(&state.published_input, 0);
        return 0;
    } catch (const std::exception& error) {
        SDL_SetAtomicInt(&state.published_input, 0);
        if (!startup_was_signaled) {
            fail_startup("InputManager construction", error.what());
        } else {
            PLOG_ERROR << "Input polling worker stopped: " << error.what();
        }
        return 1;
    } catch (...) {
        SDL_SetAtomicInt(&state.published_input, 0);
        if (!startup_was_signaled) {
            fail_startup(
                "InputManager construction",
                "unknown C++ exception");
        } else {
            PLOG_ERROR << "Input polling worker stopped: unknown exception";
        }
        return 1;
    }
}
~~~

This uses absolute deadlines and skips missed periods. It never runs catch-up samples and closes within at most one normal period after the stop flag is observed by the loop.

- [ ] **Step 4: Implement reference-counted open, final-close join, and atomic reads**

Finish InputPollingRuntime.cpp:

~~~cpp
}

InputPollingOpenResult OpenInputPollingRuntime()
{
    auto& state = runtime_state();
    if (state.lifecycle_mutex == nullptr) {
        return {
            false,
            startup_error("SDL_CreateMutex", SDL_GetError())};
    }

    SDL_LockMutex(state.lifecycle_mutex);
    if (state.open_count != 0) {
        ++state.open_count;
        SDL_UnlockMutex(state.lifecycle_mutex);
        return {true, {}};
    }

    state.startup_success = false;
    state.startup_error.clear();
    state.poll_hz = ConfigManager::instance().GetInputPollHertz();
    SDL_SetAtomicInt(&state.stop, 0);
    SDL_SetAtomicInt(&state.published_input, 0);

    state.startup_semaphore = SDL_CreateSemaphore(0);
    if (state.startup_semaphore == nullptr) {
        const auto message =
            startup_error("SDL_CreateSemaphore", SDL_GetError());
        SDL_UnlockMutex(state.lifecycle_mutex);
        return {false, message};
    }

    state.worker =
        SDL_CreateThread(input_worker, "GCLoader Input Polling", &state);
    if (state.worker == nullptr) {
        const auto message =
            startup_error("SDL_CreateThread", SDL_GetError());
        SDL_DestroySemaphore(state.startup_semaphore);
        state.startup_semaphore = nullptr;
        SDL_UnlockMutex(state.lifecycle_mutex);
        return {false, message};
    }

    SDL_WaitSemaphore(state.startup_semaphore);
    SDL_DestroySemaphore(state.startup_semaphore);
    state.startup_semaphore = nullptr;

    if (!state.startup_success) {
        const std::string message = state.startup_error;
        SDL_WaitThread(state.worker, nullptr);
        state.worker = nullptr;
        SDL_SetAtomicInt(&state.published_input, 0);
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
    if (state.lifecycle_mutex == nullptr) {
        return;
    }

    SDL_LockMutex(state.lifecycle_mutex);
    if (state.open_count == 0) {
        SDL_UnlockMutex(state.lifecycle_mutex);
        return;
    }

    --state.open_count;
    if (state.open_count != 0) {
        SDL_UnlockMutex(state.lifecycle_mutex);
        return;
    }

    SDL_SetAtomicInt(&state.stop, 1);
    if (state.worker != nullptr) {
        SDL_WaitThread(state.worker, nullptr);
        state.worker = nullptr;
    }
    SDL_SetAtomicInt(&state.published_input, 0);
    SDL_UnlockMutex(state.lifecycle_mutex);
}

std::uint32_t ReadPublishedInput() noexcept
{
    auto& state = runtime_state();
    return static_cast<std::uint32_t>(
        SDL_GetAtomicInt(&state.published_input));
}

}
~~~

- [ ] **Step 5: Compile the runtime as part of the DLL**

Add InputPollingRuntime.cpp to SOURCES, then run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32'
~~~

Expected: iDmacDrv32 builds. The runtime is linked but is not used by the export layer until Task 5.

- [ ] **Step 6: Commit the runtime slice**

~~~powershell
git add -- CMakeLists.txt InputPollingRuntime.h InputPollingRuntime.cpp
git commit -m "feat: add asynchronous input polling runtime"
~~~

### Task 5: Serve the Published Snapshot Through iDmac

**Files:**
- Modify: iDmacDrv32.cpp:1-250

**Interfaces:**
- Consumes: OpenInputPollingRuntime(), CloseInputPollingRuntime(), and ReadPublishedInput().
- Produces: iDmacDrvOpen with synchronous failure reporting, iDmacDrvClose with reference-counted shutdown, and a snapshot-only FIO_NODE_0_INPUT read.

- [ ] **Step 1: Replace SDL/InputManager globals with the runtime API**

Replace the include/global/helper block above iDmacDrvOpen with:

~~~cpp
#include <windows.h>

#include "InputPollingRuntime.h"
#include "RegisterOpTypes.h"
#include "plog/Log.h"

#include <cstdint>
#include <string>
~~~

This removes SDL_main, InputManager.h, inited, window, GetInputManager, register_read_name, the input diagnostic counters, and format. No file-level input object remains in the export layer.

- [ ] **Step 2: Make device open fail clearly without terminating the process**

Replace iDmacDrvOpen with:

~~~cpp
extern "C" __declspec(dllexport) DWORD __cdecl iDmacDrvOpen(
    int deviceId,
    LPVOID outBuffer,
    LPVOID lpSomeFlag)
{
    PLOG_VERBOSE << "iDmacDrvOpen";
    *static_cast<DWORD*>(outBuffer) = 284;
    *static_cast<DWORD*>(lpSomeFlag) = 0;

    const auto input_result = gc::input::OpenInputPollingRuntime();
    if (!input_result.success) {
        const std::string message =
            "Input initialization failed: " + input_result.message;
        PLOG_ERROR << message;
        MessageBoxA(
            nullptr,
            message.c_str(),
            "GCLoader Input Error",
            MB_OK | MB_ICONERROR);
        return ERROR_DEVICE_NOT_AVAILABLE;
    }
    return ERROR_SUCCESS;
}
~~~

The failure path leaves the published word at zero and returns a nonzero driver-open result. It does not call ExitProcess.

- [ ] **Step 3: Join the worker on the final close**

Replace iDmacDrvClose with:

~~~cpp
extern "C" __declspec(dllexport) DWORD __cdecl iDmacDrvClose(
    int deviceId,
    LPVOID lpWriteAccess)
{
    PLOG_VERBOSE << "iDmacDrvClose";
    gc::input::CloseInputPollingRuntime();
    return ERROR_SUCCESS;
}
~~~

- [ ] **Step 4: Reduce register reads to register selection plus one atomic load**

Keep every existing case value unchanged. Change only the input case to:

~~~cpp
case RegisterReadType::FIO_NODE_0_INPUT:
    result = gc::input::ReadPublishedInput();
    break;
~~~

The complete function must have no SDL_PollEvent, InputManager access, state-transition logging, periodic summaries, or per-call verbose logging. Preserve these final writes and return:

~~~cpp
*static_cast<DWORD*>(OutBuffer) = result;
*static_cast<DWORD*>(DeviceResult) = 0;
return 0;
~~~

- [ ] **Step 5: Prove the old polling path is gone and build**

Run:

~~~powershell
rg -n "SDL_PollEvent|GetInputManager|GC120FPS_INPUT|node0_input_reads|register_read_name" iDmacDrv32.cpp
~~~

Expected: no matches.

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests InputSnapshotStateTests && build-msvc32-latest\ConfigFeatureTests.exe && build-msvc32-latest\InputSnapshotStateTests.exe'
~~~

Expected: all four targets build and both focused tests pass.

- [ ] **Step 6: Commit export integration**

~~~powershell
git add -- iDmacDrv32.cpp
git commit -m "refactor: serve FastIO input snapshots"
~~~

### Task 6: Verify, Upgrade Runtime State, and Prepare Manual Acceptance

**Files:**
- Verify only: all tracked source files changed by Tasks 1-5
- Runtime-only edit if missing: H:\\gc\\config.toml
- Runtime deployment: H:\\gc\\iDmacDrv32.dll
- Runtime deployment: H:\\gc\\ConfigGUI.exe

**Interfaces:**
- Consumes: Completed DLL, GUI, configuration, and tests.
- Produces: Static/build evidence plus a deployed runtime ready for the user's manual game launch.

- [ ] **Step 1: Run focused tests and the complete CTest suite**

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests InputSnapshotStateTests && ctest --test-dir build-msvc32-latest --output-on-failure'
~~~

Expected: both production targets build and CTest reports 100% passing. If another unrelated test fails, record its exact name/output and do not describe the suite as passing.

- [ ] **Step 2: Audit the final diff against the hot-path and mapping contracts**

Run:

~~~powershell
git diff HEAD~4 --check
rg -n "SDL_PollEvent|GC120FPS_INPUT" iDmacDrv32.cpp
rg -n "L Booster Up|L Booster Left|L Booster Down|L Booster Right|R Booster Up|R Booster Left|R Booster Down|R Booster Right" GUI_main.cpp
rg -n "input_poll_hz|p1_up|p2_up|p1_down|p2_down|p1_left|p2_left|p1_right|p2_right" config.h config.toml
git status --short
~~~

Expected:

- git diff --check emits nothing.
- iDmacDrv32.cpp has no SDL/event/input-debug matches.
- GUI labels still point to the same FastIO fields documented in the design.
- Source config shows 1000 Hz and the W/S/A/D plus arrow defaults.
- The pre-existing untracked docs/superpowers/plans/2026-07-12-registry-config-virtualization.md remains untouched.

- [ ] **Step 3: Verify the production DLL is 32-bit**

Run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && dumpbin /headers build-msvc32-latest\iDmacDrv32.dll' | Select-String 'machine \(x86\)'
~~~

Expected: one machine (x86) line.

- [ ] **Step 4: Upgrade only the required field in operator runtime config**

Inspect first:

~~~powershell
Select-String -Path H:\gc\config.toml -Pattern '^input_poll_hz\s*=|^input_mode\s*=|^\[keyboard\]|^\[gamepad\]'
~~~

If input_poll_hz is absent, use apply_patch for only this insertion:

~~~diff
 gamepad_index = 0
+input_poll_hz = 1000
~~~

If it is present, require one of 125, 250, 500, or 1000 and leave the chosen value unchanged. Do not rewrite any runtime keyboard or gamepad binding: those values are explicit operator state.

- [ ] **Step 5: Ensure the game is stopped before replacing runtime binaries**

Run:

~~~powershell
Get-Process game471 -ErrorAction SilentlyContinue
~~~

Expected: no output. If the process is running, stop this step and ask the user to exit the game; do not terminate it automatically.

- [ ] **Step 6: Deploy the verified DLL and ConfigGUI**

Run:

~~~powershell
Copy-Item -LiteralPath build-msvc32-latest\iDmacDrv32.dll -Destination H:\gc\iDmacDrv32.dll -Force
Copy-Item -LiteralPath build-msvc32-latest\ConfigGUI.exe -Destination H:\gc\ConfigGUI.exe -Force
Get-FileHash build-msvc32-latest\iDmacDrv32.dll,H:\gc\iDmacDrv32.dll
Get-FileHash build-msvc32-latest\ConfigGUI.exe,H:\gc\ConfigGUI.exe
~~~

Expected: source/deployed SHA-256 pairs match.

- [ ] **Step 7: Hand off the manual game test without claiming runtime success**

Report automated evidence separately, then ask the user to launch the game and cover:

1. The log reports the selected rate and either ABOVE_NORMAL success or one clear normal-priority fallback.
2. W/S/A/D operate left booster up/down/left/right; arrow keys operate right booster up/down/left/right.
3. Holding stays held, release clears, and losing focus clears all keyboard gameplay input.
4. D-pad operates the left booster; left/right sticks operate their corresponding boosters.
5. A direction held by both an axis and button remains set until both release.
6. Disconnecting the gamepad clears its state and reconnect/hotplug resumes input.
7. Center buttons, service/test/start keys, Arcade style, Switch style, and RFID/card-read remain unchanged.
8. Menu/gameplay input is responsive at 1000 Hz with no observed regression.

Do not launch game471.exe from automated verification. User confirmation is the gameplay acceptance gate.
