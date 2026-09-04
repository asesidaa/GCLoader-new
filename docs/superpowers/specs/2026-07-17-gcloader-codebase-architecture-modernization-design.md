# GCLoader Codebase Architecture Modernization Design

Date: 2026-07-17

Status: Approved umbrella design and Subproject 1 design contract

Supersession note (2026-09-05): the Hooking, checked-image, rollback,
atomicity, phased-delivery, and completion clauses are superseded by the
[loader-wide cleanup design](2026-09-05-loader-codebase-cleanup-design.md).
The source-layout, domain ownership, external-contract, and already-completed
migration decisions in this document remain historical and authoritative.

## Context

The RFID/JVS modernization established focused `Rfid`, `TestModeStorage`, and `Win32Hooks` source packages with explicit ownership. Most remaining production code still lives at the repository root even though it already forms recognizable audio, input, NESYS, configuration, runtime-patch, logging, and iDmac domains.

The physical layout and build graph now obscure those domains:

- Forty-plus production `.cpp` and `.h` files occupy the repository root.
- The root `CMakeLists.txt` owns one large production source list and repeats production sources across 28 individual test executables.
- `config.cpp` is compiled into many unrelated targets, while audio sources are repeated across most audio tests.
- RFID, audio, and NESYS contain three independent MinHook transaction implementations.
- Configuration parsing and validation are split between a global runtime singleton and ConfigGUI.
- `FrameratePatch.cpp` owns many unrelated patch families plus checked-memory machinery.
- The exported iDmac Adapter directly contains FastIO register behavior.

This design defines one final architecture and a sequence of independently verified subprojects. It is the complete design contract for the source/build foundation. Later deepening subprojects receive focused specs before their own implementation plans.

## Relationship to Existing Designs

Existing approved designs remain authoritative for runtime behavior:

- RFID/JVS module ownership, protocol behavior, and diagnostics.
- Exclusive audio format, mixing, cursor, pacing, and failure behavior.
- Asynchronous input polling and snapshot ownership.
- Switch gameplay input semantics.
- NESYS network, registry, process-launch, and fixed-RVA behavior.
- Process-role logging and the existing `LoadLibraryW` child-readiness handshake.

This design supersedes those documents only for physical source paths, CMake ownership, internal configuration access, shared hook installation, and shared checked-patch infrastructure. It does not reinterpret their binary-backed behavior contracts.

## Domain Naming

The source tree uses `iDmac`, matching `iDmacDrv32.dll` and the user's requested repository convention. The vendor spells the product [iDMAC and expands it as Intelligent DMA Controller](https://www.oki-oids.jp/solution/products/fpga/idmac.html). `Dmac` is not used as the driver or source-package name.

Project-specific terms are defined in the repository root `AGENTS.md`.

## Goals

- Put every DLL/runtime C++ source under one `src/` tree and tool-only C++ source under `tools/`.
- Organize production and test paths by feature ownership.
- Give each source package explicit CMake target ownership.
- Link tests against the same compiled implementation used by `iDmacDrv32`.
- Preserve existing deep Interfaces in audio, input, NESYS, RFID/JVS, storage, and logging.
- Deepen only proven shared seams: configuration documents, owned MinHook installation, guarded game-image patches, and FastIO register behavior.
- Make `DllMain`, iDmac exports, and ConfigGUI thin outer Adapters.
- Use C++23 typed results throughout new fallible internal Interfaces.
- Keep every subproject buildable, testable, reviewable, and reversible on its own.
- Improve Locality and AI navigability without introducing gameplay behavior changes.

## Non-Goals

- Adding gameplay features, new config keys, new hook sites, or new executable patches.
- Changing RFID/JVS bytes, card state, COM behavior, or test-mode storage behavior.
- Changing audio formats, latency, pacing, DirectSound behavior, or WASAPI behavior.
- Changing input mappings, polling rates, Switch semantics, or FastIO register values.
- Changing NESYS network, registry, injection, resolver, or ping behavior.
- Replacing the established parent/child `LoadLibraryW` readiness handshake.
- Adding a general dependency-injection framework or abstract Interface for every class.
- Preserving old internal include paths, global names, or compatibility forwarding headers.
- Extracting or rewriting `zero_decrypt.zip`; it is only relocated unchanged to `tools/`.
- Cleaning tracked IDE metadata or changing the runtime/deploy tree at `H:\gc`.

## External Compatibility Contract

The initiative preserves:

- The `iDmacDrv32` DLL name, exported names, x86 calling conventions, and ordinals.
- The `ConfigGUI` target and existing CMake executable/test target names.
- The current set of 28 tests, with later focused tests added under their feature paths.
- The root runtime `config.toml` path, all keys, strict required-field policy, value semantics, and current GUI defaults.
- Process roles, initialization order, `loader-log.txt`, and `loader-service-log.txt`.
- Hook RVAs, signatures, enabled/disabled gates, response bytes, timing, and game-visible behavior.
- Lazy input startup and intentionally process-lifetime state where existing designs require it.

Internal source compatibility is deliberately not preserved. All callers are updated directly, and obsolete root-level headers are deleted after migration.

One approved tool-only behavior change is explicit: ConfigGUI may begin with the current defaults when `config.toml` does not exist. An existing file that cannot be read, parsed, or validated remains an error and is never silently replaced with defaults.

## Target Repository Layout

```text
GCLoader/
  CMakeLists.txt
  AGENTS.md
  config.toml
  cmake/
    Dependencies.cmake
    ProjectOptions.cmake
  src/
    Loader/
    Logging/
    Config/
    Driver/
      iDmac/
        iDmacDrv32.def
      FastIo/
    Input/
      Polling/
      Switch/
    Audio/
      DirectSound/
      Mixer/
      Wasapi/
    Nesys/
      Launcher/
      Network/
      Registry/
    Patches/
      GameImage/
      Framerate/
      Countdown/
    Rfid/
      Jvs/
    TestModeStorage/
    Win32Hooks/
    Platform/
      Win32/
        KeyMapping.h
        Hooking/
  tools/
    ConfigGUI/
    zero_decrypt.zip
  tests/
    Audio/
    Config/
    Driver/
    Input/
    Loader/
    Logging/
    Nesys/
    Patches/
    Rfid/
    TestModeStorage/
    Win32Hooks/
  docs/
```

Headers remain beside their implementations. GCLoader does not expose an installed C++ SDK, so a separate public `include/` tree would add a shallow publishing Interface without a real consumer.

`Win32Hooks` remains a higher-level Adapter package because it deliberately routes Kernel32 calls into RFID and test-mode storage. `Platform/Win32/Hooking` is lower-level MinHook infrastructure and never depends on a feature.

### Subproject 1 migration map

Subproject 1 uses this mechanical ownership map. It may rename a file to state its existing role, but it does not move behavior between files.

| Current source | Subproject 1 destination |
|---|---|
| `dllmain.cpp` | `src/Loader/DllMain.cpp` |
| `SessionLog.*` | `src/Logging/SessionLog.*` |
| `config.*`, `RegistryConfig.*`, `SdlRflParsers.h` | `src/Config/` |
| `WinKeyMapping.h` | `src/Platform/Win32/KeyMapping.h` |
| `iDmacDrv32.cpp`, `iDmacDrv32.def`, `RegisterOpTypes.h`, `keycodes.h` | `src/Driver/iDmac/` |
| `InputManager.*`, `InputPollingRuntime.*`, `InputSnapshotState.*` | `src/Input/Polling/` |
| `SwitchInputPolicy.*`, `SwitchInputPatch.*` | `src/Input/Switch/` |
| `DirectSoundFacade.*` | `src/Audio/DirectSound/` |
| `MiniaudioMixer.*`, `AudioSnapshot.*`, `AudioCursorTimeline.*` | `src/Audio/Mixer/` |
| `WasapiAudioPatch*`, `WasapiEndpoint.*`, `ExclusiveAudioEngine*`, `OutputPacingTracker.*`, `WasapiAudioTypes.*` | `src/Audio/Wasapi/` |
| `NesysServiceProcess.*`, `NesysServicePatch.*`, `NesysHookTransaction.*` | `src/Nesys/` |
| `NesysServiceLauncher.*` | `src/Nesys/Launcher/` |
| `NesysNetworkConfig.*`, `ServerAddressOverride.*`, `SyntheticNetworkAdapter.*` | `src/Nesys/Network/` |
| `RegistryConfigOverride.*` | `src/Nesys/Registry/` |
| `FrameratePatch.*` | `src/Patches/Framerate/` |
| `CountdownTimerFreeze.*` | `src/Patches/Countdown/` |
| `Rfid/**` | `src/Rfid/**` |
| `TestModeStorage/**` | `src/TestModeStorage/**` |
| `Win32Hooks/**` | `src/Win32Hooks/**`, except the existing general transaction may become the initial `src/Platform/Win32/Hooking/` implementation |
| `GUI_main.cpp` | `tools/ConfigGUI/Main.cpp` |
| `zero_decrypt.zip` | `tools/zero_decrypt.zip` unchanged |

Tests move into the matching feature path without changing their executable or CTest names. `keycodes.h` is retained during the mechanical move even if the current usage audit finds no caller; deletion belongs to closeout after explicit proof.

## Dependency Direction

```text
DllMain / iDmac exports / ConfigGUI
                 |
                 v
        feature composition Modules
                 |
        +--------+---------+
        v                  v
 validated options    shared Hooking /
                      GameImage Modules
```

Rules:

- Outer Adapters may depend on feature composition Modules.
- Feature Modules may depend on their validated option values and shared infrastructure.
- Shared infrastructure never includes a feature header.
- Config owns document parsing and validation but does not initialize a feature.
- Features never include the complete configuration schema or reach into a global configuration singleton.
- Tests cross the same Interface as production callers unless a private algorithm has a deliberately internal test Seam.
- A new Seam requires at least two production Adapters or otherwise demonstrated Leverage; a production Adapter plus a mock alone is insufficient.

These are final-state dependency rules. Subproject 1 records transitional legacy edges, including whole-schema configuration access, Logging's process-role include, and feature-local hook transactions, instead of changing their behavior during a move. The later focused subprojects remove those recorded edges.

## CMake Architecture

The root `CMakeLists.txt` is limited to the project declaration, common options, third-party dependency loading, top-level target composition, and `add_subdirectory` calls.

`cmake/Dependencies.cmake` owns pinned third-party declarations. `cmake/ProjectOptions.cmake` owns the x86 requirement, language standard, warning level, UTF-8 setting, static MSVC runtime, and the localized Ninja `/showIncludes` repair.

Each source package defines one or more internal static-library targets. Representative target ownership is:

- `gc_config`
- `gc_logging`
- `gc_hooking`
- `gc_game_image`
- `gc_audio`
- `gc_input`
- `gc_fastio`
- `gc_nesys`
- `gc_rfid_core` and `gc_rfid_feature`
- `gc_test_mode_storage`
- `gc_win32_hooks`
- `gc_runtime_patches`

Subproject 1 creates targets only for coherent Implementations that already exist. It does not add empty targets or pass-through Interfaces for future `gc_game_image` or `gc_fastio` behavior; later focused subprojects introduce those targets when their Implementations move behind the approved Interfaces.

RFID core and feature composition remain separate targets so `gc_win32_hooks` can route to RFID runtime without creating a target cycle back through `Rfid/Feature.cpp`.

The final `iDmacDrv32` shared-library target contains only outer Adapter sources such as `DllMain`, iDmac exports, the `.def` file, and final feature composition links. ConfigGUI links `gc_config` and GUI-only code. Tests link the same internal static libraries rather than recompiling production `.cpp` files into every executable.

Existing final and test target names remain unchanged so current build scripts continue to work.

## Configuration Module

`Config` owns one complete configuration value, reflect-cpp codecs, semantic validation, serialization, and filesystem persistence.

Its fallible Interfaces use C++23 results:

```cpp
std::expected<ConfigDocument, ConfigError>
LoadRequired(const std::filesystem::path&);

std::expected<ConfigDocument, ConfigError>
LoadOrDefaults(const std::filesystem::path&);

std::expected<void, ConfigError>
Validate(const ConfigDocument&);

std::expected<void, ConfigError>
Save(const std::filesystem::path&, const ConfigDocument&);
```

The focused configuration spec selects the final C++ symbol names, but these Interface semantics are fixed:

- Runtime loading requires an existing, complete, valid file.
- ConfigGUI may start from defaults only when the file is absent; every other load error remains an error.
- Runtime and GUI use identical parsing, keycode codecs, and semantic validation.
- Saving serializes only a valid document.
- Config performs no logging, dialogs, or process termination.

The runtime retains one immutable document for the process lifetime. Feature composition derives narrow values such as input, audio, NESYS, registry, RFID/storage, and patch options, then passes those values into the corresponding feature. `ConfigManager` and its field-shaped getters are deleted.

## Owned Hooking Module

`Platform/Win32/Hooking` owns exported-function resolution and MinHook lifecycle for one requested set. It supports both module/export requests and already-resolved target requests without absorbing feature policy.

The central Interface returns ownership or a typed error:

```cpp
std::expected<OwnedHookSet, HookInstallError>
Install(std::span<const HookRequest> requests, MinHookApi api);
```

The request representation preserves:

- A diagnostic name.
- Module/export or resolved target identity.
- Detour pointer.
- Original-function output pointer.

`HookInstallError` contains a scoped stage enum, export or target identity, Win32 status, MinHook status, and rollback completeness. `OwnedHookSet` supports explicit reverse-order rollback. Normal successful ownership remains process-lifetime and is not automatically unhooked during ordinary process detach.

Rules:

- Accept MinHook's already-initialized result as usable.
- Resolve every requested target before creating hooks when the feature requires preflight.
- Create and queue-enable only owned targets.
- Never call an `MH_ALL_HOOKS` operation.
- Roll back only targets created by the failed set.
- Report incomplete rollback so the outer Adapter can invoke its existing fail-fast policy.
- Keep fixed-RVA signature knowledge, feature logging, and feature activation order outside this Module.

RFID, audio, and NESYS remain distinct Adapters at this real Seam.

## Checked Game-Image Module

`Patches/GameImage` concentrates repeated executable-image correctness work:

- Resolve a guarded RVA against an explicitly selected loaded image.
- Reject arithmetic overflow or an out-of-range span.
- Read expected bytes safely.
- Compare an exact sequence or explicitly sized prefix.
- Apply a checked byte write with the required protection transition.
- Record original bytes and restore earlier writes after partial failure.

Feature Modules retain their own named sites, RVAs, expected signatures, replacement bytes, SafetyHook callbacks, and behavior. The shared Module does not turn every patch into a generic descriptor language and does not hide binary-backed intent.

`GamePatchError` carries the feature, named site, RVA, scoped stage, and bounded expected/actual byte data. Framerate, countdown, Switch input, and the NESYS fixed-RVA path migrate only where the shared Interface provides real Leverage.

## Runtime-Patch Ownership

The external framerate Interface remains one feature-level initialization operation. Its current implementation is split internally by coherent behavior:

- Frame-domain and timing changes.
- MovieClip, notice, and news gating.
- Stage-3D clip selection.
- Gameplay synchronization and counters.
- BGM preload behavior.

Addresses, signatures, state, diagnostics, and tests for one behavior stay in the same internal Module. Countdown remains a separate feature package because its enablement and local callsite semantics are independent.

Switch input remains under `Input/Switch`, not under general runtime patches, because its policy and hook Adapter jointly implement one input mode.

## iDmac and FastIO

The exported C functions remain ABI Adapters at the immutable game-facing Seam. They initialize required caller outputs for characterized calls, translate typed internal failures to the established return values, and contain no register switch. The focused iDmac/FastIO spec must characterize invalid pointer behavior before changing it; this umbrella design does not invent a new invalid-call contract.

`Driver/FastIo` owns:

- Known read and write register identifiers.
- Board identity and status values.
- Digital input projection from the published input snapshot.
- Analog, coin-slot, hub, GPIO, and currently unknown register behavior.
- The current semantics of stubbed buffer, memory, DMA, and program-download operations when those semantics belong to the emulated board.

The focused iDmac/FastIO spec must first characterize every existing export and register result. No stub is made more realistic merely because the implementation is moved.

Input polling remains lazy. `iDmacDrvOpen` starts it with validated options captured during attach, `iDmacDrvClose` stops it, and FastIO reads the published snapshot without owning SDL.

## Existing Feature Modules

### Audio

The recent audio Module split remains authoritative. Audio moves under `Audio/DirectSound`, `Audio/Mixer`, and `Audio/Wasapi`, gains CMake ownership, receives validated options, and migrates hook installation. This umbrella design does not otherwise redesign the audio Interfaces.

### Input

`InputSnapshotState`, `InputManager`, and `InputPollingRuntime` retain the responsibilities from the asynchronous-input design. They move under `Input`, adopt the `gc::input` namespace consistently, and receive validated options without including the complete configuration schema.

### NESYS

Process-role detection, launcher behavior, synthetic network state, resolver override, registry override, and fixed-RVA ping behavior remain independently gated. NESYS moves under feature subdirectories, receives narrow options, and migrates to shared Hooking/GameImage ownership without changing its process handshake.

### RFID/JVS and test-mode storage

The approved RFID/JVS and test-mode storage ownership remains unchanged. Both packages move under `src`; RFID feature composition receives card/storage options rather than using `ConfigManager`. The higher-level Kernel32 Adapter continues to own routing order.

### Logging

`SessionLog` receives a selected filename and byte limit. Loader maps the process role to `loader-log.txt` or `loader-service-log.txt`; Logging no longer depends on a NESYS header merely to make that selection.

## Runtime Initialization and Ownership

DLL attach retains the established order:

1. Detect game or NESYS process role.
2. Select and initialize the role-specific session log.
3. Load and validate `config.toml` exactly once.
4. Initialize the NESYS feature for either role.
5. In the game process only, initialize audio, RFID/storage routing, framerate/countdown patches, and Switch input.
6. Retain successful feature ownership for the process lifetime.

Each feature follows:

```text
validated options
  -> feature preflight
  -> hook / checked-patch requests
  -> shared infrastructure
  -> owned feature handle OR typed error
```

Loader publishes process-lifetime ownership only after the complete attach sequence succeeds. If a later enabled feature fails, Loader explicitly rolls completed feature handles back in reverse initialization order before returning `FALSE`. No detour may remain pointed into a DLL that failed to attach.

Normal process detach does not add a new shutdown protocol for intentionally process-lifetime workers and state.

## Input and Register Data Flow

```text
iDmacDrvOpen
  -> input polling runtime starts with captured validated options
  -> InputManager publishes FastIO snapshots

iDmacDrvRegisterRead
  -> iDmac ABI Adapter
  -> FastIO register Module
  -> published input snapshot when the input register is requested
  -> initialized caller outputs
```

`iDmacDrvClose` stops only the polling runtime, preserving the current lifecycle.

## C++23 Error Model

Every new fallible internal Interface returns `std::expected<T, E>`. Error categories use scoped enums and value types. A feature error contains a typed cause rather than flattening every failure into a string:

```cpp
using FeatureErrorCause = std::variant<
    ConfigError,
    HookInstallError,
    GamePatchError,
    FastIoError>;

struct FeatureError {
    FeatureId feature;
    FeatureErrorCause cause;
};
```

Exact variant membership remains local to the calling Module. Error rendering is a separate visitor in an outer Adapter. Raw `DWORD`, `HRESULT`, and `MH_STATUS` values remain available inside their typed error records when they are the authoritative platform result.

No exception crosses `DllMain`, an iDmac export, a hooked Win32 Interface, a COM Interface, or an audio callback. C++23 facilities are used where supported by the active MSVC toolchain; the design does not depend on an unavailable library facility merely because it is in the language standard.

## Atomicity and Failure Policy

- A hook set either becomes wholly owned or rolls back its created targets.
- A checked byte-patch set either becomes wholly owned or restores earlier writes.
- Loader rolls back completed feature handles if attach cannot complete.
- Disabled features install nothing but remain subject to the strict complete config schema.
- An incomplete rollback is explicit and invokes the feature's established unsafe-continuation policy.
- External pointer outputs are initialized on every defined success and failure path.
- Forwarded Win32 calls preserve original arguments, return values, and last-error behavior.
- Normal poll, render, resolver, and hook-success paths remain free of per-call diagnostic noise.

## Phased Delivery

### Subproject 1: Source/build foundation

- Record current tests and export inventory.
- Move every DLL/runtime source under the approved `src/` tree.
- Move ConfigGUI sources under `tools/ConfigGUI`.
- Move `zero_decrypt.zip` unchanged to `tools/zero_decrypt.zip`.
- Mirror test paths by feature.
- Split dependency/options CMake files and add per-package static-library targets.
- Preserve current internal Interfaces and behavior during this mechanical foundation.

This document is the detailed design contract for Subproject 1. After user review, the next Superpowers step is an implementation plan for this subproject only.

### Subproject 2: Shared Hooking

- Characterize all RFID, audio, and NESYS transaction semantics.
- Specify the final shared request/ownership Interface.
- Migrate one Adapter at a time with focused and full verification.

### Subproject 3: Configuration

- Specify the exact C++23 document/error Interface.
- Migrate runtime loading and ConfigGUI to one implementation.
- Pass validated options into feature Modules and delete `ConfigManager`.

### Subproject 4: Game-image patches

- Specify checked patch ownership and failure semantics.
- Split framerate implementation by behavior.
- Migrate countdown, Switch, and NESYS sites only where the shared Seam is deep.

### Subproject 5: iDmac/FastIO and input

- Characterize every register and exported stub result.
- Move FastIO behavior behind the iDmac ABI Adapter.
- Complete input namespace and option-injection cleanup.

### Subproject 6: Loader ownership and closeout

- Add reverse-order feature rollback.
- Decouple log filename selection from NESYS.
- Remove obsolete globals and shallow compatibility files.
- Document and audit the final dependency direction.

Each later subproject receives a focused design spec and implementation plan before code changes. The umbrella constraints in this document remain binding unless the user explicitly revises them.

## Automated Verification

### Baseline and every subproject

- Record the current 28-test CTest inventory before edits.
- Capture `dumpbin /exports` for the existing `iDmacDrv32` binary.
- Build affected x86 targets under the repository's active MSVC environment.
- Run focused tests, then the complete CTest suite.
- Run `git diff --check` and inspect repository status.
- Keep every subproject in independently reviewable commits.

### Source/build foundation

- All existing final and test target names configure and build.
- All 28 existing tests pass from their new paths.
- The final DLL export names and ordinals match the baseline.
- No production `.cpp` or `.h` remains at the repository root.
- Tests link package targets rather than listing production `.cpp` files directly.
- No old production include path remains.
- `zero_decrypt.zip` exists at `tools/zero_decrypt.zip` with unchanged content hash.

### Shared Hooking

- Failure at every resolution, initialization, creation, queue, and apply stage.
- Reverse-order rollback and unrelated-hook isolation.
- Already-initialized MinHook handling.
- Incomplete-rollback reporting.
- No `MH_ALL_HOOKS` operations.

### Configuration

- Required-file runtime loading.
- Missing-file GUI defaults.
- Identical runtime/GUI semantic validation.
- TOML round trips and unchanged key names.
- Strict rejection of incomplete or invalid documents.

### Game-image patches

- Expected-byte match and mismatch.
- RVA/span overflow and inaccessible-memory failure.
- Protection/write failure.
- Partial-set rollback.
- Per-feature patch inventory and signature coverage.

### iDmac/FastIO

- Every known register value and unknown-command behavior.
- Every exported stub's current return/output behavior.
- Required output initialization and characterized invalid-call behavior.
- Open/close and lazy input lifecycle.
- Unchanged export names, calling conventions, and ordinals.

## Manual Runtime Acceptance

Automated verification proves source ownership, link behavior, typed failure paths, binary exports, and characterized logic. It does not prove gameplay.

The user performs in-game acceptance after subprojects that change runtime ownership or internal behavior. Acceptance covers normal boot, NESYS child startup, card scanning, test-mode storage, input, audio, framerate/countdown behavior, Switch input, and shutdown expectations relevant to the changed subproject.

Subproject 1 is mechanical and requires build/static verification; it does not claim a gameplay delta or gameplay success.

## Completion Criteria

The architecture-modernization initiative is complete when:

- Every DLL/runtime source is under the approved `src/` tree, and ConfigGUI source is under `tools/ConfigGUI`.
- Tests mirror feature paths and link shared production implementations.
- Root CMake is a thin composition file.
- RFID, audio, and NESYS use one owned MinHook installation Module.
- Runtime and ConfigGUI share one strict configuration implementation.
- Repeated checked image-patch behavior has one deep shared Interface.
- Framerate patch behavior has internal Locality without widening its external Interface.
- iDmac exports are thin Adapters over a characterized FastIO Module.
- Loader owns reverse-order attach rollback.
- External contracts and approved game-visible behavior remain unchanged.
- Focused tests, the full x86 build, the complete CTest suite, static audits, and required user runtime acceptance all pass.
