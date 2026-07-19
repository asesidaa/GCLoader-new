# SDL-Free Native Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace SDL input and ConfigGUI integration with a native Win32 x86 input stack that makes configured keyboard Test input, exact-device controller mappings, and atomic FastIO publication work reliably inside `game471.exe`.

**Architecture:** Platform-free scan-code, logical-action, controller-binding, and hysteresis types sit in `gc_input_types`. A shared `gc_input_win32` layer decodes Raw Input, discovers exact HID identities, parses generic HID reports, samples one exact XInput slot, and exposes boolean control states to both the runtime and ConfigGUI. One gameplay worker owns the hidden Raw Input window, message pump, XInput timer, foreground gate, mapping state, and atomic FastIO publication; ConfigGUI uses the same discovery/capture model on an ImGui Win32/D3D11 host. The final cutover replaces the SDL configuration schema outright and then removes SDL from CMake.

**Tech Stack:** C++23, Win32 x86, Raw Input, Windows HID parser APIs, dynamically loaded XInput, `std::thread`/`std::atomic`, Dear ImGui Win32/D3D11 backends, reflect-cpp/TOML, plog, CMake/Ninja, MSVC x86, and CTest.

**Design:** [SDL-Free Native Input Design](../specs/2026-07-19-sdl-free-native-input-design.md)

## Global Constraints

- Execute this plan inline on the existing `configurable-fixed-framerate` branch. Do not create a worktree and do not delegate tasks to agents.
- Build only as Win32/x86 with `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`.
- Keep `H:\gc\artifacts\GCLoader` as the source, test, documentation, and commit tree. Treat `H:\gc` only as runtime/deployment state.
- Preserve the unrelated untracked `tall Microsoft.Gaming.GDKq` file. Do not add, edit, remove, or commit it.
- Use Raw Input for keyboard and generic HID gamepad/joystick/multi-axis devices, and XInput for Xbox-class controllers. Do not add DirectInput or GameInput.
- Use exactly one latency-sensitive gameplay input worker. RFID/card-read keeps its existing separate 100 ms `GetAsyncKeyState` worker.
- Create one hidden, unshown top-level Win32 `WS_POPUP` window with `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` on the gameplay worker. It must not be a message-only window and must not be the GameWare window.
- Register Generic Desktop keyboard `0x01/0x06`, gamepad `0x01/0x05`, joystick `0x01/0x04`, and multi-axis `0x01/0x08` with `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`.
- Never use `RIDEV_NOLEGACY`, `RIDEV_NOHOTKEYS`, subclassing, input-buffer clearing, or keyboard-state API hooks. Because Raw Input ownership is process-global, the gameplay runtime may guard `RegisterRawInputDevices` for its four owned usages; owned calls bypass through the trampoline, unprotected usages pass through, and ConfigGUI does not install the guard.
- Persist keyboard identity as Raw Input make code plus `None`, `E0`, or `E1`; labels are layout-aware presentation only.
- Accept a matching `PhysicalKey` from any attached keyboard; do not persist or filter by keyboard device handle.
- Persist one exact controller identity. XInput identity is backend plus slot `0..3`; Raw HID identity is backend plus exact `RIDI_DEVICENAME`. Never fall back to another slot, index, or path.
- Exclude `IG_` XInput shadow HID devices from Raw HID discovery and packet handling so input is not duplicated and LT/RT remain independent.
- Reduce every button, signed axis direction, trigger, and hat direction to boolean state. Default controller thresholds are 50 percent press and 40 percent release, exposed as integer fields.
- Permit multiple controller bindings per gameplay logical action and OR their independently latched states. One release must not clear another held binding.
- Keep Test, Service 1/2/3, P1/P2 Start, and P2 Service on keyboard in both Keyboard and Controller modes.
- Gate input by foreground process ID. Drain background Raw Input, clear every source, publish zero, and require a new keyboard transition after focus returns.
- Keep `iDmacDrvRegisterRead(FIO_NODE_0_INPUT)` to one atomic snapshot load. No polling, event handling, controller calls, mutexes, or hot-path logging may occur there.
- Start the gameplay worker on first `iDmacDrvOpen`, reuse it on later opens, and join it on final close. Do not start or join it in `DllMain`.
- Missing configured controllers are nonfatal. Required worker/window/timer/Raw Input setup failures fail startup with the published word left at zero.
- The final configuration requires `input_schema_version = 2`; there is no shipped compatibility or migration parser for `gamepad_index`, `axis_threshold`, or `[gamepad]`.
- Dear ImGui remains. ConfigGUI must use `imgui_impl_win32` plus `imgui_impl_dx11`, exact integer threshold fields, and shared native discovery/capture code.
- SDL source types, parsers, includes, FetchContent declarations, target links, ImGui SDL backends, and binary dependencies must all be absent at final verification. Audio source code is unchanged; only its unused SDL CMake entries are removed.
- Do not claim this work fixes 240 FPS menu-repeat or tick-authored gameplay animations. Those remain separate framerate-domain work.
- Do not deploy an intermediate commit. Deploy only after the final Debug and RelWithDebInfo builds, CTest suites, and SDL audits pass and `game471.exe` is stopped.
- Keep automated evidence separate from manual game acceptance. Only the user can confirm physical input delivery and in-game Test mode.

## Final Target and File Map

| Target/File | Responsibility |
|---|---|
| `src/Input/Types/CMakeLists.txt`, `gc_input_types` | Platform-free input values and digital hysteresis with no config or Win32 dependency. |
| `src/Input/Types/InputTypes.h` | `PhysicalKey`, logical actions, input mode, exact controller identity, and typed digital binding descriptors. |
| `src/Input/Types/PhysicalKey.h/.cpp` | Canonical `sc:hhhh`, `e0:hhhh`, and `e1:hhhh` parsing/formatting. |
| `src/Input/Types/DigitalLatch.h/.cpp` | Reusable 50/40-style press/release hysteresis over normalized `[0,1]` activation. |
| `src/Config/NativeInputConfig.h/.cpp` | Strict native keyboard/controller leaf configuration and backend-specific validation. |
| `src/Config/InputRflParsers.h` | reflect-cpp string representation for `PhysicalKey`. |
| `src/Config/config.h/.cpp`, `config.toml` | Final schema-v2 root contract, `ConfigManager`, strict old-schema rejection, and shipped template. |
| `src/Input/Win32/PhysicalKeyWin32.h/.cpp` | `RAWKEYBOARD` decoding, layout-aware labels, and RFID virtual-key conversion. |
| `src/Input/Win32/Win32InputWindow.h/.cpp` | Worker-owned hidden top-level window, four Raw Input registrations, verification, and teardown. |
| `src/Input/Win32/RawInputRegistrationGuard.h/.cpp` | Runtime-only ownership guard for the four process-global Raw Input usages; compiled into `gc_input`, not the shared ConfigGUI backend. |
| `src/Input/Win32/RawInputPacket.h/.cpp` | Reusable `GetRawInputData` buffer, header/size/type checks, and safe `RAWHID::dwCount` iteration. |
| `src/Input/Win32/ControllerCatalog.h/.cpp` | Exact Raw HID discovery, metadata, `IG_` exclusion, and exact path matching. |
| `src/Input/Win32/HidApi.h/.cpp` | Narrow production/fake function table around `HidP_*` and `HidD_*`. |
| `src/Input/Win32/ControllerStateView.h` | Allocation-free control descriptors and normalized activation view shared by runtime and capture. |
| `src/Input/Win32/RawHidController.h/.cpp` | Cached HID capabilities, multi-report parsing, signed ranges, buttons, values, triggers, and hats. |
| `src/Input/Win32/XInputApi.h/.cpp` | System-DLL loading and injectable `XInputGetState` function table. |
| `src/Input/Win32/XInputController.h/.cpp` | One-slot polling, semantic descriptors, connection state, independent triggers, and reconnect behavior. |
| `src/Input/Win32/ControllerBindingEvaluator.h/.cpp` | Per-binding latches and boolean binding-state output. |
| `src/Input/Win32/InputCapture.h/.cpp` | Neutral-first keyboard/controller capture on the selected exact identity. |
| `src/Input/Polling/InputMapper.h/.cpp` | Keyboard/controller logical aggregation and platform-free FastIO composition. |
| `src/Input/Polling/InputSnapshotState.h/.cpp` | Exact logical-to-FastIO mapping with keyboard and controller source separation. |
| `src/Input/Polling/InputPollingRuntime.h/.cpp` | Lifecycle, worker wait loop, foreground gate, device ownership, diagnostics, and atomic publication. |
| `tools/ConfigGUI/Win32D3D11Host.h/.cpp` | Native window, D3D11 device/swap chain/render target, ImGui frames, resize, and shutdown. |
| `tools/ConfigGUI/InputEditorModel.h/.cpp` | Testable exact-device selection and add/replace/remove binding edits used by the ImGui panel. |
| `tools/ConfigGUI/Main.cpp` | Schema-v2 editor, exact device selector, integer thresholds, and shared capture UI. |
| `src/Rfid/Feature.cpp` | Convert configured `PhysicalKey` to a Win32 virtual key while preserving the existing RFID worker. |
| `src/Driver/iDmac/iDmacDrv32.cpp` | Snapshot-only FastIO register read and worker open/close boundary. |
| `cmake/Dependencies.cmake`, target CMake files | Final target layering, Win32 libraries, ImGui Win32/DX11 backends, and complete SDL removal. |
| `tests/Config/*`, `tests/Input/*` | Strict config, token/label, decoder, HID, XInput, mapping, capture, lifecycle, and CMake/UI regressions. |

Final target dependency direction:

```text
gc_input_types
    |-- gc_config
    |-- gc_input_win32 -- hid/user32
             |-- gc_input -- gc_config/safetyhook/ntdll
             `-- ConfigGUI -- gc_config/imgui/d3d11/dxgi
```

`gc_input_win32` must not link `gc_config`; backend code consumes only platform-free identities and binding descriptors. This prevents a configuration/backend cycle and guarantees ConfigGUI and runtime use the same native interpretation.

---

### Task 1: Add Platform-Free Native Input Values and Hysteresis

**Files:**
- Create: `src/Input/Types/CMakeLists.txt`
- Create: `src/Input/Types/InputTypes.h`
- Create: `src/Input/Types/PhysicalKey.h`
- Create: `src/Input/Types/PhysicalKey.cpp`
- Create: `src/Input/Types/DigitalLatch.h`
- Create: `src/Input/Types/DigitalLatch.cpp`
- Create: `tests/Input/Types/InputTypesTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**
- Produces: `gc_input_types`, `gc::input::PhysicalKey`, `LogicalAction`, `InputMode`, `GameplayInputStyle`, `ControllerIdentity`, `DigitalControlBinding`, `ParsePhysicalKey`, `FormatPhysicalKey`, and `DigitalLatch`.
- Must not include: Windows headers, reflect-cpp, TOML, SDL, plog, or configuration types.

- [ ] **Step 1: Add the failing native-value test target**

Add `InputTypesTests` to `tests/Input/CMakeLists.txt` and cover these exact cases in `tests/Input/Types/InputTypesTests.cpp`:

```cpp
using gc::input::PhysicalKey;
using gc::input::ScanCodePrefix;

expect_key(ParsePhysicalKey("sc:0014"), {0x0014, ScanCodePrefix::None});
expect_key(ParsePhysicalKey("e0:0048"), {0x0048, ScanCodePrefix::E0});
expect_key(ParsePhysicalKey("e1:0045"), {0x0045, ScanCodePrefix::E1});
expect_string(FormatPhysicalKey({0x0014, ScanCodePrefix::None}), "sc:0014");
expect_string(FormatPhysicalKey({0x0048, ScanCodePrefix::E0}), "e0:0048");
expect_string(FormatPhysicalKey({0x0045, ScanCodePrefix::E1}), "e1:0045");

expect_parse_failure("");
expect_parse_failure("t");
expect_parse_failure("sc:014");
expect_parse_failure("SC:0014");
expect_parse_failure("e2:0014");
expect_parse_failure("sc:0000");
expect_parse_failure("sc:zzzz");
expect_parse_failure("sc:10000");
```

Add latch assertions for inactive noise, press exactly at 50 percent, remaining pressed at 40 percent, release below 40 percent, and re-press only at 50 percent. Also assert that constructor validation rejects press/release values outside `0..100` and `release >= press`.

- [ ] **Step 2: Run the focused target and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target InputTypesTests"
```

Expected: configure or compilation fails because `gc_input_types` and the new headers do not exist.

- [ ] **Step 3: Define the exact domain model**

Put these public shapes in `InputTypes.h`:

```cpp
namespace gc::input {

enum class ScanCodePrefix : std::uint8_t { None, E0, E1 };

struct PhysicalKey {
    std::uint16_t make_code{};
    ScanCodePrefix prefix{ScanCodePrefix::None};
    auto operator<=>(const PhysicalKey&) const = default;
};

enum class LogicalAction : std::uint8_t {
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
    Count,
};

enum class InputMode : std::uint8_t { Keyboard, Controller };
enum class GameplayInputStyle : std::uint8_t { Arcade, Switch };
enum class ControllerBackend : std::uint8_t { XInput, RawHid };
enum class ControlDirection : std::uint8_t {
    Positive, Negative, Up, Down, Left, Right,
};
enum class DigitalControlType : std::uint8_t {
    XInputButton,
    XInputAxis,
    XInputTrigger,
    RawHidButton,
    RawHidValue,
    RawHidHat,
};
enum class XInputControl : std::uint8_t {
    A, B, X, Y,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    Start, Back, LeftShoulder, RightShoulder,
    LeftThumb, RightThumb,
    LeftX, LeftY, RightX, RightY,
    LeftTrigger, RightTrigger,
};

struct ControllerIdentity {
    ControllerBackend backend{ControllerBackend::XInput};
    std::string device_id{"0"};
    auto operator<=>(const ControllerIdentity&) const = default;
};

struct DigitalControlBinding {
    LogicalAction action{LogicalAction::LeftBoosterUp};
    DigitalControlType type{DigitalControlType::XInputButton};
    std::optional<XInputControl> control;
    std::optional<ControlDirection> direction;
    std::optional<std::uint32_t> usage_page;
    std::optional<std::uint32_t> usage;
    std::optional<std::uint32_t> link_collection;
    std::optional<std::uint32_t> report_id;
    std::optional<std::int32_t> neutral_value;
    auto operator<=>(const DigitalControlBinding&) const = default;
};

struct KeyboardBinding {
    LogicalAction action{};
    PhysicalKey key{};
};

constexpr bool IsGameplayAction(LogicalAction action) noexcept {
    return action <= LogicalAction::RightBoosterButton;
}

} // namespace gc::input
```

Use `std::expected<PhysicalKey, std::string>` for parsing. Require a lowercase two-character prefix and exactly four hexadecimal digits, accept either case for the digits, reject make code zero, and emit lowercase hexadecimal from `FormatPhysicalKey`.

Define `DigitalLatch` with integer percentage construction and normalized activation updates:

```cpp
class DigitalLatch {
public:
    static std::expected<DigitalLatch, std::string> Create(
        std::uint32_t press_percent,
        std::uint32_t release_percent) noexcept;
    bool Update(double activation) noexcept;
    void Reset() noexcept;
    bool pressed() const noexcept;
private:
    double press_threshold_{};
    double release_threshold_{};
    bool pressed_{};
};
```

Clamp activation to `[0,1]`. An unpressed latch presses at `activation >= press`; a pressed latch releases only at `activation < release`.

- [ ] **Step 4: Add the target before `gc_config` and verify GREEN**

In `src/CMakeLists.txt`, add `add_subdirectory(Input/Types)` immediately after `Nesys/Network` and before `Config`. Define `gc_input_types` from `PhysicalKey.cpp` and `DigitalLatch.cpp`, with `${PROJECT_SOURCE_DIR}/src` as its only public project include.

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target InputTypesTests"
ctest --preset msvc32-debug -R '^InputTypesTests$'
```

Expected: build succeeds and `InputTypesTests` passes.

- [ ] **Step 5: Commit Task 1**

```powershell
git add -- src/Input/Types src/CMakeLists.txt tests/Input/CMakeLists.txt tests/Input/Types/InputTypesTests.cpp
git commit -m "feat: add native input domain types"
```

---

### Task 2: Define the Strict Native Input Configuration Leaves

**Files:**
- Create: `src/Config/InputRflParsers.h`
- Create: `src/Config/NativeInputConfig.h`
- Create: `src/Config/NativeInputConfig.cpp`
- Create: `tests/Config/NativeInputConfigTests.cpp`
- Modify: `src/Config/CMakeLists.txt`
- Modify: `tests/Config/CMakeLists.txt`

**Interfaces:**
- Produces: `gc::config::NativeKeyboardConfig`, `ControllerConfig`, `ValidateNativeInputFields`, and the `PhysicalKey` reflect-cpp representation.
- Does not yet replace the live root `InputConfig`; that atomic cutover occurs in Task 9. This is temporary branch-only coexistence, not shipped compatibility.

- [ ] **Step 1: Add strict configuration tests before the production structs**

Create a test-only root aggregate with the final top-level field names and the production leaf types. Its serialized form must contain this exact input prefix:

```toml
input_schema_version = 2
input_poll_hz = 1000
input_mode = 'Keyboard'
gameplay_input_style = 'Arcade'
axis_press_threshold_percent = 50
axis_release_threshold_percent = 40

[keyboard]
left_booster_up = 'sc:0011'
left_booster_down = 'sc:001f'
left_booster_left = 'sc:001e'
left_booster_right = 'sc:0020'
left_booster_button = 'sc:0039'
right_booster_up = 'e0:0048'
right_booster_down = 'e0:0050'
right_booster_left = 'e0:004b'
right_booster_right = 'e0:004d'
right_booster_button = 'sc:0025'
test = 'sc:0014'
service1 = 'sc:003b'
service2 = 'sc:0017'
service3 = 'sc:0019'
p1_start = 'sc:0002'
p2_start = 'sc:0003'
p2_service = 'sc:003c'
card_read = 'sc:003e'

[controller]
backend = 'XInput'
device_id = '0'
bindings = [
  { action = 'LeftBoosterUp', type = 'XInputButton', control = 'DPadUp' },
  { action = 'LeftBoosterUp', type = 'XInputAxis', control = 'LeftY', direction = 'Negative' },
  { action = 'LeftBoosterDown', type = 'XInputButton', control = 'DPadDown' },
  { action = 'LeftBoosterDown', type = 'XInputAxis', control = 'LeftY', direction = 'Positive' },
  { action = 'LeftBoosterLeft', type = 'XInputButton', control = 'DPadLeft' },
  { action = 'LeftBoosterLeft', type = 'XInputAxis', control = 'LeftX', direction = 'Negative' },
  { action = 'LeftBoosterRight', type = 'XInputButton', control = 'DPadRight' },
  { action = 'LeftBoosterRight', type = 'XInputAxis', control = 'LeftX', direction = 'Positive' },
  { action = 'LeftBoosterButton', type = 'XInputButton', control = 'A' },
  { action = 'RightBoosterUp', type = 'XInputAxis', control = 'RightY', direction = 'Negative' },
  { action = 'RightBoosterDown', type = 'XInputAxis', control = 'RightY', direction = 'Positive' },
  { action = 'RightBoosterLeft', type = 'XInputAxis', control = 'RightX', direction = 'Negative' },
  { action = 'RightBoosterRight', type = 'XInputAxis', control = 'RightX', direction = 'Positive' },
  { action = 'RightBoosterButton', type = 'XInputButton', control = 'B' },
]
```

Test round-trip equality and every validation failure from the design:

- schema version missing or not `2`;
- poll rate not `125`, `250`, `500`, or `1000`;
- press/release outside `0..100` or release greater than/equal to press;
- malformed/zero physical-key token;
- XInput `device_id` other than exactly `"0"`, `"1"`, `"2"`, or `"3"`;
- empty Raw HID path;
- system action in a controller binding;
- binding type/backend disagreement;
- missing or extraneous required fields for each of the six binding types;
- XInput button/axis/trigger using a control from the wrong semantic group;
- Raw HID value without direction and neutral value;
- Raw HID hat with a non-cardinal direction.

- [ ] **Step 2: Run the target and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target NativeInputConfigTests"
```

Expected: compilation fails because the native config files do not exist.

- [ ] **Step 3: Implement reflect-cpp key representation and final leaf structs**

Specialize `rfl::Reflector<gc::input::PhysicalKey>` in `InputRflParsers.h` with `ReflType = std::string`, `ParsePhysicalKey` on input, a zero-key sentinel on malformed input, and `FormatPhysicalKey` on output.

Define `NativeKeyboardConfig` with the exact logical field names and defaults shown above. Define:

```cpp
namespace gc::config {

inline constexpr std::uint32_t kInputSchemaVersion = 2;

struct ControllerConfig {
    rfl::Rename<"backend", gc::input::ControllerBackend>
        backend{gc::input::ControllerBackend::XInput};
    rfl::Rename<"device_id", std::string> device_id{"0"};
    rfl::Rename<"bindings", std::vector<gc::input::DigitalControlBinding>>
        bindings{};
};

std::expected<void, std::string> ValidateNativeInputFields(
    std::uint32_t schema_version,
    std::uint32_t poll_hz,
    std::uint32_t press_percent,
    std::uint32_t release_percent,
    const NativeKeyboardConfig& keyboard,
    const ControllerConfig& controller);

} // namespace gc::config
```

Validation must identify the field or binding index in every error string. It must require all 18 keyboard fields, including `card_read`, to decode to nonzero `PhysicalKey` values. It must enforce exact backend-specific field sets rather than merely checking a subset, and range-check the persisted 32-bit HID address integers before narrowing them to Windows `USHORT`/report-ID fields.

- [ ] **Step 4: Build and run the focused tests**

Link `gc_config` publicly to `gc_input_types`, add `NativeInputConfig.cpp`, and add `NativeInputConfigTests` linked to `gc_config`.

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target NativeInputConfigTests ConfigFeatureTests"
ctest --preset msvc32-debug -R '^(NativeInputConfigTests|ConfigFeatureTests)$'
```

Expected: both tests pass; the existing live SDL schema is still unchanged.

- [ ] **Step 5: Commit Task 2**

```powershell
git add -- src/Config/InputRflParsers.h src/Config/NativeInputConfig.h src/Config/NativeInputConfig.cpp src/Config/CMakeLists.txt tests/Config/NativeInputConfigTests.cpp tests/Config/CMakeLists.txt
git commit -m "feat: define SDL-free input configuration"
```

---

### Task 3: Decode Raw Keyboard Identity and Produce Win32 Labels

**Files:**
- Create: `src/Input/Win32/PhysicalKeyWin32.h`
- Create: `src/Input/Win32/PhysicalKeyWin32.cpp`
- Create: `tests/Input/Win32/PhysicalKeyWin32Tests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**
- Produces: `KeyboardTransition`, `DecodeRawKeyboard`, `PhysicalKeyLabel`, and `PhysicalKeyToVirtualKey` in `gc_input_win32`.
- Consumes only `gc_input_types` plus Win32 APIs.

- [ ] **Step 1: Add synthetic `RAWKEYBOARD` and label tests**

Cover ordinary make/break, typematic repeated make idempotence at the mapper boundary, `RI_KEY_E0`, `RI_KEY_E1`, `KEYBOARD_OVERRUN_MAKE_CODE`, and zero make code. Use these exact identity expectations:

```cpp
expect_transition(raw(0x14, 0), PhysicalKey{0x14, ScanCodePrefix::None}, true);
expect_transition(raw(0x14, RI_KEY_BREAK), PhysicalKey{0x14, ScanCodePrefix::None}, false);
expect_transition(raw(0x48, RI_KEY_E0), PhysicalKey{0x48, ScanCodePrefix::E0}, true);
expect_transition(raw(0x45, RI_KEY_E1), PhysicalKey{0x45, ScanCodePrefix::E1}, true);
expect_no_transition(raw(KEYBOARD_OVERRUN_MAKE_CODE, 0));
expect_no_transition(raw(0, 0));
```

With the active layout, verify nonempty labels for `T`, left/right Control, main Enter, Numpad Enter, and arrow Up. Verify an unknown scan code falls back to the canonical token. Verify `PhysicalKeyToVirtualKey(sc:003e)` returns `VK_F4`, `sc:0014` maps to the layout's T virtual key, and an invalid key maps to zero.

- [ ] **Step 2: Run the focused target and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target PhysicalKeyWin32Tests"
```

Expected: the new target cannot compile.

- [ ] **Step 3: Implement decoder, layout conversion, and fallback**

Use this public contract:

```cpp
struct KeyboardTransition {
    PhysicalKey key{};
    bool pressed{};
};

std::optional<KeyboardTransition> DecodeRawKeyboard(
    const RAWKEYBOARD& keyboard) noexcept;
std::wstring PhysicalKeyLabel(
    PhysicalKey key,
    HKL layout = GetKeyboardLayout(0));
UINT PhysicalKeyToVirtualKey(
    PhysicalKey key,
    HKL layout = GetKeyboardLayout(0)) noexcept;
```

Pass the E0/E1 prefix in the high byte of the scan-code value supplied to `MapVirtualKeyExW(MAPVK_VSC_TO_VK_EX)`. Build the `GetKeyNameTextW` lParam from make code at bits 16-23 plus the extended bit for E0. If naming fails, widen `FormatPhysicalKey(key)` rather than inventing a logical identity.

- [ ] **Step 4: Build and run the tests**

Create `gc_input_win32` in `src/Input/CMakeLists.txt`, initially containing only `PhysicalKeyWin32.cpp`, publicly linking `gc_input_types` and `user32`.

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target PhysicalKeyWin32Tests"
ctest --preset msvc32-debug -R '^PhysicalKeyWin32Tests$'
```

Expected: `PhysicalKeyWin32Tests` passes.

- [ ] **Step 5: Commit Task 3**

```powershell
git add -- src/Input/Win32/PhysicalKeyWin32.h src/Input/Win32/PhysicalKeyWin32.cpp src/Input/CMakeLists.txt tests/Input/Win32/PhysicalKeyWin32Tests.cpp tests/Input/CMakeLists.txt
git commit -m "feat: add native keyboard decoding"
```

---

### Task 4: Add the Hidden Raw Input Window, Packet Reader, and Exact Device Catalog

**Files:**
- Create: `src/Input/Win32/Win32InputWindow.h`
- Create: `src/Input/Win32/Win32InputWindow.cpp`
- Create: `src/Input/Win32/RawInputPacket.h`
- Create: `src/Input/Win32/RawInputPacket.cpp`
- Create: `src/Input/Win32/ControllerCatalog.h`
- Create: `src/Input/Win32/ControllerCatalog.cpp`
- Create: `tests/Input/Win32/Win32InputWindowTests.cpp`
- Create: `tests/Input/Win32/RawInputPacketTests.cpp`
- Create: `tests/Input/Win32/ControllerCatalogTests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**
- Produces: worker-owned Raw Input registration/lifetime, reusable unbuffered packet reading, Raw HID metadata, exact path matching, and XInput-shadow filtering.
- Does not own a thread or poll a controller.

- [ ] **Step 1: Add failing infrastructure tests**

`Win32InputWindowTests` must create the window on the test thread and assert:

- non-null HWND;
- `GetAncestor(hwnd, GA_PARENT) == nullptr` and the HWND is not under `HWND_MESSAGE`;
- style contains `WS_POPUP`, extended style contains `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, and `IsWindowVisible` is false;
- all four requested usage pairs are returned by `GetRegisteredRawInputDevices` with this HWND;
- every registration contains `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY` and contains neither `RIDEV_NOLEGACY` nor `RIDEV_NOHOTKEYS`;
- destruction occurs on the owner thread and unregisters each usage with `RIDEV_REMOVE` and `hwndTarget = nullptr`.

`RawInputPacketTests` must inject a fake `GetRawInputData` function and cover changed size between query/read, short read, bad header size, unsupported type, zero/overflow HID report size/count, and a valid packet with `dwCount == 3` whose three reports are all exposed.

`ControllerCatalogTests` must cover case-insensitive `IG_` detection, non-shadow paths, exact case-insensitive Windows path identity, no substring/friendly-name fallback, and filtering by top-level usages `0x05`, `0x04`, and `0x08` only.

- [ ] **Step 2: Run the three targets and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target Win32InputWindowTests RawInputPacketTests ControllerCatalogTests"
```

Expected: compilation fails for missing classes.

- [ ] **Step 3: Implement the window and registration contract**

Use a non-owning sink interface so the later runtime receives messages without `std::function` allocation:

```cpp
class RawInputMessageSink {
public:
    virtual ~RawInputMessageSink() = default;
    virtual void OnRawInput(HRAWINPUT input) noexcept = 0;
    virtual void OnRawInputDeviceChange(WPARAM change, HANDLE device) noexcept = 0;
};

class Win32InputWindow {
public:
    explicit Win32InputWindow(RawInputMessageSink& sink) noexcept;
    std::expected<void, std::string> Create(HINSTANCE instance);
    void Destroy() noexcept;
    HWND hwnd() const noexcept;
    DWORD owner_thread_id() const noexcept;
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM) noexcept;
};
```

Store `this` through `GWLP_USERDATA` during `WM_NCCREATE`. Forward only `WM_INPUT` and `WM_INPUT_DEVICE_CHANGE`; use `DefWindowProcW` for ordinary lifetime messages. After registration, enumerate effective registrations and fail `Create` if any required usage/target/flag differs.

- [ ] **Step 4: Implement safe packet reading and catalog discovery**

Define a reusable `RawInputPacketBuffer` whose vector grows only when a larger packet arrives:

```cpp
struct RawInputApi {
    decltype(&GetRawInputData) get_raw_input_data{::GetRawInputData};
};

class RawInputPacketBuffer {
public:
    explicit RawInputPacketBuffer(RawInputApi api = {});
    std::expected<const RAWINPUT*, std::string> Read(HRAWINPUT handle);
private:
    RawInputApi api_;
    std::vector<std::byte> bytes_;
};

class HidReportView {
public:
    std::size_t size() const noexcept;
    std::span<const std::byte> operator[](std::size_t index) const noexcept;
};

std::expected<HidReportView, std::string> HidReports(
    const RAWHID& hid) noexcept;
```

Use checked multiplication for `dwSizeHid * dwCount`. `HidReportView` stores only the base span, report size, and count; iterating reports must not allocate. The view remains valid only until the next `RawInputPacketBuffer::Read` call.

Define catalog output:

```cpp
struct RawHidDeviceInfo {
    HANDLE raw_device{};
    std::string device_path;
    std::wstring product_name;
    std::uint16_t vendor_id{};
    std::uint16_t product_id{};
    std::uint16_t usage_page{};
    std::uint16_t usage{};
};

std::expected<std::vector<RawHidDeviceInfo>, std::string>
EnumerateRawHidDevices();
bool IsXInputShadowPath(std::string_view path) noexcept;
const RawHidDeviceInfo* FindExactRawHidDevice(
    std::span<const RawHidDeviceInfo> devices,
    std::string_view configured_path) noexcept;
```

Use `GetRawInputDeviceList`, `RIDI_DEVICEINFO`, and `RIDI_DEVICENAME`; convert the path to UTF-8 without lossy ANSI conversion. Query `HidD_GetProductString` when the path can be opened, but use it only for display. Compare paths with `CompareStringOrdinal(..., TRUE)` and never choose another candidate.

- [ ] **Step 5: Build and run the infrastructure tests**

Add the sources to `gc_input_win32`, link `hid` and `user32`, and register the three tests.

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target Win32InputWindowTests RawInputPacketTests ControllerCatalogTests"
ctest --preset msvc32-debug -R '^(Win32InputWindowTests|RawInputPacketTests|ControllerCatalogTests)$'
```

Expected: all three pass and the test leaves no Raw Input registration behind.

- [ ] **Step 6: Commit Task 4**

```powershell
git add -- src/Input/Win32/Win32InputWindow.h src/Input/Win32/Win32InputWindow.cpp src/Input/Win32/RawInputPacket.h src/Input/Win32/RawInputPacket.cpp src/Input/Win32/ControllerCatalog.h src/Input/Win32/ControllerCatalog.cpp src/Input/CMakeLists.txt tests/Input/Win32/Win32InputWindowTests.cpp tests/Input/Win32/RawInputPacketTests.cpp tests/Input/Win32/ControllerCatalogTests.cpp tests/Input/CMakeLists.txt
git commit -m "feat: add Raw Input window and device discovery"
```

---

### Task 5: Parse Generic HID Controllers into Stable Digital Controls

**Files:**
- Create: `src/Input/Win32/HidApi.h`
- Create: `src/Input/Win32/HidApi.cpp`
- Create: `src/Input/Win32/ControllerStateView.h`
- Create: `src/Input/Win32/RawHidController.h`
- Create: `src/Input/Win32/RawHidController.cpp`
- Create: `tests/Input/Win32/RawHidControllerTests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**
- Produces: cached Raw HID descriptor/state parsing and an allocation-free view consumed by mapping and ConfigGUI capture.
- Uses Windows `HidP_*` semantics through an injectable function table; tests do not require a physical controller.

- [ ] **Step 1: Add fake-HID tests for every required control family**

Build fake preparsed-data/capability fixtures and assert:

| Case | Expected result |
|---|---|
| two button usages in one report | both descriptors update independently |
| button disappears in its next report | only that report's button state clears |
| signed axis range `-32768..32767` | negative and positive activation normalize correctly |
| unsigned trigger range `0..255`, neutral `0` | full press produces `1.0`, neutral `0.0` |
| centered unusual range `100..900`, neutral `500` | each captured direction normalizes away from neutral |
| hat 0, 1, 2, 3 plus null | cardinal directions map correctly and null clears |
| diagonal hat value | both participating cardinal activations are nonzero |
| report ID 1 followed by report ID 2 | ID 2 does not clear ID 1-only controls |
| `dwCount == 3` | all reports apply in order |
| malformed report / `HidP_*` failure | affected controller state clears and an error is returned |
| exact selected path removed | all control state clears |

- [ ] **Step 2: Run the test and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target RawHidControllerTests"
```

Expected: missing HID adapter/controller types prevent compilation.

- [ ] **Step 3: Define the shared state-view contract**

```cpp
struct ControllerControlDescriptor {
    gc::input::DigitalControlBinding binding;
    std::string label;
};

class ControllerStateView {
public:
    virtual ~ControllerStateView() = default;
    virtual const gc::input::ControllerIdentity& identity() const noexcept = 0;
    virtual std::span<const ControllerControlDescriptor>
        controls() const noexcept = 0;
    virtual std::optional<double> Activation(
        const gc::input::DigitalControlBinding& binding) const noexcept = 0;
    virtual std::optional<std::int32_t> RawValue(
        const gc::input::DigitalControlBinding& binding) const noexcept = 0;
};
```

The descriptor vector is built on connect/open and remains stable until disconnect. Packet processing updates only numeric state storage; it must not allocate or rebuild descriptors. `RawValue` returns the current signed logical value for Raw HID value descriptors and `std::nullopt` for XInput/buttons/hats. Capture uses it to record the neutral observed when the modal starts; persisted mapping uses `Activation` with that captured neutral.

- [ ] **Step 4: Wrap HID APIs and cache capabilities once**

In `HidApi.h`, define a trivially copyable function table using `decltype(&GetRawInputDeviceInfoW)`, `decltype(&HidP_GetCaps)`, `decltype(&HidP_GetButtonCaps)`, `decltype(&HidP_GetValueCaps)`, `decltype(&HidP_GetUsages)`, and `decltype(&HidP_GetUsageValue)`. `ProductionHidApi()` returns real addresses; tests supply static fake functions.

`RawHidController::Open` must obtain `RIDI_PREPARSEDDATA`, `HIDP_CAPS`, button caps, and value caps once. Expand range caps into stable address descriptors containing report ID, usage page, usage, and link collection. Use logical min/max from the descriptor and explicit sign extension where the reported value is narrower than 32 bits.

- [ ] **Step 5: Implement report-scoped updates and activation lookup**

Use this lifecycle:

```cpp
class RawHidController final : public ControllerStateView {
public:
    static std::expected<RawHidController, std::string> Open(
        const RawHidDeviceInfo& device,
        HidApi api = ProductionHidApi());
    std::expected<bool, std::string> Apply(
        HANDLE source_device,
        const RAWHID& packet);
    void Clear() noexcept;
    // ControllerStateView overrides
};
```

Return `true` only when observable control state changes. Buttons are boolean. `RawHidValue` uses the binding's direction and captured neutral. `RawHidHat` maps all eight directions plus null while respecting the descriptor's declared range/null value. Ignore `source_device` unless it is the exact selected Raw Input handle. Add `ValidateBinding` so a connected source rejects an unknown HID address or a saved neutral outside that control's logical range; one invalid binding is unavailable without corrupting other valid bindings.

- [ ] **Step 6: Build and run the HID tests**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target RawHidControllerTests"
ctest --preset msvc32-debug -R '^RawHidControllerTests$'
```

Expected: all synthetic descriptor/report cases pass.

- [ ] **Step 7: Commit Task 5**

```powershell
git add -- src/Input/Win32/HidApi.h src/Input/Win32/HidApi.cpp src/Input/Win32/ControllerStateView.h src/Input/Win32/RawHidController.h src/Input/Win32/RawHidController.cpp src/Input/CMakeLists.txt tests/Input/Win32/RawHidControllerTests.cpp tests/Input/CMakeLists.txt
git commit -m "feat: add generic HID controller parsing"
```

---

### Task 6: Add Exact-Slot XInput Polling with Independent Triggers

**Files:**
- Create: `src/Input/Win32/XInputApi.h`
- Create: `src/Input/Win32/XInputApi.cpp`
- Create: `src/Input/Win32/XInputController.h`
- Create: `src/Input/Win32/XInputController.cpp`
- Create: `tests/Input/Win32/XInputControllerTests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**
- Produces: safe system-XInput loading, one exact slot source, semantic control descriptors, and normalized activations.
- Must not link an XInput import library or enumerate/fallback to another slot.

- [ ] **Step 1: Add fake-XInput behavior tests**

Cover slot validation, every supported button/D-pad bit, positive and negative directions for both sticks, LT only, RT only, simultaneous LT+RT, unchanged `dwPacketNumber`, disconnect clearing, low-rate disconnected probe eligibility, reconnect, and proof that a configured slot 2 never calls slot 0.

- [ ] **Step 2: Run the test and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target XInputControllerTests"
```

Expected: compilation fails for missing XInput source files.

- [ ] **Step 3: Implement system-DLL loading without search-path ambiguity**

Define:

```cpp
struct XInputApi {
    HMODULE module{};
    decltype(&XInputGetState) get_state{};
    std::wstring loaded_name;
};

std::expected<XInputApi, std::string> LoadSystemXInput();
void UnloadXInput(XInputApi& api) noexcept;
```

Build absolute paths with `GetSystemDirectoryW`, try `xinput1_4.dll`, then `xinput9_1_0.dll`, and resolve only `XInputGetState`. Report the selected DLL once. Do not call plain `LoadLibraryW` on a relative name.

- [ ] **Step 4: Implement one-slot controller state**

```cpp
class XInputController final : public ControllerStateView {
public:
    static std::expected<XInputController, std::string> Create(
        std::uint32_t slot,
        XInputApi api);
    std::expected<bool, std::string> Poll() noexcept;
    void Clear() noexcept;
    bool connected() const noexcept;
    std::uint32_t slot() const noexcept;
    // ControllerStateView overrides
};
```

Create fixed semantic descriptors for supported controls. Normalize signed stick directions using the full asymmetric signed range, and normalize LT and RT independently from their separate bytes. An unchanged packet number may skip descriptor remapping but must not be used by the later runtime to skip foreground checks or publication duties.

- [ ] **Step 5: Build and run the XInput tests**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target XInputControllerTests"
ctest --preset msvc32-debug -R '^XInputControllerTests$'
```

Expected: all slot, state, trigger, disconnect, and reconnect tests pass.

- [ ] **Step 6: Commit Task 6**

```powershell
git add -- src/Input/Win32/XInputApi.h src/Input/Win32/XInputApi.cpp src/Input/Win32/XInputController.h src/Input/Win32/XInputController.cpp src/Input/CMakeLists.txt tests/Input/Win32/XInputControllerTests.cpp tests/Input/CMakeLists.txt
git commit -m "feat: add XInput controller polling"
```

---

### Task 7: Aggregate Bindings into Logical FastIO and Share Neutral-First Capture

**Files:**
- Create: `src/Input/Win32/ControllerBindingEvaluator.h`
- Create: `src/Input/Win32/ControllerBindingEvaluator.cpp`
- Create: `src/Input/Win32/InputCapture.h`
- Create: `src/Input/Win32/InputCapture.cpp`
- Create: `src/Input/Polling/InputMapper.h`
- Create: `src/Input/Polling/InputMapper.cpp`
- Create: `tests/Input/Polling/InputMapperTests.cpp`
- Create: `tests/Input/Win32/InputCaptureTests.cpp`
- Modify: `src/Input/Polling/InputSnapshotState.h`
- Modify: `src/Input/Polling/InputSnapshotState.cpp`
- Modify: `src/Input/Polling/InputManager.h`
- Modify: `src/Input/Polling/InputManager.cpp`
- Modify: `tests/Input/Polling/InputSnapshotStateTests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`

**Interfaces:**
- Produces: platform-free mapping, per-binding hysteresis, arbitrary binding OR semantics, and shared ConfigGUI capture.
- Keeps the old SDL `InputManager` compiling only until Task 9; it is not part of the final architecture.

- [ ] **Step 1: Add failing mapping/evaluator tests**

Cover all 17 logical-to-FastIO bits, Keyboard versus Controller gameplay mode, system keys in both modes, repeated keyboard make idempotence, one physical key mapped to two logical actions, two controller bindings for one action, release of one while the other remains held, positive/negative axis thresholds, trigger thresholds, diagonal hats, focus clear, disconnect clear, and shutdown clear.

Use this key regression explicitly:

```cpp
mapper.ApplyKeyboardTransition(
    PhysicalKey{0x14, ScanCodePrefix::None}, true);
expect_word(mapper.GetInput(), FastIoBits::TEST_MODE, "T enters Test bit");
mapper.ApplyKeyboardTransition(
    PhysicalKey{0x14, ScanCodePrefix::None}, true);
expect_word(mapper.GetInput(), FastIoBits::TEST_MODE, "repeat make is idempotent");
mapper.ApplyKeyboardTransition(
    PhysicalKey{0x14, ScanCodePrefix::None}, false);
expect_word(mapper.GetInput(), 0, "T break clears Test bit");
```

- [ ] **Step 2: Add failing neutral-first capture tests**

`InputCaptureTests` must prove:

- a key make produces its `PhysicalKey`, while break/repeat does not complete capture;
- controls active when controller capture begins are ignored;
- each ignored control becomes armed only after returning below release threshold;
- the first armed control crossing press wins;
- a captured Raw HID value stores the observed neutral;
- capture rejects a view whose `ControllerIdentity` differs from the selected identity;
- cancel produces no binding;
- XInput labels and generic HID labels come from the shared descriptor, not GUI-local enums.

- [ ] **Step 3: Run the new targets and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target InputMapperTests InputCaptureTests"
```

Expected: missing mapper/evaluator/capture types prevent compilation.

- [ ] **Step 4: Collapse snapshot sources to Keyboard and Controller**

Change `InputSource` to:

```cpp
enum class InputSource : std::uint8_t { Keyboard, Controller, Count };
enum class GameplaySource : std::uint8_t { Keyboard, Controller };
```

Retain the existing FastIO bit constants and exact logical mapping. Update the temporary SDL `InputManager` to OR its own button and axis state per logical action before setting the one Controller source, so the branch remains buildable. Rename Gamepad mode labels to Controller only at the final config cutover.

- [ ] **Step 5: Implement evaluator and mapper contracts**

```cpp
class ControllerBindingEvaluator {
public:
    static std::expected<ControllerBindingEvaluator, std::string> Create(
        std::span<const gc::input::DigitalControlBinding> bindings,
        std::uint32_t press_percent,
        std::uint32_t release_percent);
    std::span<const std::uint8_t> Update(
        const ControllerStateView& view) noexcept;
    void Clear() noexcept;
};

class InputMapper {
public:
    InputMapper(
        gc::input::InputMode mode,
        std::span<const gc::input::KeyboardBinding> keyboard,
        std::span<const gc::input::DigitalControlBinding> controller);
    void ApplyKeyboardTransition(gc::input::PhysicalKey, bool pressed) noexcept;
    void ApplyControllerBindingStates(std::span<const std::uint8_t>) noexcept;
    void ClearKeyboard() noexcept;
    void ClearController() noexcept;
    void ClearAll() noexcept;
    std::uint32_t GetInput() const noexcept;
};
```

`InputMapper` must iterate all matching keyboard bindings rather than use `else if`. Recompute controller pressed counts from the complete evaluator state span so arbitrary binding overlap cannot be erased by an unrelated release.

- [ ] **Step 6: Implement shared capture**

Use one state machine with `BeginKeyboard`, `BeginController(action, identity, initial_view)`, `OnKeyboardTransition`, `SampleController`, `Cancel`, and `TakeResult`. Store an armed bit per stable controller descriptor. At `BeginController`, snapshot each Raw HID value descriptor through `RawValue`, create positive and negative candidates with that exact neutral, and arm them only after movement has returned inside the release threshold. Return this exact once-only shape so the editor can recheck identity at commit time:

```cpp
struct CaptureResult {
    std::optional<gc::input::ControllerIdentity> controller_identity;
    std::variant<gc::input::PhysicalKey, gc::input::DigitalControlBinding>
        value;
};
```

Keyboard results have no controller identity; controller results carry the exact identity supplied to `BeginController`.

- [ ] **Step 7: Build and run state, mapper, and capture tests**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target InputSnapshotStateTests InputMapperTests InputCaptureTests"
ctest --preset msvc32-debug -R '^(InputSnapshotStateTests|InputMapperTests|InputCaptureTests)$'
```

Expected: all three tests pass, including the direct Test-bit regression.

- [ ] **Step 8: Commit Task 7**

```powershell
git add -- src/Input/Win32/ControllerBindingEvaluator.h src/Input/Win32/ControllerBindingEvaluator.cpp src/Input/Win32/InputCapture.h src/Input/Win32/InputCapture.cpp src/Input/Polling/InputMapper.h src/Input/Polling/InputMapper.cpp src/Input/Polling/InputSnapshotState.h src/Input/Polling/InputSnapshotState.cpp src/Input/Polling/InputManager.h src/Input/Polling/InputManager.cpp src/Input/CMakeLists.txt tests/Input/Polling/InputMapperTests.cpp tests/Input/Win32/InputCaptureTests.cpp tests/Input/Polling/InputSnapshotStateTests.cpp tests/Input/CMakeLists.txt
git commit -m "feat: add native input mapping and capture"
```

---

### Task 8: Build the Win32/D3D11 ConfigGUI Host Before Cutover

**Files:**
- Create: `tools/ConfigGUI/Win32D3D11Host.h`
- Create: `tools/ConfigGUI/Win32D3D11Host.cpp`
- Create: `tests/Config/ConfigGuiHostContractTests.cmake`
- Modify: `cmake/Dependencies.cmake`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `tests/Config/CMakeLists.txt`

**Interfaces:**
- Produces: the final native ConfigGUI host as a separately buildable support library.
- Keeps the SDL Main host active only until Task 9 so every intermediate commit still builds.

- [ ] **Step 1: Add a source-contract test for the native host**

The CMake script must fail unless the host source contains:

- `D3D11CreateDeviceAndSwapChain`;
- `ImGui_ImplWin32_Init`, `ImGui_ImplDX11_Init`;
- `ImGui_ImplWin32_NewFrame`, `ImGui_ImplDX11_NewFrame`;
- `ImGui_ImplDX11_RenderDrawData`;
- `WM_SIZE` handling that rebuilds the render target;
- `ImGui_ImplDX11_Shutdown`, `ImGui_ImplWin32_Shutdown`;
- no `SDL` token.

- [ ] **Step 2: Run the contract test and verify RED**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug"
ctest --preset msvc32-debug -R '^ConfigGuiHostContractTests$'
```

Expected: the source-contract test fails because the host does not exist.

- [ ] **Step 3: Compile both ImGui backend sets during the transition**

Temporarily add `imgui_impl_win32.cpp` and `imgui_impl_dx11.cpp` to `imgui` while retaining the two SDL backend sources until Task 10. Add the ImGui backends directory to public includes. This coexistence is build-only and is removed in Task 10.

- [ ] **Step 4: Implement the native host support library**

```cpp
class Win32D3D11Host {
public:
    using MessageHandler = LRESULT (*)(
        void* context, HWND, UINT, WPARAM, LPARAM) noexcept;
    std::expected<void, std::string> Open(
        HINSTANCE instance,
        MessageHandler handler,
        void* context);
    bool PumpMessages() noexcept;
    void BeginFrame() noexcept;
    void Render(const ImVec4& clear_color) noexcept;
    void Close() noexcept;
    HWND window() const noexcept;
    bool quit_requested() const noexcept;
};
```

Own the window class, visible ConfigGUI HWND, D3D11 device/context, DXGI swap chain, render target, ImGui context/backends, resize deferral, and orderly shutdown. Call `ImGui_ImplWin32_WndProcHandler` first, then the supplied native-input handler, then host lifetime handling/`DefWindowProcW`.

- [ ] **Step 5: Build the support library and run the contract test**

Create `gc_config_gui_host` and link it to `imgui`, `d3d11`, `dxgi`, and `user32`; do not switch `Main.cpp` yet.

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target gc_config_gui_host ConfigGUI"
ctest --preset msvc32-debug -R '^ConfigGuiHostContractTests$'
```

Expected: host contract passes and the still-SDL ConfigGUI continues to build.

- [ ] **Step 6: Commit Task 8**

```powershell
git add -- tools/ConfigGUI/Win32D3D11Host.h tools/ConfigGUI/Win32D3D11Host.cpp cmake/Dependencies.cmake tools/ConfigGUI/CMakeLists.txt tests/Config/ConfigGuiHostContractTests.cmake tests/Config/CMakeLists.txt
git commit -m "feat: add Win32 D3D11 ConfigGUI host"
```

---

### Task 9: Atomically Cut Over Configuration, ConfigGUI, RFID, and the Gameplay Runtime

**Files:**
- Modify: `src/Config/config.h`
- Modify: `src/Config/config.cpp`
- Modify: `config.toml`
- Modify: `tests/Config/ConfigFeatureTests.cpp`
- Modify: `tests/Config/ConfigGuiWidgetTests.cmake`
- Modify: `tests/Config/CMakeLists.txt`
- Modify: `tools/ConfigGUI/Main.cpp`
- Create: `tools/ConfigGUI/InputEditorModel.h`
- Create: `tools/ConfigGUI/InputEditorModel.cpp`
- Create: `tests/Config/ConfigGuiInputModelTests.cpp`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `src/Input/Polling/InputPollingRuntime.h`
- Delete: `src/Input/Polling/InputManager.h`
- Delete: `src/Input/Polling/InputManager.cpp`
- Modify: `tests/Input/Polling/InputPollingRuntimeStartupTests.cpp`
- Create: `src/Input/Polling/ForegroundPolicy.h`
- Create: `src/Input/Polling/ForegroundPolicy.cpp`
- Create: `tests/Input/Polling/ForegroundPolicyTests.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `tests/Input/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `src/Rfid/Feature.cpp`
- Modify: `src/Input/Switch/SwitchInputPatch.cpp`
- Modify: `src/Driver/iDmac/iDmacDrv32.cpp`
- Modify: `src/Loader/DllMain.cpp`

**Interfaces:**
- Replaces: live SDL schema and SDL worker with the final native schema and single Win32 worker.
- Preserves: `OpenInputPollingRuntime`, `CloseInputPollingRuntime`, `ReadPublishedInput`, FastIO values, iDmac open-count behavior, Switch policy, RFID thread, and all unrelated configuration sections.
- This task is one atomic commit: do not commit a state where the shipped template, ConfigGUI, and runtime disagree.

- [ ] **Step 1: Rewrite root config fixtures first and verify RED**

Replace the old SDL prefix in `ConfigFeatureTests.cpp` with the exact schema from Task 2. Keep all NESYS, registry, audio, RFID, and experimental assertions. Add explicit failure tests for:

```toml
gamepad_index = 0
axis_threshold = 16384
[gamepad]
```

whether each appears alone or beside schema-v2 fields. The error must contain `obsolete SDL input schema`. Add final round-trip assertions for exact Raw HID paths containing backslashes, multiple bindings for one action, `E0`/`E1` key tokens, and both threshold integers.

Extend `ConfigGuiWidgetTests.cmake` to require `ImGui::InputScalar` for both `Axis press threshold (%)` and `Axis release threshold (%)`, and reject `SliderInt`, `SliderScalar`, or `DragInt` for either field.

Add `ConfigGuiInputModelTests` before implementing the model. It must select and retain an exact XInput identity, select and retain an exact Raw HID path containing backslashes, keep a missing selected identity marked unavailable without fallback, add two bindings to one action, replace only the requested binding index, remove only the requested binding, reject a capture result from a different identity, and serialize/reparse the resulting identity and descriptors without change.

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target ConfigFeatureTests"
```

Expected: compile/test failure against the still-live SDL root schema.

- [ ] **Step 2: Replace the live root schema and `ConfigManager` accessors**

Make `InputConfig` contain these fields in this order before unrelated sections:

```cpp
rfl::Rename<"input_schema_version", std::uint32_t> input_schema_version{2};
rfl::Rename<"input_poll_hz", std::uint32_t> input_poll_hz{1000};
rfl::Rename<"input_mode", gc::input::InputMode>
    input_mode{gc::input::InputMode::Keyboard};
rfl::Rename<"gameplay_input_style", gc::input::GameplayInputStyle>
    gameplay_input_style{gc::input::GameplayInputStyle::Arcade};
rfl::Rename<"axis_press_threshold_percent", std::uint32_t>
    axis_press_threshold_percent{50};
rfl::Rename<"axis_release_threshold_percent", std::uint32_t>
    axis_release_threshold_percent{40};
rfl::Rename<"keyboard", gc::config::NativeKeyboardConfig> keyboard;
rfl::Rename<"controller", gc::config::ControllerConfig> controller;
```

Delete `GamepadConfig`, all SDL getters, and old axis/index fields. Add const-reference keyboard/controller getters plus scalar getters for mode, poll rate, thresholds, style, and card-read `PhysicalKey`. Call `ValidateNativeInputFields` from `ValidateInputConfig`. Before reflect-cpp parsing, use toml++ syntax inspection to require an integer `input_schema_version` equal to `2` and to emit the explicit obsolete-schema error for old top-level keys or `[gamepad]`; do not rely on C++ member initializers to make the schema version required.

Update `config.toml` with the exact schema-v2 template from Task 2 while preserving every unrelated section/value.

- [ ] **Step 3: Port ConfigGUI Main to the native host and final schema**

Remove SDL includes, objects, initialization, events, logging, renderer calls, gamepad opening/index fallback, and SDL binding helpers from `Main.cpp`. Instantiate `Win32D3D11Host`, register Raw Input for keyboard/gamepad/joystick/multi-axis on its visible HWND without suppression flags, enumerate XInput slots and Raw HID candidates through `gc_input_win32`, and drive `InputCapture` from the Win32 message handler plus per-frame selected-XInput polling.

The UI must provide:

- explicit `Keyboard` / `Controller` input mode;
- fixed poll-rate combo for 125/250/500/1000;
- exact integer `InputScalar` fields for press/release percentages;
- XInput group showing slots 0-3 and connection state;
- Generic Raw HID group showing friendly name, VID/PID, usage, and exact path;
- configured-but-missing identity retained and visibly marked unavailable;
- keyboard rows labeled by `PhysicalKeyLabel`;
- controller rows grouped by logical action with Add, Replace, and Remove;
- capture modal that uses shared neutral-first capture and accepts only the selected identity;
- validation errors inline and Save disabled until the production validator succeeds.

Use standard output/plog/Win32 dialogs for errors; do not introduce a replacement media/window library.

Keep device-selection and binding-list mutations out of ImGui callbacks by implementing `InputEditorModel` with `SelectIdentity`, `SetAvailableIdentities`, `AddBinding`, `ReplaceBinding`, `RemoveBinding`, and `AcceptCapture`. The model edits `gc::config::ControllerConfig`; `Main.cpp` renders it. Put it in a `gc_config_gui_model` static target and link `ConfigGuiInputModelTests` to that model plus `gc_config` and `gc_input_win32`.

- [ ] **Step 4: Add foreground-policy tests and implement the pure policy seam**

`ForegroundPolicyTests` supplies fake `GetForegroundWindow`/`GetWindowThreadProcessId` results and verifies current PID accepted, different PID rejected, null foreground rejected, and transitions request one clear/publication rather than repeated logs.

```cpp
struct ForegroundApi {
    decltype(&GetForegroundWindow) get_foreground_window;
    decltype(&GetWindowThreadProcessId) get_window_thread_process_id;
    DWORD current_process_id;
};

bool IsCurrentProcessForeground(const ForegroundApi&) noexcept;
```

- [ ] **Step 5: Replace `InputPollingRuntime.cpp` with the one-worker Win32 loop**

Use `std::atomic<std::uint32_t> g_published_input{0}` and a mutex-protected singleton lifecycle containing open count, `std::thread`, stop event, startup condition/result, and no SDL object. On the worker:

1. receive the already-created lifecycle stop event, then call `PeekMessageW` once to establish the queue;
2. request `THREAD_PRIORITY_ABOVE_NORMAL` and log the result once;
3. create `Win32InputWindow` and verify all four effective registrations;
4. load the final immutable config, create keyboard bindings, `InputMapper`, the selected controller source, and `ControllerBindingEvaluator`;
5. create a periodic waitable timer at `1000 / input_poll_hz` ms, preferring `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` and retrying without it on unsupported systems;
6. publish zero and signal startup success;
7. wait with `MsgWaitForMultipleObjectsEx` on stop, timer, and `QS_ALLINPUT | MWMO_INPUTAVAILABLE`;
8. dispatch all window messages, process every `WM_INPUT` immediately, and poll only the selected XInput slot on timer ticks;
9. check foreground process ID on every Raw Input and timer wake;
10. compose and publish one complete word after every changed path;
11. on stop or unexpected exit, clear sources, publish zero, destroy/unregister on the worker, close handles, and return a terminal result.

`OpenInputPollingRuntime` creates the manual-reset stop event before launching the worker so final close can always signal it; failure to create that handle fails synchronously. Raw keyboard packets call `DecodeRawKeyboard` and `InputMapper::ApplyKeyboardTransition`. Background packets are drained but never arm state. Raw HID packets are accepted only when their exact selected path/handle matches and are applied report-by-report. `WM_INPUT_DEVICE_CHANGE` clears removal state and reopens only the configured exact path on arrival. Any device change forces an immediate XInput reconnect probe; otherwise a disconnected slot is probed no more than once per second.

Failure to load XInput, an absent configured slot/path, unavailable HID preparsed data, or a controller binding unavailable on the connected descriptor disables only controller gameplay input and logs the exact reason. It must not fail the worker or prevent keyboard/system inputs. Failure of the hidden window, stop/timer setup, or any required effective Raw Input registration remains a startup failure.

One-time/transition logs must include worker ID/priority, hidden HWND/owner, requested/effective registrations, mode/rate/thresholds/Test token and label, exact controller identity, HID metadata/cap counts, XInput DLL/slot/connect state, focus transitions, and device match transitions. Parsing failures are rate-limited. Logical transitions and published words are debug-only. There is no per-poll or per-register log.

- [ ] **Step 6: Update runtime lifecycle/startup tests**

Rewrite `InputPollingRuntimeStartupTests` to expect a hidden top-level target rather than a message-only window and to verify all four effective registrations. Open twice, prove the same target remains after the second open, close once and prove it remains, close finally and prove the registrations are removed. Confirm initial/final published words are zero and final close completes under the existing five-second timeout.

Keep the direct T-path proof split across `PhysicalKeyWin32Tests` (synthetic `RAWKEYBOARD` to `PhysicalKey`) and `InputMapperTests` (`PhysicalKey` to `TEST_MODE`). Do not add a test-only message route to the production runtime.

- [ ] **Step 7: Preserve RFID while switching it to `PhysicalKey`**

In `Rfid/Feature.cpp`, replace `SdlKeycodeToVirtualKey` and `KeycodeToString` with `PhysicalKeyToVirtualKey` and `PhysicalKeyLabel`/`FormatPhysicalKey`. Link `gc_rfid_feature` to `gc_input_win32` in `src/CMakeLists.txt`. Keep `Rfid/Runtime.cpp`, its detached worker, edge behavior, and 100 ms sleep unchanged. A conversion failure disables only card scan and logs the physical token.

Update config tests so `sc:003e` maps to `VK_F4`, `sc:0014` maps to the T virtual key, an invalid physical key maps to zero, and punctuation/layout labels are presentation-only.

- [ ] **Step 8: Update Switch enum use and remove register-read diagnostics**

Change `SwitchInputPatch.cpp` to use `gc::input::GameplayInputStyle`. Remove the now-deleted `InputManager.h` include from `DllMain.cpp` while leaving its transitional unused SDL include for the mechanical Task 10 audit. In `iDmacDrv32.cpp`, delete the temporary `last_test_bit` block so `FIO_NODE_0_INPUT` is exactly:

```cpp
case RegisterReadType::FIO_NODE_0_INPUT:
    result = gc::input::ReadPublishedInput();
    break;
```

- [ ] **Step 9: Build all cutover targets and run focused tests**

Update target sources/links so `gc_input` uses `InputMapper`, `InputPollingRuntime`, `InputSnapshotState`, Switch sources, `gc_config`, and `gc_input_win32`; remove the deleted SDL `InputManager` sources. Link `ConfigGUI` to `gc_config_gui_host`, `gc_input_win32`, `gc_config`, ImGui, D3D11/DXGI/user32, while SDL may remain only as a transitional CMake dependency until Task 10.

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target iDmacDrv32 ConfigGUI ConfigFeatureTests NativeInputConfigTests ConfigGuiInputModelTests InputSnapshotStateTests InputMapperTests InputPollingRuntimeStartupTests ForegroundPolicyTests InputCaptureTests"
ctest --preset msvc32-debug -R '^(ConfigFeatureTests|NativeInputConfigTests|ConfigGuiWidgetTests|ConfigGuiInputModelTests|InputSnapshotStateTests|InputMapperTests|InputPollingRuntimeStartupTests|ForegroundPolicyTests|InputCaptureTests)$'
```

Expected: every target builds and every focused test passes against schema v2 and the native worker.

- [ ] **Step 10: Commit the atomic cutover**

```powershell
git add -- src/Config/config.h src/Config/config.cpp config.toml tests/Config/ConfigFeatureTests.cpp tests/Config/ConfigGuiWidgetTests.cmake tests/Config/CMakeLists.txt tools/ConfigGUI/Main.cpp tools/ConfigGUI/InputEditorModel.h tools/ConfigGUI/InputEditorModel.cpp tests/Config/ConfigGuiInputModelTests.cpp tools/ConfigGUI/CMakeLists.txt src/Input/Polling/InputPollingRuntime.cpp src/Input/Polling/InputPollingRuntime.h src/Input/Polling/ForegroundPolicy.h src/Input/Polling/ForegroundPolicy.cpp tests/Input/Polling/InputPollingRuntimeStartupTests.cpp tests/Input/Polling/ForegroundPolicyTests.cpp src/Input/CMakeLists.txt tests/Input/CMakeLists.txt src/CMakeLists.txt src/Rfid/Feature.cpp src/Input/Switch/SwitchInputPatch.cpp src/Driver/iDmac/iDmacDrv32.cpp src/Loader/DllMain.cpp
git add -u -- src/Input/Polling/InputManager.h src/Input/Polling/InputManager.cpp
git commit -m "feat: run gameplay input on Win32 backends"
```

---

### Task 10: Remove SDL from CMake and Prove the Final Build Graph Is SDL-Free

**Files:**
- Modify: `cmake/Dependencies.cmake`
- Modify: `src/Config/CMakeLists.txt`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp`
- Delete: `src/Config/SdlRflParsers.h`
- Delete: `src/Platform/Win32/KeyMapping.h`

**Interfaces:**
- Removes: the SDL FetchContent project, SDL ImGui backends, every SDL include/link, obsolete parser/mapping headers, and unused loader include.
- Does not change: any audio `.cpp` file, input behavior, configuration behavior, or deployment files.

- [ ] **Step 1: Add a failing source/build-graph SDL audit**

Run before removal:

```powershell
rg -n -i 'SDL3|SDL_' CMakeLists.txt cmake src tests tools config.toml
```

Expected: matches remain in `cmake/Dependencies.cmake`, target CMake files, `SdlRflParsers.h`, `KeyMapping.h`, and/or the unused loader include. Test source references must already be gone after Task 9.

- [ ] **Step 2: Remove only SDL dependency wiring and obsolete source**

Delete the SDL `FetchContent_Declare`, cache options, and `FetchContent_MakeAvailable`. Make ImGui contain core sources, `imgui_impl_win32.cpp`, `imgui_impl_dx11.cpp`, and `imgui_stdlib.cpp` only. Remove `${SDL3_SOURCE_DIR}/include` and `SDL3-static` from `gc_config`, `gc_input`, `gc_audio`, `ConfigGUI`, and `iDmacDrv32`. Keep all audio production sources untouched.

Delete `SdlRflParsers.h` and `KeyMapping.h` after confirming `rg` has no consumers. Remove the unused SDL include from `DllMain.cpp`.

- [ ] **Step 3: Reconfigure and build full Debug and RelWithDebInfo trees**

```powershell
$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --fresh --preset msvc32-debug && cmake --build --preset msvc32-debug"
cmd.exe /d /s /c "call `"$vcvars32`" && cmake --fresh --preset msvc32-release && cmake --build --preset msvc32-release"
```

Expected: both complete builds succeed.

- [ ] **Step 4: Run the complete test suite in both configurations**

```powershell
ctest --preset msvc32-debug
ctest --preset msvc32-release
```

Expected: zero failed tests in both presets.

- [ ] **Step 5: Audit source, generated graph, and binary imports**

```powershell
$sourceMatches = rg -n -i 'SDL3|SDL_' CMakeLists.txt cmake src tests tools config.toml
if ($LASTEXITCODE -eq 0) { $sourceMatches; throw 'SDL source/CMake reference remains' }

$debugGraph = cmake --build --preset msvc32-debug --target help | Select-String -Pattern '(?i)sdl'
if ($debugGraph) { $debugGraph; throw 'SDL target remains in Debug graph' }

$releaseGraph = cmake --build --preset msvc32-release --target help | Select-String -Pattern '(?i)sdl'
if ($releaseGraph) { $releaseGraph; throw 'SDL target remains in Release graph' }

$vcvars32 = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat'
$dllSdlImports = cmd.exe /d /s /c "call `"$vcvars32`" && dumpbin /imports build-msvc32-release\dist\iDmacDrv32.dll" |
    Select-String -Pattern '(?i)sdl'
if ($dllSdlImports) { $dllSdlImports; throw 'SDL import remains in iDmacDrv32.dll' }

$guiSdlImports = cmd.exe /d /s /c "call `"$vcvars32`" && dumpbin /imports build-msvc32-release\dist\ConfigGUI.exe" |
    Select-String -Pattern '(?i)sdl'
if ($guiSdlImports) { $guiSdlImports; throw 'SDL import remains in ConfigGUI.exe' }
```

Expected: every audit has zero SDL matches. Also run `dumpbin /headers` on both files and confirm machine `14C (x86)`.

- [ ] **Step 6: Review the final diff for scope and hot-path regressions**

```powershell
git status --short
git diff --check
git diff --stat 9914fc0
rg -n 'GetAsyncKeyState|GetKeyState|GetKeyboardState|DirectInput|GameInput|RIDEV_NOLEGACY|RIDEV_NOHOTKEYS' src tools
rg -n 'ReadPublishedInput|FIO_NODE_0_INPUT' src/Driver/iDmac/iDmacDrv32.cpp src/Input/Polling
```

Expected:

- only the separate RFID runtime uses `GetAsyncKeyState`;
- no DirectInput/GameInput or suppression flag appears in gameplay input;
- the register read remains one atomic snapshot load;
- no unrelated untracked file is staged;
- `git diff --check` is clean.

- [ ] **Step 7: Commit Task 10**

```powershell
git add -- cmake/Dependencies.cmake src/Config/CMakeLists.txt src/Input/CMakeLists.txt src/Audio/CMakeLists.txt src/CMakeLists.txt tools/ConfigGUI/CMakeLists.txt src/Loader/DllMain.cpp
git add -u -- src/Config/SdlRflParsers.h src/Platform/Win32/KeyMapping.h
git commit -m "build: remove SDL dependency"
```

---

## Operator Runtime Acceptance Checkpoint

This checkpoint changes runtime files but creates no source commit. Stop after deployment and wait for the user's test report before designing native-input blocking or returning to menu-repeat timing.

- [ ] **Step 1: Confirm the game is stopped and record source hashes**

```powershell
if (Get-Process game471 -ErrorAction SilentlyContinue) {
    throw 'game471.exe must be stopped before deployment'
}
Get-FileHash 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll' -Algorithm SHA256
Get-FileHash 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\ConfigGUI.exe' -Algorithm SHA256
```

- [ ] **Step 2: Back up runtime files and deploy binaries**

Create a timestamped directory under `H:\gc\deploy-backups`, copy the current DLL, ConfigGUI, and config there, then copy the two RelWithDebInfo binaries into `H:\gc`. Do not place the backup inside the git repo.

- [ ] **Step 3: Upgrade only the runtime input fields**

Edit `H:\gc\config.toml` so its input prefix, `[keyboard]`, and `[controller]` match the new template. Preserve the operator's existing `[nesys]`, `[registry]`, `[experimental]`, audio, RFID, and other unrelated settings. This is an explicit config replacement, not a loader compatibility path.

- [ ] **Step 4: Verify deployed hashes and launch readiness**

```powershell
Get-FileHash 'H:\gc\iDmacDrv32.dll' -Algorithm SHA256
Get-FileHash 'H:\gc\ConfigGUI.exe' -Algorithm SHA256
```

Expected: hashes match the release artifacts. ConfigGUI loads, displays exact device identity, saves, and reloads the same schema-v2 bindings.

- [ ] **Step 5: User performs physical runtime acceptance**

Required observations:

- startup logs show one gameplay worker, its hidden top-level window, all four effective registrations, configured poll rate/thresholds, and the Test token/label;
- configured T produces raw make, logical Test, `TEST_MODE`, and entry into Test mode;
- ordinary remapped keyboard boosters now work through FastIO;
- held/released keys are stable, focus loss publishes zero, and background keyboard packets do not arm state;
- the game's existing native F1 behavior remains unchanged in this phase;
- selected Raw HID buttons, hat/D-pad, axes, and triggers work; an unselected device is ignored;
- removal clears state, another path is not substituted, and the exact path reconnects;
- selected XInput slot buttons, D-pad, both sticks, LT, RT, and simultaneous LT+RT work;
- a missing selected controller never disables keyboard/system input;
- input remains responsive without duplicate or stuck transitions at both 60 and 240 FPS.

Record high-framerate menu-repeat behavior separately. It is evidence for the later framerate-timing plan and is not a failure of this acquisition plan unless physical state itself duplicates or sticks.

## Execution Stop Conditions

- If a focused test fails for a reason outside the task's expected RED state, stop and diagnose before adding more code.
- If full builds pass but physical Test input still never reaches the log, collect `loader-log.txt` from that exact binary/config and inspect the requested/effective Raw Input registrations and foreground/device transitions before changing architecture.
- If keyboard works but one controller family fails, keep keyboard and the other backend intact; diagnose only the selected backend with its fake tests and transition logs.
- If any experiment affects the game's native F1/direct input path, revert that experiment immediately because native blocking is outside this plan.
- Do not proceed to native keyboard blocking or 240 FPS menu-repeat hooks until the user accepts loader keyboard and controller acquisition.
