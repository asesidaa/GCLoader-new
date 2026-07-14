# Asynchronous Input Polling Design

Date: 2026-07-15

## Context

`iDmacDrv32.dll` emulates the FastIO hardware interface used by Groove Coaster. The real hardware updates an input buffer independently of the game. The game reads the latest completed buffer through the driver; it does not initiate physical input polling.

GCLoader currently couples those two responsibilities. Every `iDmacDrvRegisterRead` call invokes `SDL_PollEvent`, handles at most one queued event, and only then returns the requested register. Consequently, the game's register-read frequency controls SDL event processing, unrelated register reads can mutate input state, queued events can accumulate, and the FastIO input read is not the cheap snapshot operation the game expects.

The input model will be changed to a dedicated SDL-owned worker that samples input asynchronously at a configured rate and publishes the latest complete FastIO word. The game-facing register read will only load that published word.

## Goals

- Poll keyboard and gamepad input independently of game register reads.
- Offer fixed polling rates of 125, 250, 500, and 1000 Hz, with 1000 Hz as the default.
- Keep all SDL input, window, event, gamepad, and cleanup operations on one owner thread.
- Publish one lock-free 32-bit FastIO snapshot for the game to read.
- Preserve focus-bound keyboard behavior and SDL gamepad hotplug behavior.
- Reliably clear held state on key release, focus loss, axis neutral, and gamepad disconnect.
- Preserve the existing logical-booster-to-FastIO translation.
- Correct the default keyboard and gamepad bindings by assigning them through logical booster directions rather than interpreting FastIO field names as logical names.
- Remove SDL work, allocation, locking, and input diagnostics from the register-read hot path.
- Fail device initialization with a clear error instead of exposing a partially initialized input device.

## Non-Goals

- Changing the game's judgment, chattering, repeat, or edge-detection logic.
- Changing the existing Arcade or Switch gameplay-input semantics.
- Renaming the serialized FastIO-oriented configuration fields.
- Changing ConfigGUI's existing logical booster labels.
- Stretching a press so the game observes a press and release that both occur between two worker samples.
- Moving RFID/card-read hotkey handling into the FastIO input worker. RFID is loader-owned functionality with no meaningful latency requirement and remains separate.
- Starting or joining the input worker from `DllMain`.
- Adding a custom Windows timer implementation when SDL already supplies the required timing abstraction.

## Terminology and Direction Mapping

The serialized direction names such as `p1_up` and `p2_left` identify FastIO board inputs. They are not logical booster-direction names. ConfigGUI already presents the intended logical labels, and the existing runtime translation from logical directions to FastIO bits is correct.

The authoritative mapping is:

| Logical input | FastIO config field | FastIO bit |
|---|---|---|
| Left Booster Up | `p1_up` | `P1_UP` |
| Left Booster Down | `p2_up` | `P2_UP` |
| Left Booster Left | `p1_down` | `P1_DOWN` |
| Left Booster Right | `p2_down` | `P2_DOWN` |
| Right Booster Up | `p1_left` | `P1_LEFT` |
| Right Booster Down | `p2_left` | `P2_LEFT` |
| Right Booster Left | `p1_right` | `P1_RIGHT` |
| Right Booster Right | `p2_right` | `P2_RIGHT` |

Internal input-state code will use logical booster names. Conversion to the FastIO word happens at the snapshot boundary so raw field names do not leak into input-source reasoning.

## Corrected Defaults

### Keyboard

The left booster uses W/S/A/D and the right booster uses the arrow keys:

| Logical input | FastIO config field | Correct default |
|---|---|---|
| Left Booster Up | `p1_up` | `W` |
| Left Booster Down | `p2_up` | `S` |
| Left Booster Left | `p1_down` | `A` |
| Left Booster Right | `p2_down` | `D` |
| Right Booster Up | `p1_left` | Up arrow |
| Right Booster Down | `p2_left` | Down arrow |
| Right Booster Left | `p1_right` | Left arrow |
| Right Booster Right | `p2_right` | Right arrow |

The left and right center-button defaults remain Space and K respectively. System-key and card-read defaults remain unchanged.

### Gamepad

The left stick controls the left booster and the right stick controls the right booster. The D-pad is a second default source for the left booster:

| Logical input | FastIO config field | Correct button default |
|---|---|---|
| Left Booster Up | `p1_dpad_up` | D-pad Up |
| Left Booster Down | `p2_button_up` | D-pad Down |
| Left Booster Left | `p1_dpad_down` | D-pad Left |
| Left Booster Right | `p2_button_down` | D-pad Right |
| Right Booster Up | `p1_dpad_left` | Invalid |
| Right Booster Down | `p2_button_left` | Invalid |
| Right Booster Left | `p1_dpad_right` | Invalid |
| Right Booster Right | `p2_button_right` | Invalid |

The left-stick axes remain the default P1 horizontal/vertical axes and the right-stick axes remain the default P2 horizontal/vertical axes. Center-button defaults remain South for the left booster and East for the right booster.

These are default-value corrections. Existing explicit operator bindings continue to mean exactly what their ConfigGUI logical labels say.

## Configuration

Add one required top-level setting:

```toml
input_poll_hz = 1000
```

Only `125`, `250`, `500`, and `1000` are accepted. The source example and newly generated configurations use `1000`. Missing or unsupported values fail parsing under the repository's strict configuration-upgrade policy.

ConfigGUI adds a fixed-choice combo near `Input Mode` and `Gameplay Input Style`. It displays the four numeric rates and serializes the selected integer. Existing logical binding labels and row-to-field relationships are unchanged.

The runtime deployment config under `H:\gc` is operator state. Upgrading it must add the required poll-rate field without overwriting explicit operator bindings.

## Architecture

### `InputPollingRuntime`

Add `InputPollingRuntime.h` and `InputPollingRuntime.cpp`. This unit owns:

- SDL worker creation and joining.
- A startup semaphore and structured startup result.
- Device-open reference counting and lifecycle serialization.
- The SDL stop flag and published 32-bit snapshot.
- SDL subsystem, attached-window, event, gamepad, and `InputManager` ownership.
- Poll cadence and priority setup.

The runtime uses SDL primitives where they express the requirement:

- `SDL_CreateThread` and `SDL_WaitThread` for worker lifetime.
- `SDL_Semaphore` for synchronous startup readiness.
- `SDL_AtomicInt` for the stop flag and FastIO snapshot.
- `SDL_GetTicksNS` and `SDL_DelayNS` for cadence.
- SDL window properties, events, keyboard, and gamepad APIs for input ownership.

Two narrow Win32 calls remain:

- `FindWindowA` locates the existing GameWare window.
- `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)` requests the exact selected priority. SDL's `SDL_THREAD_PRIORITY_HIGH` maps to Windows `THREAD_PRIORITY_HIGHEST`, which is stronger than required.

### Open and close lifecycle

The first `iDmacDrvOpen` starts the runtime and blocks on its startup semaphore. The worker performs all SDL initialization and publishes an initial snapshot before reporting success. Subsequent opens increment the reference count and reuse the same worker.

The final `iDmacDrvClose` sets the SDL atomic stop flag and joins through `SDL_WaitThread`. The worker publishes zero, destroys `InputManager` (which closes its gamepad), releases the attached SDL window wrapper, and calls `SDL_Quit`, all on the owner thread. A later open may create a fresh worker.

No thread is created or joined from `DllMain`. During normal execution, cleanup belongs to the final device close. Process termination may rely on Windows process teardown.

### Worker initialization

The worker performs these steps in order:

1. Attempt `THREAD_PRIORITY_ABOVE_NORMAL`; log and continue at normal priority if denied.
2. Locate the GameWare window.
3. Call `SDL_SetMainReady` on the worker so it is SDL's event/video owner.
4. Apply the existing joystick/gamepad hints.
5. Initialize the SDL joystick, gamepad, event, and video subsystems.
6. Load optional gamepad mappings.
7. Enable gamepad and joystick events.
8. Wrap the GameWare HWND using SDL window properties, then destroy the temporary property container.
9. Construct `InputManager` after SDL is ready.
10. Drain pending events, compose the initial FastIO word, publish it, and signal successful startup.

`InputManager` no longer owns global SDL initialization or shutdown. Its construction, event handling, gamepad access, and destruction all occur on the worker.

## Polling and Publication

The configured periods are exact integer milliseconds:

| Rate | Period |
|---:|---:|
| 125 Hz | 8 ms |
| 250 Hz | 4 ms |
| 500 Hz | 2 ms |
| 1000 Hz | 1 ms |

Each worker iteration:

1. Drains the entire SDL event queue with `SDL_PollEvent`.
2. Applies every event to worker-confined logical input state.
3. Composes one complete FastIO word.
4. Publishes it through `SDL_SetAtomicInt`.
5. Advances an absolute nanosecond deadline.
6. Calls `SDL_DelayNS` for the remaining time.

The pinned SDL 3.2.12 Windows implementation already uses a thread-local high-resolution waitable timer when available and provides its own fallback. No GCLoader-specific waitable-timer code is needed.

Absolute deadlines prevent work time from accumulating as schedule drift. When an iteration finishes after its deadline, the worker advances to the next future deadline rather than running catch-up iterations. The stop flag is checked between iterations; the maximum normal close delay is one configured period, or 8 ms at 125 Hz.

`SDL_AddTimerNS` is not used because its callback runs on another SDL-owned background thread. `SDL_DelayPrecise` is not used because it may busy-wait near a deadline, which is unnecessary for these integer-millisecond periods.

## Held, Released, and Combined State

Normal keyboard state is event-driven:

- `SDL_EVENT_KEY_DOWN` sets the mapped logical source.
- The logical source remains held in every later snapshot.
- `SDL_EVENT_KEY_UP` clears it.

SDL resets pressed keyboard state and generates key-up events when keyboard focus is lost. `InputManager` will also handle `SDL_EVENT_WINDOW_FOCUS_LOST` explicitly by clearing every keyboard-derived logical source. This makes the focus-only requirement explicit and prevents a stuck state if an event is missed or reordered around focus transition.

Gamepad button and axis sources are tracked separately. Axis values below or above the configured threshold set their corresponding logical direction; returning inside the threshold clears only the axis source. Releasing a gamepad button clears only the button source. If a button and an axis both hold the same logical direction, releasing one does not clear the other. A gamepad removal clears every gamepad-derived source immediately.

System keys continue to work from the keyboard in both input modes. Gameplay keyboard sources contribute in Keyboard mode; gameplay gamepad sources contribute in Gamepad mode.

If a complete press and release both occur between two worker samples, the published word contains only the final released state. This matches a sampled hardware buffer. At 1000 Hz the nominal sampling window is 1 ms.

## Game Register-Read Path

`iDmacDrvRegisterRead` no longer constructs or accesses `InputManager` and never calls SDL. `FIO_NODE_0_INPUT` performs one `SDL_GetAtomicInt` load from `InputPollingRuntime` and returns that word. All other register values and unknown-command behavior remain unchanged.

Temporary input event, state-transition, periodic read-summary, and per-register diagnostics are removed from this hot path. Runtime diagnostics are limited to startup configuration, startup failure, priority result, and device add/remove events.

## Failure Handling

Runtime startup reports a structured failure stage plus the underlying SDL or Win32 error. Fatal stages include:

- Worker creation failure.
- GameWare window discovery failure.
- SDL subsystem initialization failure.
- SDL property or attached-window creation failure.
- Input-manager construction failure.

On fatal failure, worker-owned resources are cleaned up, the snapshot remains zero, and `iDmacDrvOpen` shows one clear error dialog and returns a nonzero result. It never reports a partially initialized device.

Failure to obtain above-normal priority is nonfatal and logged once. No connected gamepad is also nonfatal: the worker remains active, publishes available keyboard/system state, and waits for SDL hotplug events.

## Code Boundaries

### `InputSnapshotState`

Add a small platform-free state unit that owns:

- Logical left/right booster and system-input state.
- Separate keyboard, gamepad-button, and gamepad-axis sources.
- Focus-loss and gamepad-disconnect clearing.
- Logical-input-to-FastIO-word conversion.

This keeps SDL event translation in `InputManager` while making the logical/FastIO distinction and source-combination rules directly testable.

### Existing files

- `iDmacDrv32.cpp`: delegate open/close to the runtime and replace input-register polling with an atomic snapshot load.
- `InputManager.h/.cpp`: become worker-confined, use logical state, handle focus loss, and relinquish SDL global lifetime.
- `config.h/.cpp`: add and validate the required polling rate and correct direction defaults.
- `config.toml`: add the 1000 Hz setting and corrected source defaults.
- `GUI_main.cpp`: add the polling-rate combo; retain existing logical binding labels and their FastIO field relationships.
- `CMakeLists.txt`: compile the runtime/state units and register focused state tests.
- `tests/ConfigFeatureTests.cpp`: cover poll-rate parsing and corrected defaults.

`SwitchInputPatch`, RFID/card-read handling, and unrelated hardware emulation remain unchanged.

## Automated Verification

Automated coverage remains intentionally narrow because the authoritative input test is a real game launch:

- Extend `ConfigFeatureTests` for all four supported rates, strict missing/unsupported failures, round-tripping, and corrected keyboard/gamepad defaults.
- Add one small state test for logical-to-FastIO conversion, press/hold/release, focus clearing, axis/button overlap, and gamepad disconnect.
- Build `iDmacDrv32`, `ConfigGUI`, the focused state test, and `ConfigFeatureTests` under the existing x86 MSVC toolchain.
- Run the complete existing CTest suite.

These checks prove configuration, pure state behavior, and build integration. They do not prove in-game latency or gameplay behavior.

## Manual Gameplay Acceptance

Manual gameplay testing is performed by the user. Agent verification must report build/static evidence separately and must not claim gameplay success before the user confirms it.

The manual pass should cover:

- Worker startup at the configured rate and a successful above-normal priority request or clear fallback log.
- W/S/A/D operating the four left-booster directions.
- Arrow keys operating the four right-booster directions.
- Center buttons and system keys remaining unchanged.
- Held keys staying held and key-up clearing them.
- Losing focus clearing keyboard input and background keyboard input remaining inactive.
- Left and right sticks controlling their corresponding boosters.
- D-pad defaults controlling the four left-booster directions.
- Axis/button overlap not releasing a direction until both sources release.
- Gamepad disconnect clearing state and reconnect restoring input.
- Arcade and Switch gameplay-input styles retaining their existing semantics.
- No input regression in menus or gameplay and acceptable perceived latency at 1000 Hz.

The other supported polling rates may be selected for compatibility or CPU-constrained systems; configuration tests cover their validity, while any subjective gameplay comparison remains manual.

## Deployment Boundary

Source, tests, documentation, and commits belong in `H:\gc\artifacts\GCLoader`. `H:\gc` is the runtime/deployment tree. Deployment must preserve operator-owned configuration values, add the required poll-rate setting, and treat the user's manual game launch as the final acceptance gate.
