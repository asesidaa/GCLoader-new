# Switch Gameplay Input Design

Date: 2026-07-10

## Context

The arcade game exposes two boosters. Each booster has four directions and one center button. GCLoader currently maps keyboard or gamepad inputs into the original arcade board signals and leaves the game's gameplay judgment rules unchanged.

The Switch version uses a simplified gameplay control scheme:

- A direction press can also act as the center-button input for the same booster.
- A diagonal direction note can be hit with either adjacent cardinal direction instead of requiring both cardinal components simultaneously.

The simplified behavior must be optional, gameplay-only, identical for keyboard and gamepad mappings, and enabled as one coherent input style. Menus must retain the native arcade distinction between directions and booster buttons.

## Binary Evidence

The analysis target is `H:\gc\game471.exe.i64`, whose image base is `0x00400000`. The evidence below was collected through one reusable daemon-backed `ida-cli` session.

### Arcade input path

- `GC120FPS_GWInputDeviceXioFioBoost_UpdateSnapshotFromIdmac` at `0x004B4500` translates the iDmac/FIO snapshot into the game's GW input-device state.
- `GC120FPS_GWInputXio_ComputeHeldPressReleaseRepeatBits` at `0x00455C80` derives held, pressed-edge, released-edge, and repeat fields from the current and previous input words.
- `CBooster` keeps ten basic logical inputs: left-booster directions `0..3`, left button `4`, right-booster directions `5..8`, and right button `9`.
- The paired logical-input table at `0x006F4A6C` pairs `0/5`, `1/6`, `2/7`, `3/8`, and `4/9`. Logical IDs `10..14` mean either paired input, while `15..19` mean both paired inputs.
- `sub_62E290` builds a two-dimensional direction vector for one booster from its four direction states. `sub_62D880` quantizes that vector into direction IDs: diagonals are `1`, `3`, `7`, and `9`; cardinals are `2`, `4`, `6`, and `8`; neutral is `5`.

### Gameplay-only input query seam

- `sub_659640` at `0x00659640` is the pressed-edge query wrapper used by gameplay judgment. Its direct callers are the note-judgment functions in the `0x005Dxxxx` region, including normal button, direction, hold, and special-note paths.
- `sub_659570` at `0x00659570` is the held-state query wrapper used by `sub_5D2E50` and `sub_5D41B0` for direction and sustained-note judgment.
- These wrappers are above the shared GW input device but below gameplay note logic. Detouring them changes gameplay judgment without changing menu input or GCLoader's emulated board word.
- Both functions begin with the same verified 16-byte sequence:

  ```text
  55 8B EC 83 EC 18 89 4D EC C6 45 FF 00 8B 4D EC
  ```

### Diagonal judgment seam

`sub_5D2E50` at `0x005D2E50` compares the current booster direction with the chart target. Native behavior first accepts an exact direction match and small angle-normalization equivalents. Its additional fallback only lets a physical diagonal satisfy a cardinal target under the existing edge rules. It does not let one cardinal component satisfy a diagonal target.

At `0x005D32A0` (RVA `0x001D32A0`), native matching has finished and the function is about to consume its local match flag. The verified instruction sequence begins:

```text
0F B6 55 8B 83 FA 01 75 2B
```

A mid-hook here can change only a failed local result to success after inspecting the already-normalized target and current direction locals. It does not replace the note matcher or disturb native successes.

## Goals

- Add one required `Arcade`/`Switch` gameplay input-style setting.
- Keep `Arcade` as the updated-config default.
- Apply Switch semantics during gameplay only.
- Treat every newly pressed direction as an independent same-booster button edge.
- Treat a booster button as held while its real button or any same-booster direction is held.
- Let either adjacent cardinal satisfy a diagonal for initial and continuation judgment.
- Preserve real buttons, exact diagonals, native cardinal matching, menu behavior, and both physical input backends.
- Install the three required hooks atomically and fall back completely to Arcade behavior if installation fails.

## Non-Goals

- Changing the raw iDmac/FIO board word produced by `InputManager`.
- Applying direction-to-button aliases in menus or test mode.
- Converting chart data or changing note definitions.
- Changing keyboard or gamepad bindings.
- Changing input polling, chattering windows, judgment timing, or repeat timing.
- Reproducing Switch UI, assets, or other platform behavior.
- Supporting unknown executable revisions without verified signatures.

## Configuration

Add a first-class enum and top-level field:

```cpp
enum class GameplayInputStyle {
    Arcade,
    Switch,
};
```

```toml
gameplay_input_style = 'Arcade'
```

The existing `input_mode` continues to select `Keyboard` or `Gamepad`; `gameplay_input_style` changes the post-mapping gameplay semantics for either backend.

The field is required. A config missing it fails parsing under the repository's existing strict upgrade contract. `ConfigGUI` adds an `Arcade`/`Switch` combo near the input-mode controls and round-trips the field through reflect-cpp TOML serialization. The setting is not experimental because it is a complete, user-selected control scheme.

## Architecture

Add two focused units.

### `SwitchInputPolicy`

This unit contains no Windows, SDL, SafetyHook, or process state. It owns the fixed logical-input rules:

- Return the direction IDs that may alias a requested booster button.
- Decide whether a current cardinal is an allowed Switch component for a diagonal target.

Keeping these rules pure makes the complete mapping exhaustively testable without loading the game.

### `SwitchInputPatch`

This unit owns executable addresses, signature preflight, SafetyHook objects, hook callbacks, counters, logging, and transactional installation.

`SwitchInputPatchInit()` runs only in the game-process initialization branch. In Arcade mode it logs the selected style and installs nothing. In Switch mode it preflights every site before creating any hook, installs all three hooks, and publishes an active Switch state only after all hooks succeed.

The hook sites are:

| Purpose | VA | RVA | Hook type |
|---|---:|---:|---|
| Gameplay pressed-edge query | `0x00659640` | `0x00259640` | Inline |
| Gameplay held-state query | `0x00659570` | `0x00259570` | Inline |
| Post-native diagonal match | `0x005D32A0` | `0x001D32A0` | Mid |

No `InputManager` or `iDmacDrvRegisterRead` changes are part of this feature.

## Runtime Behavior

### Direction-to-button pressed edges

The pressed-edge detour calls the native wrapper for the requested logical input first. A native true result is returned immediately.

When the native result is false and the requested input is a booster button, the detour calls the original wrapper through its trampoline for each same-booster direction, using the same input-device ID and gameplay frame:

| Requested button | Additional direction queries |
|---|---|
| Left button `4` | `0`, `1`, `2`, `3` |
| Right button `9` | `5`, `6`, `7`, `8` |

The queries remain individual pressed-edge queries. If Up remains held and Left is newly pressed, the Left edge can create another virtual button hit. The feature does not collapse all directions into one aggregate held bit.

For every requested logical input other than `4` or `9`, the detour returns the native result unchanged.

### Direction-to-button held state

The held-state detour uses the same native-first and same-booster expansion rules, but calls the native held query. A sustained button judgment therefore remains held while the real button or any direction on that booster is held.

The detour does not add aliases for released-edge, repeat, menu, or raw board queries because the observed gameplay note paths require pressed and held semantics only.

### Simplified diagonal judgment

The diagonal mid-hook runs after the native matcher has produced its local result. It never changes a native success. When the native result is false, it adds only these matches:

| Diagonal target | Additional accepted current directions |
|---|---|
| `1` (up-left) | `2` (up), `4` (left) |
| `3` (up-right) | `2` (up), `6` (right) |
| `7` (down-left) | `8` (down), `4` (left) |
| `9` (down-right) | `8` (down), `6` (right) |

Neutral, another diagonal, the opposite cardinal, and unrelated cardinals do not gain a match. Exact diagonal inputs continue through the native success path.

The hook is inside the shared directional matcher used by the supported initial and continuation note paths, so the same one-component rule applies for the entire diagonal-note lifecycle.

## Installation and Error Handling

Installation is transactional:

1. Resolve the main executable base and all three RVAs.
2. Safely read and validate the two function-entry prefixes and the diagonal instruction sequence.
3. Create both inline hooks and the mid-hook.
4. Mark the active style as Switch only after all hook objects are valid.

If any signature or hook creation fails:

- Log the exact RVA and failure stage.
- Destroy every hook created by this initialization attempt.
- Leave the active style as Arcade.
- Continue game startup without partial Switch behavior.

Hook callbacks preserve native results on unexpected inputs. The diagonal callback uses guarded stack reads and writes; if a local cannot be read or written safely, it leaves the native match unchanged. No exception may escape a hook callback.

Startup logging reports both `requested_style` and `active_style`. Runtime counters track:

- Direction edges accepted as virtual button presses.
- Direction holds accepted as virtual button holds.
- Cardinal inputs accepted for diagonal targets.

Only the first occurrence of each behavior is logged directly, avoiding per-frame log spam.

## Testing

### Pure policy tests

Add `SwitchInputPolicyTests` and exhaustively cover the direction domain:

- Only target diagonals `1`, `3`, `7`, and `9` gain matches.
- Each diagonal gains exactly its two adjacent cardinals.
- Neutral `5`, unrelated cardinals, and other diagonals do not gain matches.
- Button `4` expands exactly to `0..3`.
- Button `9` expands exactly to `5..8`.
- Other requested IDs have no aliases.
- Native true results remain true without consulting aliases.
- Independent direction-edge results remain independent.

### Configuration tests

Extend `ConfigFeatureTests` to verify:

- `Arcade` and `Switch` both parse.
- `Arcade` is the value in the upgraded example config.
- A missing `gameplay_input_style` fails parsing.
- An unsupported enum value fails parsing.
- reflect-cpp TOML serialization and reparsing preserve the selected style.

### Hook-boundary tests

Test signature and installation-state helpers with synthetic byte spans:

- All three matching signatures permit installation.
- Any one mismatch rejects the complete install plan.
- A simulated hook-creation failure rolls the active state back to Arcade.
- The diagonal local-update helper leaves a native true result unchanged and fails safely on invalid local access.

Unit tests do not patch a live process.

### Build verification

Use the existing x86 MSVC environment to build at least:

```text
iDmacDrv32
ConfigGUI
SwitchInputPolicyTests
ConfigFeatureTests
```

Then run the full CTest suite.

## Runtime Acceptance

Validate against `game471.exe` with both keyboard and gamepad configurations.

Arcade mode:

- Reproduces current button, direction, diagonal, sustained-note, and menu behavior.
- Logs `requested_style=Arcade active_style=Arcade` and no Switch hooks.

Switch mode:

- Each direction edge hits a same-booster button note.
- Pressing a second direction while the first remains held produces another button hit.
- A direction held on a booster sustains the corresponding button judgment.
- Either component alone hits its matching diagonal.
- An unrelated direction does not hit that diagonal.
- Exact physical diagonals and real booster buttons still work.
- Initial and continuation judgments use the same simplified diagonal rule.
- Menu directions do not act as booster-button confirmation.
- Logs report all three hooks active and counters increment only from gameplay paths.

Failure-mode acceptance:

- A deliberate signature mismatch results in `requested_style=Switch active_style=Arcade`.
- No subset of the three hooks remains installed.
- Native gameplay and menus remain usable.

## Deployment Boundary

Source, tests, and this spec belong in `H:\gc\artifacts\GCLoader`. The runtime `H:\gc\config.toml` is deploy/operator state. It must be upgraded when the feature is implemented and tested, but it is not committed as part of the source change.
