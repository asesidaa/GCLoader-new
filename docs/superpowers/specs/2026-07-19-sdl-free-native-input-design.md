# SDL-Free Native Input Design

Date: 2026-07-19

## Context

GCLoader emulates the FastIO input word consumed by Groove Coaster. The
asynchronous snapshot boundary introduced by the earlier input-polling work is
correct: physical input is acquired independently of game register reads, and
`iDmacDrvRegisterRead(FIO_NODE_0_INPUT)` returns one already-composed atomic
snapshot. The SDL acquisition layer behind that boundary is not reliable in
the injected 32-bit game process, however. Both the attached-window approach
and SDL's Windows raw-keyboard mode initialized successfully but produced no
loader keyboard transitions at runtime. As a result, ordinary controls seen
during testing came from the game's own native keyboard path and the loader's
configured Test key never reached the FastIO snapshot.

The input acquisition layer will be replaced with native Windows APIs:

- Raw Input for keyboards and generic HID gamepads/joysticks.
- XInput for Xbox-class controllers so both triggers remain independently
  observable.
- One hidden Win32 window owned by the existing gameplay input worker.
- ImGui's Win32 and Direct3D 11 backends for ConfigGUI.

SDL will be removed from the CMake dependency graph. This is not an audio
redesign: the audio sources do not use SDL and remain unchanged when their
unnecessary SDL include/link entries are removed.

This design supersedes the SDL-specific acquisition, lifetime, configuration,
and ConfigGUI sections of
`2026-07-15-asynchronous-input-polling-design.md`. It retains that design's
logical-input model, lock-free FastIO publication, worker ownership, supported
polling rates, system-key policy, RFID separation, and register-read boundary.

## Goals

- Make configured keyboard input, including the Test key, enter the loader's
  FastIO snapshot without depending on focus delivery to an SDL window.
- Use Raw Input scan codes as keyboard identity while showing layout-aware
  logical labels in ConfigGUI.
- Support buttons, axes, triggers, and hats from arbitrary HID game
  controllers as digital logical inputs.
- Use XInput for Xbox controllers to preserve independent LT and RT state.
- Keep all latency-sensitive input acquisition, mapping, and publication on
  one gameplay input thread.
- Keep Raw Input event delivery and XInput polling synchronized through one
  owner loop and one publication point.
- Select one exact controller identity and never silently fall back to a
  different device.
- Preserve the existing foreground gate, gameplay-input modes, logical
  booster mapping, and atomic FastIO read path.
- Preserve the game's existing native keyboard messages during this phase.
- Move ConfigGUI to ImGui Win32 plus Direct3D 11 and make it the authoritative
  way to select devices and capture mappings.
- Remove SDL source types, parsers, FetchContent declaration, include paths,
  and link dependencies from the repository build.
- Ship a new input configuration template with no SDL-schema compatibility.

## Non-Goals

- Blocking or hooking the game's native DirectInput/keyboard path. That is a
  separately designed phase after loader input passes runtime acceptance.
- Changing the game's menu-repeat, held-edge, judgment, or other authored
  60 Hz behavior. Menu scrolling at high frame rates remains a separate
  framerate-domain concern.
- Moving RFID/card-read polling into the gameplay input worker. RFID is not
  latency-sensitive and retains its separate worker.
- Supporting vibration, force feedback, LEDs, audio endpoints, touchpads,
  gyro, or other controller output/specialized features.
- Preserving or automatically migrating the old SDL-based input fields.
- Giving analog values to the emulated board. Every physical control is
  reduced to a logical pressed/released result.
- Selecting a particular physical keyboard. Scan-code bindings accept the
  matching key from any attached keyboard.
- Associating an XInput slot with its Raw Input shadow device. Windows does
  not expose a reliable association, and the shadow device is deliberately
  ignored.

## Required Behavior Preserved from the Current Input Model

The ten gameplay logical inputs remain:

- Left Booster Up, Down, Left, Right, and Button.
- Right Booster Up, Down, Left, Right, and Button.

The keyboard-only system inputs remain Service 1, Service 2, Service 3,
P1 Start, P2 Start, P2 Service, and Test. They work in both Keyboard and
Controller gameplay modes. Card Read remains a separate keyboard-configured
RFID action.

The established logical-to-FastIO mapping in `InputSnapshotState` remains
authoritative. Multiple physical sources for one logical action are ORed; a
release from one source does not clear another source that remains held.

## Architecture

### Shared native input model

Create backend-independent value types shared by the runtime, configuration,
tests, and ConfigGUI:

- `PhysicalKey`: Raw Input make code plus `None`, `E0`, or `E1` prefix.
- `ControllerBackend`: `XInput` or `RawHid`.
- `ControllerIdentity`: backend tag plus an XInput slot or exact Raw Input
  device-interface path.
- `LogicalAction`: the existing logical booster/system action names.
- `DigitalControlBinding`: a tagged button, axis direction, trigger, or hat
  direction descriptor.
- `DigitalControlState`: pressed/released state after thresholding.

SDL keycodes, gamepad enums, joystick IDs, and SDL parsers must not cross this
model. The logical state and FastIO composition remain platform-free.

### Runtime components

The native input runtime is divided into focused components:

- `InputPollingRuntime` owns open/close reference counting, startup
  synchronization, the worker, the stop event, and the published atomic word.
- `Win32InputWindow` registers a private window class, creates the hidden
  top-level window, registers Raw Input, and dispatches window messages.
- `RawKeyboardSource` decodes `RAWKEYBOARD` make/break transitions into
  `PhysicalKey` state.
- `RawHidSource` enumerates the selected HID controller, caches its
  descriptor/capabilities, parses `RAWHID` reports, and detects device changes.
- `XInputSource` loads XInput, probes the configured slot, and samples its
  current state.
- `InputMapper` matches physical source states to logical actions, applies
  digital thresholds/hysteresis, and feeds `InputSnapshotState`.

These may be separate classes/files, but they execute on the same runtime
worker. No Raw Input callback thread, controller hotplug thread, timer callback
thread, or DirectInput thread is permitted.

### Hidden Win32 window

The gameplay worker creates an unshown top-level `WS_POPUP` window with
`WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`. It is not the GameWare window, never
takes activation, and is destroyed on its owner thread. The runtime does not
subclass, replace, or hook the game's window procedure.

The window registers these Generic Desktop usages:

- Keyboard (`0x01/0x06`).
- Game Pad (`0x01/0x05`).
- Joystick (`0x01/0x04`).
- Multi-axis Controller (`0x01/0x08`).

Registration uses `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`. It must not use
`RIDEV_NOLEGACY`, `RIDEV_NOHOTKEYS`, or any other flag that suppresses input
visible to the game. Startup logs the requested and effective registrations
using `GetRegisteredRawInputDevices` so a missing or overwritten registration
is diagnosable.

The normal, unbuffered `WM_INPUT` path calls `GetRawInputData` for each message.
This avoids the documented/example x86-on-x64 alignment complexity of
`GetRawInputBuffer` and is sufficient for the expected device rates. HID
packets must iterate every `RAWHID::dwCount` report rather than assuming one
report per message.

### Single worker loop

The worker raises itself to `THREAD_PRIORITY_ABOVE_NORMAL`, initializes its
window and input sources, publishes zero, and signals startup readiness. It
then waits with `MsgWaitForMultipleObjectsEx` on:

- The runtime stop event.
- The XInput polling timer.
- The worker message queue containing `WM_INPUT`,
  `WM_INPUT_DEVICE_CHANGE`, and ordinary window lifetime messages.

Raw Input messages are drained and applied immediately. A waitable timer fires
at the configured 125, 250, 500, or 1000 Hz rate for XInput sampling and the
foreground check. All wake paths finish by composing and, when changed,
publishing one complete FastIO word. There is no second source publication
path and no input processing in `iDmacDrvRegisterRead`.

The worker uses a high-resolution waitable timer when the running Windows
version supports it and a normal waitable timer otherwise. The supported
rates all have integral millisecond periods. Raw HID input remains event-driven
at the device/driver report rate; it is not delayed until the next XInput tick.

### Foreground policy

`RIDEV_INPUTSINK` is required so the private window reliably receives Raw
Input, but background input must never enter the emulated board. On every Raw
Input message and timer tick, the worker compares the foreground window's
process ID with the current process. When they differ, it clears gameplay,
system-key, Raw HID, and XInput states and publishes zero.

Background Raw Input packets are drained but do not arm logical state. A key
held continuously across focus loss therefore requires a new transition after
focus returns. XInput is state-polled, so a controller held across focus return
may become active on the next poll; this preserves the current gamepad
behavior. No foreground check calls into the game's input buffers.

### Atomic publication and lifecycle

The SDL atomic word becomes `std::atomic<std::uint32_t>`. The stop state and
lifecycle synchronization likewise use standard C++ atomics/mutexes plus
Win32 events where a waitable handle is required.

The first `iDmacDrvOpen` starts the worker outside `DllMain` and waits for its
startup result. Later opens reuse it. Final close signals the stop event, joins
the worker, publishes zero, and releases handles. Window destruction, Raw Input
unregistration, and controller cleanup occur on the worker. Process teardown
may still rely on Windows cleanup, matching the existing loader boundary.

## Keyboard Acquisition and Labels

`RAWKEYBOARD::MakeCode` plus `RI_KEY_E0`/`RI_KEY_E1` form the stored physical
identity. Make sets the source, break clears it, and typematic repeated make
packets are idempotent. Left/right modifiers, main Enter/Numpad Enter, and
navigation/numpad distinctions are preserved.

ConfigGUI turns a stored `PhysicalKey` into a friendly label using the active
Windows keyboard layout and `GetKeyNameTextW`/`MapVirtualKeyExW`. The label is
presentation only: changing keyboard layout does not change the captured
physical binding. If Windows cannot name a scan code, the GUI displays a
canonical hexadecimal scan-code label.

The RFID card-read worker remains separate. Its configured `PhysicalKey` is
converted to the corresponding Win32 virtual key at startup for the existing
low-frequency `GetAsyncKeyState` path. Failure to convert disables only the
card-read hotkey and is logged clearly.

## Generic HID Controller Acquisition

### Discovery and exact identity

Raw HID candidates are enumerated with `GetRawInputDeviceList` and
`GetRawInputDeviceInfo`. Only registered gamepad, joystick, and multi-axis
top-level collections are offered. The exact `RIDI_DEVICENAME` interface path
is the persisted identity. Friendly product name, VID, PID, usage, and usage
page are display/diagnostic metadata and never substitute for identity.

Raw Input exposes a compatibility HID device for Xbox controllers. Candidates
whose device path identifies an XInput shadow (`IG_`) are excluded from the
Raw HID device list and ignored when packets arrive. They are handled only by
XInput, preventing duplicated buttons and the legacy combined-trigger axis.

If the selected path is absent, runtime startup succeeds with that controller
inactive. Another HID controller is never substituted. On
`WM_INPUT_DEVICE_CHANGE`, removal clears all controller-derived state. Arrival
reopens the source only when the exact configured path is present again.

### Descriptor cache and report parsing

When the selected device connects, the source obtains and caches
`RIDI_PREPARSEDDATA`, `HIDP_CAPS`, button capabilities, and value
capabilities. It builds stable internal control descriptors using report ID,
usage page, usage, and link collection. No capability allocation or descriptor
enumeration occurs for each packet.

Reports are interpreted through the Windows `HidP_*` parser. The
implementation must use the descriptor's logical range and correct sign
extension; it must not assume every value range is `[0, 2^bitSize - 1]`.
Devices with multiple report IDs update only controls contained in the current
report instead of clearing unrelated controls.

Raw HID binding capture records the exact control address and activation:

- Button usage.
- Positive or negative movement of a value usage.
- Trigger movement from its captured neutral state.
- Hat direction.

All values are normalized only long enough to determine digital state. No
analog value crosses the logical input boundary.

## XInput Controller Acquisition

The selected identity is the backend tag plus XInput user slot `0` through
`3`. XInput does not expose a persistent physical device identifier beyond the
slot, so this is the exact identity available from that API and is shown
explicitly in ConfigGUI.

The runtime dynamically loads `XInputGetState`, preferring the system XInput
1.4 DLL and falling back to the system XInput 9.1.0 DLL. Only the selected slot
is sampled. Buttons and D-pad bits map directly; stick directions and the two
independent trigger bytes pass through the shared digital normalizer.

A connected slot is sampled at `input_poll_hz`. A disconnected selected slot
is cleared immediately and probed at a low rate, with a device-change message
triggering an immediate probe. The runtime may skip remapping an unchanged
`dwPacketNumber`, but it still performs foreground and publication duties.

No XInput output API is required.

## Digital Normalization and Logical Mapping

Every controller binding resolves to a boolean source:

- Buttons are pressed when their bit/usage is active.
- A signed axis binding chooses its positive or negative direction.
- A trigger binding chooses movement away from its neutral value.
- A hat binding chooses one cardinal direction; diagonal values activate both
  participating cardinal directions when both are bound.

The new template stores integer percentage thresholds:

- `axis_press_threshold_percent = 50` by default.
- `axis_release_threshold_percent = 40` by default.

The release threshold must be lower than the press threshold. The gap provides
hysteresis so drift/noise cannot chatter the logical bit. ConfigGUI exposes
exact integer input fields. Normalization uses each control's declared range;
Raw HID capture also records the neutral value needed for an unipolar or
unusually centered control.

Each logical gameplay action accepts zero or more controller bindings. This
preserves the existing ability to use a stick and D-pad for the same booster
direction. Binding states are tracked separately and ORed at the logical
boundary.

## New Configuration Contract

There is no compatibility parser or migration for the SDL schema. The shipped
template and ConfigGUI require `input_schema_version = 2`, so an old file fails
with a direct message rather than being partially interpreted.

Input-wide fields remain at the top level to avoid moving unrelated sections:

```toml
input_schema_version = 2
input_poll_hz = 1000
input_mode = 'Keyboard' # or 'Controller'
gameplay_input_style = 'Arcade' # or 'Switch'
axis_press_threshold_percent = 50
axis_release_threshold_percent = 40
```

The `[keyboard]` table uses logical action names and canonical physical-key
tokens. A token is `sc:hhhh`, `e0:hhhh`, or `e1:hhhh`, where `hhhh` is the
four-digit hexadecimal Raw Input make code. The default shape is:

```toml
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
```

The `[controller]` table records one tagged exact identity and a list of typed
bindings:

```toml
[controller]
backend = 'XInput' # or 'RawHid'
device_id = '0' # XInput slot, or exact RIDI_DEVICENAME path
bindings = [
  { action = 'LeftBoosterUp', type = 'XInputButton', control = 'DPadUp' },
  { action = 'LeftBoosterUp', type = 'XInputAxis', control = 'LeftY', direction = 'Negative' },
  { action = 'LeftBoosterButton', type = 'XInputButton', control = 'A' },
]
```

The complete binding type contract is:

| Type | Required fields |
|---|---|
| `XInputButton` | `action`, `control` |
| `XInputAxis` | `action`, `control`, `direction` |
| `XInputTrigger` | `action`, `control` |
| `RawHidButton` | `action`, `usage_page`, `usage`, `link_collection`, `report_id` |
| `RawHidValue` | the Raw HID address fields plus `direction` and captured `neutral_value` |
| `RawHidHat` | the Raw HID address fields plus cardinal `direction` |

`XInputButton` controls use the XInput names `A`, `B`, `X`, `Y`, `DPadUp`,
`DPadDown`, `DPadLeft`, `DPadRight`, `Start`, `Back`, `LeftShoulder`,
`RightShoulder`, `LeftThumb`, and `RightThumb`. `XInputAxis` controls are
`LeftX`, `LeftY`, `RightX`, and `RightY`. `XInputTrigger` controls are
`LeftTrigger` and `RightTrigger`. Axis direction is `Positive` or `Negative`;
hat direction is `Up`, `Down`, `Left`, or `Right`.

Raw HID address fields are unsigned integers taken from the cached HID
capability descriptor. `neutral_value` is the signed raw value observed during
capture and is validated against the connected control's logical range. The
selected controller backend and every binding type must agree.

The serialized field names use logical booster directions rather than the old
FastIO-oriented `p1_*`/`p2_*` naming. The FastIO translation remains internal.
Other NESYS, registry, audio, RFID, and experimental settings retain their
existing schema. The old `axis_threshold`, `gamepad_index`, and `[gamepad]`
fields are removed.

Users are not expected to author Raw HID usage fields or device paths. The
template provides XInput defaults, while ConfigGUI performs real device
selection and capture for either backend.

## ConfigGUI

Dear ImGui remains the UI layer. Its platform/renderer integration changes
from SDL3 plus SDL_Renderer to:

- `imgui_impl_win32` for the Win32 window and messages.
- `imgui_impl_dx11` for Direct3D 11 rendering.

The pinned ImGui source already includes both backends. ConfigGUI owns a normal
visible Win32 window, D3D11 device, swap chain, render target, resize handling,
and the normal ImGui frame loop. Its UI/main thread receives Raw Input and
polls XInput during capture; it does not create a gameplay-style worker.

ConfigGUI and runtime share discovery, identity, scan-code labeling, HID
descriptor, control normalization, and binding serialization code. This is a
functional shared library without UI or worker ownership, preventing the GUI
from producing identifiers the runtime interprets differently.

Controller selection presents two explicit groups:

- XInput slots with connection state.
- Generic Raw HID devices, excluding Xbox shadow-HID entries.

Selecting a device records its backend and exact identity. A configured but
missing identity remains visible as unavailable and is never silently replaced.

Binding capture behaves as follows:

1. Open a modal for one logical action.
2. Ignore controls already active when capture begins until they return to
   neutral.
3. Accept the first button, axis direction, trigger, or hat direction that
   crosses the capture threshold on the selected exact device.
4. Show a friendly semantic XInput label or a stable generic HID label such as
   `Button 1`, `X Axis +`, or `Hat Up`.
5. Let the user add, replace, or remove bindings.
6. Commit the captured descriptor only after validation.

Keyboard capture records the next Raw Input make transition and shows the
logical label derived from the current layout.

## Failure Handling

Configuration validation rejects:

- A missing or unsupported schema version.
- An unknown backend.
- An XInput slot outside `0..3`.
- An empty Raw HID device path.
- An unsupported poll rate.
- Thresholds outside `0..100` or release greater than/equal to press.
- A malformed scan-code token.
- A binding whose required backend-specific fields are missing.
- A binding targeting an unknown logical action or control kind.

Failure to create the worker, private window, message queue, stop/timer handles,
or required Raw Input registrations fails runtime startup. The FastIO snapshot
stays zero and the existing iDmac open failure path reports the stage.

A missing configured controller is not a startup failure. Keyboard and system
inputs remain active. Invalid/malformed HID packets clear only the affected
controller state, are discarded safely, and produce rate-limited diagnostics.
An unavailable XInput DLL/slot likewise disables only that controller source.

Any unexpected worker exit publishes zero before logging a prominent terminal
input error. No per-frame, per-poll, or per-register logging is allowed.

## Diagnostics

One-time and transition diagnostics include:

- Worker thread ID and priority result.
- Hidden window handle and owner thread.
- Requested/effective Raw Input registrations and flags.
- Configured input mode, poll rate, thresholds, and Test scan-code label.
- Selected backend and exact identity.
- Raw HID path, product name, VID/PID, top-level usage, report sizes, and
  capability counts.
- XInput DLL version choice, selected slot, connect, and disconnect.
- Game foreground/background transitions.
- Device arrival/removal and exact-identity match result.
- Rate-limited parsing errors.
- Debug-only logical transitions, including Test and the published FastIO word.

The normal hot path remains quiet.

## Build and Dependency Changes

Remove SDL from `cmake/Dependencies.cmake` and from every target include/link
list. In particular:

- `gc_config` uses native input configuration types and no SDL parser/header.
- `gc_input` links the required Windows Raw Input/HID/XInput libraries or
  loads XInput dynamically.
- `ConfigGUI` links Win32, D3D11, DXGI, and the ImGui Win32/DX11 backends.
- `iDmacDrv32` no longer links SDL directly.
- `gc_audio` loses only its unused SDL include/link entries; audio source code
  is unchanged.

Delete the SDL-specific parser after all consumers use the native types. Remove
the unused SDL include from `DllMain.cpp`. The source tree, generated build
graph, DLL, and ConfigGUI must contain no SDL dependency after the migration.

## Automated Verification

### Configuration and labels

- Parse and round-trip the new template.
- Reject the old SDL schema and every invalid field listed above.
- Verify default logical mappings and supported poll rates.
- Verify canonical `PhysicalKey` tokens for ordinary, `E0`, and `E1` keys.
- Verify label generation/fallback for letters, arrows, modifiers, main Enter,
  and Numpad Enter.
- Verify RFID scan-code-to-VK conversion and conversion failure isolation.

### Pure mapping and normalization

- Preserve every logical-to-FastIO bit mapping.
- Verify keyboard make/hold/break and repeated-make idempotence.
- Verify independent source OR behavior and multiple bindings per action.
- Verify positive/negative axes, unipolar triggers, hats, diagonal hats,
  press/release hysteresis, and neutral drift.
- Verify source clearing on focus loss, disconnect, and worker shutdown.

### Backend adapters

- Feed synthetic `RAWKEYBOARD` packets through the decoder, including
  extended flags.
- Test Raw Input size/type/count validation separately from the platform call.
- Wrap `HidP_*` access behind a narrow adapter so fake capabilities/reports can
  test buttons, values, report IDs, signed ranges, and malformed data.
- Wrap `XInputGetState` behind a function table and verify buttons, sticks,
  independent LT/RT, simultaneous LT+RT, unchanged packet numbers,
  disconnect, and reconnect.
- Verify Xbox shadow-HID filtering and exact Raw HID path matching.

### Runtime and GUI integration

- Extend the startup test to create/find the GameWare test window, start one
  gameplay worker, verify its private window/registrations, and close cleanly.
- Verify repeated opens reuse the worker and final close joins it.
- Verify foreground mismatch publishes zero.
- Verify ConfigGUI device selection and binding capture operate on the shared
  model and save/reload the same identity/control descriptors.
- Build full x86 Debug and RelWithDebInfo configurations and run the complete
  CTest suite.
- Audit CMake, source, linked dependencies, and distribution artifacts to prove
  SDL is absent from both `iDmacDrv32.dll` and ConfigGUI.

Automated tests prove configuration, decoding, mapping, lifecycle, and build
integration. They do not prove physical-device delivery inside the game.

## Manual Runtime Acceptance

The user performs runtime acceptance after a fresh Release DLL and ConfigGUI
are deployed. The agent must keep build/static results separate from gameplay
acceptance.

Required checks:

- Startup logs show one gameplay worker, one private window, effective Raw
  Input registrations, and the configured Test scan code.
- Pressing configured `T` produces a raw transition, a Test logical transition,
  the Test FastIO bit, and entry into Test mode.
- Ordinary remapped keyboard booster controls work through the loader.
- Keyboard hold/release is stable, focus loss clears input, and background
  input never reaches FastIO.
- The game's existing native keyboard input remains unchanged in this phase;
  in particular its current F1 behavior still works.
- A selected Raw HID controller accepts buttons, D-pad/hat, stick directions,
  and triggers as logical inputs.
- A second unselected HID controller is ignored.
- Removal clears state; another device is not substituted; reconnecting the
  exact path restores eligibility.
- A selected XInput slot accepts buttons, D-pad, both sticks, each trigger, and
  simultaneous LT+RT.
- A missing XInput slot does not block keyboard/system input and reconnects
  without restarting.
- ConfigGUI captures keyboard, Raw HID, and XInput bindings and the runtime
  consumes the saved identifiers exactly.
- Input remains responsive and free of duplicate/stuck transitions at 60 and
  240 FPS.

High-frame-rate menu repeat is observed during this pass but is accepted or
fixed under the framerate timing design, not this acquisition design.

## Deployment Boundary

Source, tests, documentation, and commits belong in
`H:\gc\artifacts\GCLoader`. `H:\gc` remains the runtime/deployment tree.
Deployment occurs only while `game471.exe` is stopped and preserves
operator-owned non-input settings. The new template intentionally replaces the
old input schema; no automatic upgrade is attempted.

## References

- Microsoft, Raw Input overview and API:
  <https://learn.microsoft.com/en-us/windows/win32/inputdev/raw-input>
- Microsoft, Using Raw Input:
  <https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input>
- Microsoft, HID application programming interface:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/hid-api>
- Microsoft, DirectInput and XUSB devices:
  <https://learn.microsoft.com/en-us/windows/win32/xinput/directinput-and-xusb-devices>
- MysteriousJ, Joystick Input Examples and combined Raw Input/XInput example:
  <https://github.com/MysteriousJ/Joystick-Input-Examples>
