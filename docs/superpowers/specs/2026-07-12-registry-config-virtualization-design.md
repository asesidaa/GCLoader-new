# Registry Configuration Virtualization Design

**Date:** 2026-07-12

**Status:** Approved design

**Related design:** `2026-07-11-nesys-network-virtualization-design.md`

## Problem

`game471.exe` and `NesysService.exe` read machine-wide registry values during startup. The installed Type X registry key is a superset that also contains settings owned by other software. Requiring users to edit the real 32-bit registry makes the meaningful game and service settings harder to discover, validate, move between installations, and manage alongside the rest of GCLoader's TOML configuration.

GCLoader already runs in `game471.exe`, injects the same DLL into the service child when required, and owns a transactional process-local hook framework. Registry configuration should use that existing process boundary. It must not write the real registry, hide unrelated values, or absorb settings that the analyzed binaries do not consume.

## Binary Evidence

The design is based on daemon-backed IDA analysis of the deployed binaries.

### `game471.exe`

- `sub_6313C0` at `0x6313C0` is the generic registry reader.
- `Country` is read as four bytes from `HKLM\SOFTWARE\taito\typex`.
- `SystemBiosDate` is read from `HKLM\SYSTEM\ControlSet001\Control\Biosinfo` and used only in machine-information reporting.
- `sub_460F30` enumerates NIC class subkeys and looks for `NetworkAddress` while validating adapter MAC state.
- A nonzero `Country` causes the single caller of `sub_6312E0` at `0x6312E0` to try removable-drive `country.dat` files and then `D:\country.dat`. If those reads fail, the registry-derived DWORD remains unchanged.

### `NesysService.exe`

`sub_411BE0` at `0x411BE0` reads eight values from `HKLM\SOFTWARE\taito\typex`:

- `GameKind`
- `EventNextTime`
- `ConditionTime`
- `TrafficCount`
- `LogLevel`
- `NewsPath`
- `EventPath`
- `LogPath`

`TrafficCount` is stored but has no consumer outside the registry reader in this build. The other seven values have concrete consumers in URL/request construction, scheduling, logging, or download storage.

The analyzed sample hashes are provenance, not compatibility gates:

- `game471.exe`: `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`
- `NesysService.exe`: `487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2`

## Goals

- Move meaningful game and service registry settings into strict TOML configuration.
- Keep registry virtualization independently gated from NESYS network virtualization.
- Override only values with approved, proven consumers.
- Preserve every unowned key and value through the original Windows registry APIs.
- Keep `Country` human-readable while returning the exact DWORD expected by the game.
- Reuse the existing game-to-service DLL injection and owned MinHook transaction.
- Implement native `RegQueryValueExA` buffer and type behavior.
- Make disabled mode indistinguishable from current registry behavior.
- Leave final runtime acceptance to the user after automated build and static verification.

## Non-goals

- Writing, creating, deleting, or repairing real registry keys or values.
- Making the Type X key optional.
- Supplying or configuring `TrafficCount`.
- Intercepting `SystemBiosDate`.
- Synthesizing, hiding, or changing NIC `NetworkAddress` values.
- Hooking `RegEnumKeyExA` or `RegEnumValueA`.
- Suppressing or virtualizing the game's `country.dat` path.
- Supporting registry APIs or binary versions not observed in the analyzed targets.
- Replacing the existing NESYS network configuration or adapter profile.

## Configuration Contract

Add one required top-level registry configuration tree:

```toml
[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'
```

All tables and fields are required by the reflect-cpp schema even when `registry.enabled` is false. This preserves the repository's strict configuration contract: old or partial files must be upgraded rather than silently receiving compatibility defaults.

Newly constructed ConfigGUI state and the distributed `config.toml` use the values above. Registry virtualization is disabled by default.

The existing `[nesys].server_ip` field remains separate. It continues to describe network destination policy and does not own service registry values.

### Country enum

Define a strict `GameCountry` enum with the following wire values:

| TOML name | Registry DWORD | Meaning |
|---|---:|---|
| `GrooveCoasterJpn` | `0` | GROOVE COASTER, Japanese branding |
| `Rhythmvaders` | `1` | RHYTHMVADERS, English branding |
| `GrooveCoasterEng` | `2` | GROOVE COASTER, English branding |

No numeric TOML compatibility form or unknown enum fallback is accepted.

The registry overlay returns this DWORD directly. No additional `country.dat` hook is installed. The supported runtime assumption is that the removable-drive copy and `D:\country.dat` read fail, leaving the configured DWORD intact. Supporting a machine on which that file path successfully replaces the value is outside this design.

### NESYS fields

| TOML field | Registry value | Type | Default | Validation |
|---|---|---|---:|---|
| `game_kind` | `GameKind` | `REG_DWORD` | `303801` (`0x4A2B9`) | Unsigned 32-bit integer |
| `event_next_time` | `EventNextTime` | `REG_DWORD` | `900` | Unsigned 32-bit integer; zero remains legal |
| `condition_time` | `ConditionTime` | `REG_DWORD` | `300` | Unsigned 32-bit integer; zero remains legal |
| `log_level` | `LogLevel` | `REG_DWORD` | `3` | Integer from `0` through `3` |
| `news_path` | `NewsPath` | `REG_SZ` | `D:\system\DUA\news` | Non-empty; at most 259 bytes before the terminating NUL |
| `event_path` | `EventPath` | `REG_SZ` | `D:\system\DUA\event` | Non-empty; at most 259 bytes before the terminating NUL |
| `log_path` | `LogPath` | `REG_SZ` | `D:\system\CmdFile\log` | Non-empty; at most 259 bytes before the terminating NUL |

The service itself converts zero timing values to 1800 seconds. The loader does not reinterpret them.

`TrafficCount` is deliberately absent. It remains a required physical registry value because the service checks that its query succeeds even though the loaded value is unused afterward.

### GUI

ConfigGUI adds a Registry section containing:

- An independent **Registry configuration overrides** checkbox.
- A country combo showing the three enum names with human-readable branding descriptions.
- Editable fields for the seven NESYS registry values.
- Inline validation for `log_level`, integer ranges, empty paths, and encoded path length.

Invalid registry configuration disables **Save Configuration**. Serialization uses the same strict reflect-cpp model as the runtime.

## Architecture

Add a focused `RegistryConfigOverride` component. It owns immutable configured values, tracked Type X registry handles, original API trampolines, native query-response formatting, and bounded diagnostics.

The component uses the existing `ApiHookRequest` and `OwnedMinHookTransaction` infrastructure. When registry virtualization is enabled, it requests exactly three exported hooks in each applicable process:

- `ADVAPI32!RegOpenKeyExA`
- `ADVAPI32!RegQueryValueExA`
- `ADVAPI32!RegCloseKey`

No wide-character, enumeration, creation, write, or deletion API is hooked.

### Tracked-key opening

The `RegOpenKeyExA` detour always calls the original trampoline first with the caller's original root handle, subkey, options, and access mask. This preserves the game's `KEY_ALL_ACCESS` request, the service's `KEY_READ` request, the 32-bit registry view, Windows ACL behavior, and native error codes.

When the original call succeeds and the request is a case-insensitive open of `SOFTWARE\taito\typex` directly beneath `HKEY_LOCAL_MACHINE`, the returned `HKEY` is added to a thread-safe process-local tracked-handle set. Other successful handles are not tracked.

The loader does not fabricate a key when the original open fails. The installed Type X key remains necessary, particularly because `TrafficCount` passes through.

### Query overlay

The `RegQueryValueExA` detour checks the handle and value name before deciding whether to override:

| Process role | Owned value names |
|---|---|
| Game | `Country` |
| Service | `GameKind`, `EventNextTime`, `ConditionTime`, `LogLevel`, `NewsPath`, `EventPath`, `LogPath` |

Matching is case-insensitive, like the registry. An owned query is answered from immutable process-lifetime configuration without calling Windows. Any untracked handle, null or unowned value name, or disabled feature calls the original trampoline unchanged.

The overlay implements the native query contract:

- `lpData == nullptr` reports the required byte count and succeeds.
- `lpType`, when present, receives `REG_DWORD` or `REG_SZ`.
- A DWORD requires exactly four bytes.
- A string's required size includes its terminating NUL.
- An undersized output buffer receives the full required size and returns `ERROR_MORE_DATA` without overrun.
- Valid exact or oversized buffers receive the complete value and return `ERROR_SUCCESS`.
- Invalid pointer or reserved-parameter combinations return the corresponding Win32 error.

No temporary string or buffer may outlive the query call. Configuration strings themselves remain immutable for the process lifetime.

### Handle close

The `RegCloseKey` detour calls the original trampoline first. It removes a tracked handle from process-local state only when Windows returns `ERROR_SUCCESS`; a failed close preserves the tracking entry because the handle may still be valid. Multiple simultaneous opens are supported. A later Windows handle reuse cannot inherit stale overlay ownership after a successful close.

### Pass-through boundary

The following remain completely native:

- `TrafficCount` under the Type X key.
- `SystemBiosDate` and its BIOS key.
- NIC class keys, subkeys, value enumeration, and `NetworkAddress`.
- Current Type X superset values such as `CoinCredit`, `Resolution`, `ScreenVertical`, `EventModeEnable`, `UserSelectEnable`, `GameResult`, `IOErrorCoin`, `IOErrorCredit`, and `UpdateStep`.
- Any future or unknown key/value.

The synthetic adapter MAC remains exposed only by the existing IP Helper API hooks. Registry virtualization does not create a synthetic `NetworkAddress` and therefore does not enter the game's matching-MAC error path.

## Independent Feature Composition

The existing network switch and the new registry switch represent independent policies:

- `experimental.enable_nesys_service_adapter_patch` controls adapter virtualization, resolver override, mutation suppression, and service ping redirection.
- `registry.enabled` controls only registry configuration virtualization.

The service launcher is shared transport. The game installs `CreateProcessA` interception when either policy requires the child DLL:

| Network | Registry | Game policy | Service policy |
|---|---|---|---|
| Off | Off | No NESYS launcher or registry hooks | Original service process |
| Off | On | Registry hooks plus service launcher | Registry hooks only |
| On | Off | Existing network hooks plus service launcher | Existing service network hooks |
| On | On | Combined network and registry hooks plus launcher | Combined network and registry hooks |

Extend the current feature-plan model so hook inventory is composed from enabled components rather than assuming one Boolean owns every service behavior. Existing hook-count assertions remain, but expected counts account for the selected combination.

A registry-only service initialization must not prepare adapter state, resolver state, mutation suppression, the fixed-RVA ping hook, or its signature check. A network-only initialization remains behaviorally unchanged.

All enabled exported hooks in a process are created and enabled through one owned MinHook transaction. The internal service ping hook continues to use its existing guarded lifecycle only when network virtualization is enabled.

## Failure Handling

Configuration parsing and semantic validation happen before hook installation. Invalid registry data is a startup error even when the table is disabled because the strict schema must always be internally valid.

When registry virtualization is enabled:

- Missing Advapi32 modules or exports fail initialization.
- Hook initialization, creation, queueing, or commit failure rolls back every owned hook in that process.
- The game does not continue with a partial registry policy.
- An injected service whose DLL initialization fails remains suspended, is terminated by the existing launcher handshake, and returns `ERROR_DLL_INIT_FAILED` to the game.
- A real Type X key open failure remains an open failure.
- A pass-through query failure remains the original failure. In particular, a missing `TrafficCount` still causes this service build to reject its configuration.

When registry virtualization is disabled, the three registry hooks are absent and every query follows original binary behavior.

## Diagnostics

Diagnostics are bounded and avoid logging physical registry contents:

- Process role and independent network/registry toggle state during initialization.
- Successful registry component installation and owned hook count.
- First successful tracked Type X key open in each process.
- First override of each owned value name, including its type but not repeated path contents.
- Exact hook-resolution or transaction stage on failure.
- Type X open failures remain observable through the binary's native behavior and may be reported once by the overlay without changing the returned status.

Pass-through queries are not logged individually.

## Automated Verification

Agent-owned verification is limited to build, unit, integration, and static checks. It does not claim final runtime acceptance.

### Configuration tests

- Constructed defaults serialize the complete required `[registry]`, `[registry.game]`, and `[registry.nesys]` tree.
- `registry.enabled` defaults to false.
- All seven NESYS defaults match the current installed registry values.
- All three country enum names round-trip and map to DWORD `0`, `1`, and `2`.
- Numeric or unknown country representations fail parsing.
- Missing tables, missing fields, invalid integers, invalid log levels, empty paths, and overlong paths fail validation.
- ConfigGUI and runtime serialization share the same field names and values.

### Registry overlay tests

- Exact and case-insensitive Type X key recognition.
- Game `KEY_ALL_ACCESS` and service `KEY_READ` opens are passed unchanged to the original API.
- Failed opens are not tracked or fabricated.
- Game and service roles own only their approved value sets.
- DWORD and string size probes, exact buffers, oversized buffers, short buffers, types, lengths, and terminators match the Win32 contract.
- Multiple simultaneous handles are tracked independently and removed only after a successful close.
- A failed close preserves the tracked handle and the original error.
- Closed or reused handles cannot retain stale ownership.
- `TrafficCount`, `SystemBiosDate`, `NetworkAddress`, unrelated Type X values, unrelated keys, and disabled mode invoke the original API.
- Hook inventory contains no `RegEnumKeyExA`, `RegEnumValueA`, registry-write, or registry-delete detour.

### Feature-plan and lifecycle tests

- Both roles produce the exact component and hook inventory for all four network/registry combinations.
- Registry-only game mode still installs the service launcher.
- Registry-only service mode installs no adapter, resolver, mutation, or ping hook.
- Network-only inventories remain unchanged.
- Combined failures roll back owned hooks and never leave a partially patched process.
- Injected service initialization failure retains the existing terminate-without-resume behavior.

### Build verification

- Build `iDmacDrv32`, ConfigGUI, focused registry/configuration tests, and existing NESYS tests with the x86 MSVC toolchain.
- Run the focused tests, then the complete CTest suite.
- Confirm the source and hook inventory contain no registry writes.
- Treat successful build and tests as implementation verification only.

## User-owned Runtime Acceptance

The user performs the final runtime test and decides whether the behavior is accepted. The manual checklist is:

- With registry virtualization disabled, confirm original registry behavior is unchanged.
- With registry virtualization enabled and network virtualization disabled, confirm the service is still injected and only registry hooks activate.
- Confirm `GrooveCoasterJpn`, `Rhythmvaders`, and `GrooveCoasterEng` produce the expected branding/language and effective DWORD values.
- Confirm the expected environment does not successfully replace the configured country through `country.dat`.
- Change physical owned NESYS values to distinguishable values and confirm the service observes TOML values instead.
- Confirm `TrafficCount` still comes from the physical registry and a missing value retains the original service failure.
- Confirm `SystemBiosDate` remains physical.
- Confirm NIC registry enumeration is untouched and no synthetic `NetworkAddress` is created.
- Confirm the adapter APIs still report the existing synthetic MAC independently of registry state.
- Confirm configured timers, log level, and news/event/log directories have the expected runtime effects.
- Confirm no registry value is written, created, or deleted by GCLoader.

The implementation is not described as runtime accepted until the user reports the manual result.

## Acceptance Criteria

The feature is complete when:

1. Registry configuration has an independent, disabled-by-default strict TOML gate.
2. Game `Country` and the seven consumed NESYS values are provided process-locally with the approved defaults and types.
3. Country enum values map exactly to 0, 1, and 2 with the approved branding meanings.
4. Every unowned value and key, including `TrafficCount`, `SystemBiosDate`, and NIC `NetworkAddress`, preserves native Windows behavior.
5. The real Type X key remains a superset and is never mutated by the loader.
6. Service injection occurs when either network or registry policy requires it, while hook families remain independently gated.
7. Enabled initialization is transactional and fail-closed in both processes.
8. Automated build and tests pass without being overstated as runtime acceptance.
9. The user completes and accepts the final manual runtime checklist.
