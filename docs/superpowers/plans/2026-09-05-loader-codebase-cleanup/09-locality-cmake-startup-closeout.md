# Locality, CMake, and Startup Closeout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the loader-wide cleanup by splitting the remaining multi-responsibility implementations, replacing the coarse runtime-patch target with coherent ownership targets, and cutting DllMain over to explicit game/NESYS composition roots.

**Architecture:** Feature files group state and callbacks by behavior, not arbitrary size. Game and NESYS startup remain explicit and separate. DllMain is only the Win32 attach adapter; role-specific roots collect and validate complete plans, publish state, then install versioned and non-versioned operations. Final CMake edges enforce the architecture and expose only SafetyHook through `gc_hooking` and reflect-cpp through `gc_config`.

**Tech Stack:** C++23, RuntimeImage, GameVersion profiles, SafetyHook v0.7.0 through HookRegistry, reflect-cpp v0.25.0 through Config, Win32 x86 DLL ABI, CMake/Ninja/MSVC, dumpbin.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 08 first. This plan performs the single final
  startup cutover; do not activate an incomplete intermediate barrier.
- Preserve the iDmac DLL/export ABI, game/NESYS process separation, strict
  config, hook order, patch order, Win32 handler order, calling conventions,
  original-call policy, output writes, return values, and `LastError` behavior.
- Split a file only where its groups have distinct state, dependencies, or
  reasons to change. Do not chase a line-count target or create a one-source
  static library without a coherent module boundary.
- No generic feature registry, static registration, linker-section discovery,
  global-constructor installer, service locator, or plugin framework.
- Preflight and installation remain distinct. A successful plan validation is
  not a successful hook installation.
- After mutation can begin, every failure logs, displays one popup, and aborts.
  Do not return `FALSE` or reverse an already installed operation.
- Do not deploy, launch the game/NESYS process, or claim runtime acceptance.

---

## Task 1: Split Audio composition from backend behavior

**Files:**

- Create: `src/Audio/AudioFeature.h`
- Create: `src/Audio/AudioFeature.cpp`
- Create: `src/Audio/AudioBackendComposition.h`
- Create: `src/Audio/AudioBackendComposition.cpp`
- Create: `src/Audio/AudioDiagnostics.h`
- Create: `src/Audio/AudioDiagnostics.cpp`
- Modify: `src/Audio/AudioPatch.h`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/AudioRuntimeState.*`
- Modify: `src/Audio/CMakeLists.txt`

- [ ] **Step 1: Make `AudioFeature` the sole startup-facing API**

Expose one preparation function that consumes owned `AudioSettings`, prepares
runtime state, and contributes the non-versioned DirectSound export request
and conditional ASIO-close versioned request created by prior plans. It does
not install either hook directly.

- [ ] **Step 2: Move factory/controller assembly together**

Move production controller configuration, WASAPI/ASIO backend factory wiring,
exact-history selection, and runtime-owner construction to
`AudioBackendComposition.*`. Keep backend implementations in their existing
DirectSound, WASAPI, ASIO, and Mixer directories.

- [ ] **Step 3: Move formatting away from control flow**

Move diagnostic stage/source-to-stable-label functions and startup failure
report construction to `AudioDiagnostics.*`. Keep real-time callback paths
allocation-free and free of per-call logging. Do not use reflect-cpp for these
stable diagnostic labels.

- [ ] **Step 4: Retire `AudioPatch` as a monolith**

Either reduce `AudioPatch.*` to a compatibility forwarding header/source with
no independent state and remove it in the same commit, or rename the public
entry to `AudioFeature` and update all callers. No duplicated old/new entry
path may remain.

---

## Task 2: Split Framerate callbacks by timing domain

**Files:**

- Create: `src/Patches/Framerate/FramerateFeature.h`
- Create: `src/Patches/Framerate/FramerateFeature.cpp`
- Create: `src/Patches/Framerate/FrameTimingHooks.h`
- Create: `src/Patches/Framerate/FrameTimingHooks.cpp`
- Create: `src/Patches/Framerate/EffectTimingHooks.h`
- Create: `src/Patches/Framerate/EffectTimingHooks.cpp`
- Create: `src/Patches/Framerate/MenuTimingHooks.h`
- Create: `src/Patches/Framerate/MenuTimingHooks.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/Framerate/FramerateGameProfile.*`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Group callbacks by native timing responsibility**

Move the 11 pre-effect/frame callbacks to `FrameTimingHooks`, the 34 effect
and two post-effect callbacks to `EffectTimingHooks`, and the six menu
callbacks to `MenuTimingHooks`. Keep existing pure policy/transform logic in
`FramerateEffectTiming`, `FramerateHookTransforms`, and
`FramerateMenuTiming`; the new files own native adapters and typed originals.

- [ ] **Step 2: Keep the profile as the complete manifest**

The split files do not reintroduce RVAs, byte prefixes, or their own hook
arrays. `FramerateGameProfile` remains the only 17-write/53-hook manifest and
binds each site to a callback exposed by the owning hook group.

- [ ] **Step 3: Make `FramerateFeature` composition-only**

It prepares shared timing state, declares dependencies, and contributes the
profile plan. Preserve frame/effect/post-effect/menu install order explicitly;
do not sort callbacks by filename or RVA.

- [ ] **Step 4: Remove the old mixed implementation**

Delete `FrameratePatch.*` after caller migration or leave only a temporary
forwarder within this task and delete it before commit. No transaction,
profile, state, or callback may be defined twice.

---

## Task 3: Split Widescreen hooks by rendering responsibility

**Files:**

- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenFeature.h`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenFeature.cpp`
- Create: `src/Patches/WindowedWidescreen/WindowHooks.h`
- Create: `src/Patches/WindowedWidescreen/WindowHooks.cpp`
- Create: `src/Patches/WindowedWidescreen/RenderHooks.h`
- Create: `src/Patches/WindowedWidescreen/RenderHooks.cpp`
- Create: `src/Patches/WindowedWidescreen/GameplayHudHooks.h`
- Create: `src/Patches/WindowedWidescreen/GameplayHudHooks.cpp`
- Create: `src/Patches/WindowedWidescreen/NetworkStatusHooks.h`
- Create: `src/Patches/WindowedWidescreen/NetworkStatusHooks.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.h`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenProfile.*`
- Modify: `src/Patches/CMakeLists.txt`

- [ ] **Step 1: Move window ownership and policy adapters**

`WindowHooks` owns native window creation/style/size/mouse adapters and uses
`NativeWindowPolicy`/`ResolutionModel`. It does not own render callbacks,
gameplay placement, or network status.

- [ ] **Step 2: Move renderer and resource adapters**

`RenderHooks` owns D3D9 entry/reset/present/draw/render-space callbacks and
bridges the Renderer Device Loss before/after reset callbacks. Compositor and
resource lifecycle remain in their existing policy/device classes.

- [ ] **Step 3: Separate gameplay HUD from network status**

`GameplayHudHooks` owns tune/gameplay feedback, MovieClip, common 2D/3D, and
HUD viewport adapters. `NetworkStatusHooks` owns only the network-status
matrix/site behavior. Keep their typed originals/state minimal and explicit.

- [ ] **Step 4: Keep one profile and one state owner**

`WindowedWidescreenProfile` remains the sole 40-byte/9-pointer/36-operation
manifest. `WindowedWidescreenFeature` constructs one runtime state and
contributes operations in existing manifest order. Hook-group files neither
install hooks nor own SafetyHook objects.

- [ ] **Step 5: Delete the old monolith**

Remove `WindowedWidescreenPatch.*` after all callers use the feature API. Do
not retain forwarding globals, copied manifest subsets, or duplicate fatal
reporting.

---

## Task 4: Split ConfigCompiler validation by settings domain

**Files:**

- Create: `src/Config/Validation/ValidationContext.h`
- Create: `src/Config/Validation/CommonValidation.h`
- Create: `src/Config/Validation/InputValidation.h`
- Create: `src/Config/Validation/InputValidation.cpp`
- Create: `src/Config/Validation/RegistryValidation.h`
- Create: `src/Config/Validation/RegistryValidation.cpp`
- Create: `src/Config/Validation/ExperimentalValidation.h`
- Create: `src/Config/Validation/ExperimentalValidation.cpp`
- Modify: `src/Config/ConfigCompiler.cpp`
- Modify: `src/Config/CMakeLists.txt`

- [ ] **Step 1: Keep aggregate ordering explicit**

`ConfigCompiler::Compile` remains the one public operation and calls validators
in the current document/declaration order. `ValidationContext` appends to one
`ConfigErrors` collection; each domain function preserves field paths, codes,
messages, related paths, leaf-validity suppression, and multi-error order.

- [ ] **Step 2: Move pure/common validation primitives only once**

Place `ValidateLeaf`, strict UTF-8 validation, and narrow reusable validation
state in `CommonValidation`. Keep the reflect-cpp declared-enum helper from
Plan 07 in `DeclaredEnum.h`; do not duplicate it in each domain.

- [ ] **Step 3: Keep input semantics together**

Move input mode/rate/threshold/key/controller/binding compatibility validation
to `InputValidation.*`, including XInput semantic subsets and HID address
rules.

- [ ] **Step 4: Keep registry/path semantics together**

Move registry DWORD/log-level and derived system/NESYS path validation to
`RegistryValidation.*` without moving filesystem I/O or persistence into the
compiler.

- [ ] **Step 5: Keep experimental dependencies together**

Move framerate, Widescreen, audio, test-mode storage, AutoPlay, SongUnlock,
and Absolute Judgement validation/dependency rules to
`ExperimentalValidation.*`. Return validated intermediate values needed by
`Compile`; the friend-only construction of immutable runtime settings remains
in `ConfigCompiler`.

- [ ] **Step 6: Run ConfigContract after each extraction**

```powershell
cmake --build --preset msvc32-debug --target gc_config_contract_tests
ctest --preset msvc32-debug -R ConfigContract --output-on-failure
```

---

## Task 5: Split NESYS request diagnostics by lifecycle

**Files:**

- Create: `src/Nesys/Diagnostics/RequestTracking.h`
- Create: `src/Nesys/Diagnostics/RequestTracking.cpp`
- Create: `src/Nesys/Diagnostics/RequestFormatting.h`
- Create: `src/Nesys/Diagnostics/RequestFormatting.cpp`
- Create: `src/Nesys/Diagnostics/RequestHooks.h`
- Create: `src/Nesys/Diagnostics/RequestHooks.cpp`
- Modify: `src/Nesys/Diagnostics/RequestPipelineDiagnostics.h`
- Modify: `src/Nesys/Diagnostics/RequestPipelineDiagnostics.cpp`
- Modify: `src/Nesys/CMakeLists.txt`

- [ ] **Step 1: Isolate tracked request/handle state**

Move maps, synchronization, endpoint/handle classification, correlation, and
lifetime transitions to `RequestTracking.*`. Preserve lock scope, invalid-
handle policy, cleanup order, and process-role assumptions.

- [ ] **Step 2: Isolate record formatting**

Move request/response summaries, byte previews, stable field labels, and sink
record construction to `RequestFormatting.*`. Formatting receives immutable
snapshots and does not acquire hook-state locks or perform Win32 I/O.

- [ ] **Step 3: Isolate detour adapters and plan contribution**

Move the 15 NESYS request-pipeline callback adapters, typed original slots,
and HookPlan contribution to `RequestHooks.*`. Preserve every API argument,
return/output/LastError rule and existing request observation order. The file
does not install or own hooks.

- [ ] **Step 4: Keep the facade small**

`RequestPipelineDiagnostics.*` becomes the feature-facing construction and
configuration facade. Delete it entirely if the new three modules make the
facade a name-only forwarder.

---

## Task 6: Create explicit role-specific startup roots

**Files:**

- Create: `src/Loader/ProcessStartup.h`
- Create: `src/Loader/ProcessStartup.cpp`
- Create: `src/Loader/GameStartup.h`
- Create: `src/Loader/GameStartup.cpp`
- Create: `src/Loader/NesysStartup.h`
- Create: `src/Loader/NesysStartup.cpp`
- Create: `src/Loader/StartupFailure.h`
- Create: `src/Loader/StartupFailure.cpp`
- Modify: `src/Loader/StartupConfiguration.*`
- Modify: `src/Loader/GameVersionedStartupPlan.*`
- Modify: `src/Loader/NesysVersionedStartupPlan.*`
- Modify: `src/Loader/NonVersionedHookPlan.*`
- Modify: `src/Loader/VersionedStartupExecutor.*`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::loader {

[[nodiscard]] std::expected<PreparedProcessConfiguration, StartupError>
PrepareProcessStartup(HMODULE loader_module) noexcept;

[[nodiscard]] std::expected<void, StartupError>
StartGame(HMODULE loader_module,
          GameProcessConfiguration configuration) noexcept;

[[nodiscard]] std::expected<void, StartupError>
StartNesys(HMODULE loader_module,
           NesysProcessConfiguration configuration) noexcept;

[[noreturn]] void AbortForStartupError(const StartupError&) noexcept;

} // namespace gc::loader
```

- [ ] **Step 1: Keep common preparation before role branching**

`ProcessStartup` detects the current role, selects/initializes the matching
session log, reads and compiles strict `config.toml`, applies the configured log
level, and returns the role-specific immutable configuration variant. It
performs no game-only initialization in the NESYS process.

- [ ] **Step 2: Implement the complete game flow**

In explicit source order:

1. Detect the game build/image variant.
2. Ask every mandatory and enabled game feature for its versioned profile/plan.
3. Validate the complete versioned plan set without mutation.
4. Build, resolve, and collision-validate all game non-versioned export hooks.
5. Construct/publish feature runtime state and shared Win32 handler tables.
6. Install approved versioned writes, global slots, and SafetyHook detours.
7. Install non-versioned hooks in the Plan 01 declared order.
8. Return success.

Include GameCompatibility, AutoPlay, SongUnlock, Switch input, Absolute
Judgement, Framerate, Countdown, Test Mode Timing, Renderer Device Loss,
Widescreen, and conditional ASIO close. Optional disabled features contribute
no sites.

- [ ] **Step 3: Implement the complete NESYS flow**

When fixed-RVA NESYS behavior is enabled, detect/select the NESYS profile and
validate its complete versioned plan. Build only NESYS-role runtime state and
export hooks, publish them, install the optional ping detour, then install the
NESYS export hooks in existing order. Never initialize game input, audio,
RFID/storage, gameplay patches, or renderer state.

- [ ] **Step 4: Centralize typed startup failure formatting**

`StartupFailure` converts configuration, unsupported-profile, versioned
contract, RuntimeImage, hook collision/target, SafetyHook installation, and
feature preparation errors into the common fatal-process report. Preserve
process-specific log selection, precise feature/site/RVA/address/captured
Win32/SafetyHook detail, one popup, formatting fallback, and unconditional
abort.

- [ ] **Step 5: Make DllMain a thin adapter**

On `DLL_PROCESS_ATTACH`, call `DisableThreadLibraryCalls`, prepare common
startup, dispatch exactly one role-specific root, and return `TRUE` on success.
Catch every exception at the ABI boundary and call the terminal reporter. Do
not retain feature includes, local fatal-format functions, plan assembly,
settings copies, hook installation, or a recoverable `return FALSE` branch in
DllMain.

---

## Task 7: Replace the coarse CMake target graph

**Files:**

- Modify: `src/CMakeLists.txt`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `src/Nesys/CMakeLists.txt`
- Modify: every affected nested `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: root `CMakeLists.txt` only if dependency metadata still needs cleanup

- [ ] **Step 1: Delete `gc_runtime_patches`**

Replace the umbrella target with these coherent feature targets:

```text
gc_runtime_image
gc_game_version
gc_patch_game_compatibility
gc_patch_auto_play
gc_patch_song_unlock
gc_patch_framerate
gc_patch_countdown
gc_patch_absolute_judgement
gc_patch_switch_input
gc_test_mode_timing
gc_renderer_device_loss
gc_windowed_widescreen
```

Each target owns its feature profile, callbacks, state/policy it genuinely
contains, and only its direct dependencies. Existing coherent Audio, Input,
RFID, Config, Logging, and NESYS domain targets may remain; do not merge them
to match this list.

- [ ] **Step 2: Add the composition targets**

Define `gc_game_startup` and `gc_nesys_startup`. They may depend downward on
feature targets and shared infrastructure; no feature/shared target may depend
back upward on either composition target or on iDmac.

Retain the coherent shared targets `gc_platform_win32`, `gc_fatal_process`,
`gc_hooking`, `gc_runtime_image`, and `gc_game_version`. In particular,
feature-level fatal paths depend on `gc_fatal_process`, never on Loader.

- [ ] **Step 3: Enforce third-party ownership**

Only `gc_hooking` names `safetyhook::safetyhook`; callback ABI headers may be
available transitively from it. Only `gc_config` names reflect-cpp include
directories/targets. No CMake or source dependency mentions MinHook. Final
`iDmacDrv32` links project-owned targets rather than third-party hook/reflection
targets directly.

- [ ] **Step 4: Preserve final artifacts**

Keep `iDmacDrv32`, ConfigGUI, helper executable, CTest executable/test names,
x86/static-runtime properties, `.def` file, ordinals, output paths,
corresponding-source packaging, and installed runtime file set unchanged
except for the intentional MinHook dependency removal.

- [ ] **Step 5: Document the final graph**

Replace the Plan 01 `before` target graph with a separate `after` section in
`docs/architecture/loader-cleanup-baseline.md`. Show dependency direction and
which target owns RuntimeImage, SafetyHook, reflect-cpp, each feature profile,
and each process startup root.

---

## Task 8: Run the legacy-removal audit

**Files:**

- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Audit hook and transaction leftovers**

```powershell
rg -n -i 'minhook|MH_[A-Za-z0-9_]+' CMakeLists.txt cmake src tests tools
rg -n 'PatchTransaction|HookTransaction|Prepare.*Hook|Activate.*Hook|Rollback.*Hook|rollback_(memory|win32|complete)|reverse rollback' src tests tools
rg -n 'safetyhook::create_|safetyhook::InlineHook|safetyhook::MidHook' src
```

Expected: no MinHook; no native patch/hook rollback transaction; direct
SafetyHook creation/ownership only in `Platform/Win32/Hooking`. Classify any
domain use of the word rollback rather than deleting unrelated state-machine
semantics by regex.

- [ ] **Step 2: Audit native-memory ownership**

```powershell
rg -n 'VirtualProtect|ReadProcessMemory|WriteProcessMemory|FlushInstructionCache|InterlockedCompareExchangePointer|safetyhook::unprotect' src
```

Expected: loaded-image mechanics only in RuntimeImage (plus clearly unrelated
memory uses documented in the baseline). Feature profiles contain contracts,
not memory API calls.

- [ ] **Step 3: Audit dispatch and dependency direction**

```powershell
rg -n '#include ".*(Rfid|TestModeStorage|SystemPath|Nesys).*"' src\Win32Hooks
rg -n 'reflectcpp_SOURCE_DIR|reflectcpp|<rfl' src tools -g CMakeLists.txt -g '*.h' -g '*.cpp'
rg -n 'safetyhook::safetyhook' . -g CMakeLists.txt
```

Expected: Win32 hook adapters include shared dispatch contracts, not feature
implementations; only `gc_config` directly owns reflect-cpp; only
`gc_hooking` names the external SafetyHook target.

- [ ] **Step 4: Audit fixed native facts**

Search each migrated feature for executable-base additions, concrete RVAs,
byte arrays, pointer targets, layout offsets, and direct hook creation. Every
version-specific fact must be in its feature-owned profile and every enabled
profile must be reachable from exactly one role-specific global barrier.

- [ ] **Step 5: Close the seam ledger**

For every Plan 01 `Actions`/`Api` row, record the final file and `removed` or
`retained` decision with evidence. There may be no undecided row. Retained
seams must satisfy the deletion test and have a real production boundary.

---

## Task 9: Perform final static verification and handoff

- [ ] **Step 1: Fresh-configure both target graphs**

Run from x86 MSVC Developer PowerShell:

```powershell
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
cmake --fresh --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4 --output-on-failure
cmake --fresh --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4 --output-on-failure
```

- [ ] **Step 2: Compare the final DLL to the baseline contract**

Run `dumpbin /headers` and `dumpbin /exports` on the Release
`iDmacDrv32.dll`. Compare machine type, DLL characteristics, runtime linkage,
export names, ordinals, and decorated x86 signatures against the Plan 01
baseline. Any ABI/export difference blocks completion.

- [ ] **Step 3: Inspect the final diff and repository state**

```powershell
git diff --check
git diff --stat
git status --short --branch
```

Review every deletion and CMake edge. Do not hide unrelated user changes or
commit generated build output.

- [ ] **Step 4: Record the Markdown closeout**

Complete the `after` sections of
`docs/architecture/loader-cleanup-baseline.md` with counts, target graph,
remaining intentional seams, audit output, Debug/Release/CTest evidence, and
the explicit runtime rows still untested. Do not generate an HTML report.

- [ ] **Step 5: Commit**

```powershell
git add -- src tests CMakeLists.txt cmake docs\architecture\loader-cleanup-baseline.md
git commit -m "Complete loader architecture cleanup"
```

- [ ] **Step 6: Hand off runtime acceptance separately**

Report static/build proof in the discussion and request explicit authorization
before deployment or target-process execution. The future runtime matrix must
cover both known 4.71 images, game and NESYS roles, shared Win32 consumers,
locale/crash behavior, DirectSound/WASAPI/ASIO, all enabled game patch
families, NESYS ping/request behavior, RFID/storage, and every future older
profile. No static result satisfies those rows.
