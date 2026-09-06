# Groove Coaster 2.06 and NESYS 2.86.1 Implementation Plan

> For execution: use `superpowers:executing-plans` task by task. This is a
> plan authorized for inline implementation on 2026-09-06. Deployment and
> game/process actions require separate authorization.
> Use CLion MCP for loader source work and the normal shell for Git, IDA,
> compilation, and commands. Do not dispatch agents unless the user requests it.

**Execution status:** Tasks 1-7 and Task 8's source/build preparation are complete.
Both complete presets passed; CLion reported no problems in 26 changed source
files. Operator runs remain pending. See the
[implementation and validation record](../../reverse-engineering/gc206-implementation-2026-09-06.md).

**Goal:** Support the analyzed 2.06 game and its NESYS 2.86.1 executable while
preserving existing 4.71/2.97 behavior and all applicable loader features.

**Architecture:** Extend the completed feature-owned version profiles and
existing startup coordinator. Each process selects its own binary identity;
all required native operations enter the existing approved plan before
installation. Patch algorithms stay shared, with explicit native ABI variants
where the older instructions or layouts differ.

**Tech stack:** Windows x86, C++23, MSVC static runtime, CMake presets,
SafetyHook, IDA-CLI/IDAPython/Hex-Rays, CLion MCP.

**Spec and evidence:** The existing
[loader cleanup design](../specs/2026-09-05-loader-codebase-cleanup-design.md),
[AutoPlay safety design](../specs/2026-09-03-native-auto-play-safety-design.md),
[widescreen design](../specs/2026-08-26-windowed-widescreen-stage-design.md), and
the version-specific decisions in the
[September 6 native audit](../../reverse-engineering/gc206-new-patches-2026-09-06.md).
Where the older designs describe rollback or old installation machinery,
the completed cleanup design and current `AGENTS.md` take precedence.

## Global constraints

- Source and plans remain in `H:\gc\artifacts\GCLoader`.
- Analysis targets are `H:\gc2_game\game_decrypted.exe.i64`,
  `H:\gc\game471.exe.i64`, and `H:\gc2_game\NesysService.exe.i64`.
- Preserve the iDmac DLL/export names, ordinals, x86 calling conventions,
  established return values, and initialized caller outputs.
- Game and NESYS roles remain separate; the NESYS process receives no
  game-only input, audio, RFID/storage, renderer, or gameplay initialization.
- Exact SHA-256 matches select the project's known profile directly.
  Preserve the existing omission of redundant content comparison on that
  path. Unknown hashes require an unambiguous structural candidate and the
  complete local contract for mandatory plus enabled versioned features.
- Retain plan shape, address/accessibility, dependency, and overlap checks.
  A failed mandatory/enabled contract aborts before versioned mutation.
  A later write/hook failure is fatal; do not introduce reverse rollback.
- Win32/API/export hooks remain outside the global versioned-site barrier.
- Runtime settings remain strict, complete, immutable per launch, and without
  hot reload. Version detection does not introduce a user version selector.
- Do not change the game's authored 60 Hz time unit or couple faster input
  transport to judgement/score cadence.
- Windowed widescreen remains upright desktop output. No fullscreen,
  borderless, monitor rotation, or Windows rotation feature is added.
- For native patch work, follow `AGENTS.md`: no TDD, new/extended unit tests,
  fake images, synthetic backends, copied binary fixtures, callback recorders,
  or standalone test/verification harnesses. Establish contracts directly in
  IDA and use compilation as build proof. Do not hash analysis artifacts.
- No deployment or process lifecycle operation follows from this plan.
  Real-process acceptance requires the user's explicit authorization and
  concrete observations.

## Scope and choices

Use one loader DLL with two game profiles and two independently selected
NESYS profiles. Adding per-feature profiles to the existing architecture is
the smallest implementation. A separate 2.06 loader would duplicate policy;
scattered version checks and a global RVA delta cannot express the native
differences already established in IDA.

The implementation includes AutoPlay and current widescreen behavior. Start
development with the mandatory boot contracts, then add the optional families
and high-FPS variants. Intermediate commits are not a declaration that the
whole version is supported. Unsupported enabled features must continue to fail
preflight until their task is complete.

The only feature omissions are native paths absent from 2.06: two later remote
cadence hooks, three unlock-reward counter hooks, unlock-reward prompt
inspection, and six countdown call sites. These are omissions within otherwise
supported features, not reasons to disable high-FPS or countdown altogether.

## Task 1: Register exact identities and report the selected build

**Files:**

- Modify `src/Patches/GameVersion/GameBuild.h`.
- Modify `src/Nesys/NesysBuild.h`.
- Modify `src/Patches/GameVersion/KnownImages.cpp`.
- Modify `src/Patches/GameVersion/VersionedPlanDiagnostics.cpp`.
- Modify `src/Patches/GameVersion/VersionedPlan.cpp`: its context validator
  retained explicit 4.71/2.97 restrictions and must admit the new combinations.
- Inspect `src/Patches/GameVersion/BuildDetector.cpp`,
  `src/Loader/GameVersionedStartupPlan.cpp`, and
  `src/Loader/NesysVersionedStartupPlan.cpp`.

**Interfaces:** Keep `DetectGameBuild`, `DetectNesysBuild`, `GameDetection`,
`NesysDetection`, and the approved-plan interfaces. Add enumerators
`GameBuild::groove_coaster_206` and `NesysBuild::service_2861` after existing
enumerators. Keep clean/legacy-patched/locally-verified image variants distinct.

- [x] Add these descriptors using the existing `Digest` helper:

```cpp
{GameBuild::groove_coaster_206, GameImageVariant::clean,
 Digest("556722C899ED75F33F17900BCD94BBB07288789547A30A982B3FB6ABFF6FB61C"),
 3405312, IMAGE_FILE_MACHINE_I386, 0x00400000, 0x003E7000, 0x565828C3},

{nesys_service::NesysBuild::service_2861,
 nesys_service::NesysImageVariant::original,
 Digest("328C48B01F884E0B32B39E44936661B224A4D9E48C679BE3F8CA3AE74A9760A4"),
 368640, IMAGE_FILE_MACHINE_I386, 0x00400000, 0x0005C000, 0x5451056C},
```

- [x] Replace the diagnostics' current game-vs-service constant selection
  with exhaustive switches on the actual build enum, reporting
  `groove_coaster_206` and `nesys_service_2861` for the new values.
- [x] Keep the three existing known-image descriptors unchanged. There is
  no independently analyzed patched 2.06 hash to register. A future known
  dump can receive its own verified descriptor; do not label an arbitrary
  altered 2.06 executable `legacy_patched` automatically.
- [x] Confirm source control flow still selects the NESYS profile from the
  child executable itself. No game-to-NESYS version mapping is added.
- [x] Compile Debug. At this intermediate point, missing 2.06 feature
  profiles are expected to reject startup; do not bypass that rejection.

## Task 2: Populate mandatory compatibility, renderer and Test Mode profiles

**Files:**

- Modify `src/Patches/GameCompatibility/GameCompatibilityProfile.cpp`.
- Modify `src/Patches/RendererDeviceLoss/RendererDeviceLossProfile.cpp`.
- Modify `src/Patches/TestModeTiming/TestModeTimingProfile.cpp`.
- Inspect matching headers and runtime binding consumers.

**Interfaces:** Preserve `BuildGameCompatibilityPlan`,
`BuildRendererDeviceLossPlan`, `BuildTestModeTimingPlan`, and the feature-owned
runtime preparation functions. Add explicit 2.06 clean and locally-verified
profile selection; no fallback to the 4.71 profile.

- [x] Read the native contracts from the earlier reports:
  `01_game_compatibility/GAME_COMPATIBILITY_206.md`,
  `02_renderer_device_loss/RENDERER_DEVICE_LOSS_206.md`, and
  `03_test_mode_timing/TEST_MODE_TIMING_206.md` under
  `H:\gc\tmp\gc206_patch_analysis`.
- [x] Reopen the listed sites in IDA as the profile entries are written.
  Reconcile old hook signatures with the current SafetyHook protected spans:
  decode whole instructions covering the detour, check interior incoming
  branches, and record the required continuation/helper contracts. Do not
  copy a short historical signature as proof of a longer detour span.
- [x] Implement the four compatibility writes:

| Site | RVA | Original | Replacement |
| --- | --- | --- | --- |
| Mouse dispatch | `0x0A3FF6` | `75 02` | `90 90` |
| Dongle failure | `0x0F7E9B` | `75 3B` | `EB 3B` |
| Dongle transmit | `0x0F90F6` | `E8 45 F6 FF FF` | Five NOPs |
| RFID COM character | `0x2B68C7` | `31` | `32` |

- [x] Populate renderer hooks at `0x0DA6B8`, `0x0DA7A2`, `0x0DB7E8`,
  `0x0DB92E`, `0x0DCA07`, `0x0DCA94`, and continuations at `0x0DA722`,
  `0x0DA7B9`, `0x0DBAE6`, `0x0DCEF9`. Preserve its existing register ABI,
  fields `+0x484/+0x778`, resource ownership, and fatal-failure behavior.
- [x] Populate Test Mode constructor `0x15EA20`, render `0x15E940`, row
  patch `0x15EA55` (`6A 0B -> 6A 0C`), every native helper and vtable entry
  from the report, carrier size `0x1D4`, sound vtable `0x2B92AC`, and timing
  globals `0x39367C/0x393680`. Bind the existing UI behavior through the
  approved profile; preserve all native form/grid ownership.
- [x] Compile Debug and Release, recording only compilation/linkage proof.

## Task 3: Port the mandatory judgement lifecycle and optional exact judgement

**Files:**

- Modify `src/Patches/AbsoluteJudgement/AbsoluteJudgementProfile.cpp`.
- Modify `src/Patches/AbsoluteJudgement/NativeJudgementAbi.h` only if the
  existing layout cannot represent a proven native difference.
- Inspect `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp` and
  the other native callback consumers in that directory.

**Interfaces:** Keep `BuildAbsoluteJudgementPlan(build, variant, enabled)`.
Both its enabled and disabled operation sets must have 2.06 profiles.
The disabled feature still installs lifecycle hooks and reads configuration.

- [x] Materialize the native contract in
  `06_absolute_judgement/ABSOLUTE_JUDGEMENT_206.md`, rechecking its ABI and
  current detour spans directly in IDA.
- [x] Set initialization `0x1C2B89`, stage entry/exit
  `0x1C451F/0x1C5102`, owned loop guard/tail `0x20D269/0x20D386`, recognition
  `0x1AF5A0`, score `0x1A8870`, grade `0x1A9D20`, and all input/accessor
  targets from the detailed report.
- [x] Set layout values: judgement Tune local `-0x334`, semantic Tune local
  `-0x36C`, active player `+0x728`, config game/hold/slide `+0x2C/+0x68/+0x6C`,
  arrange publication `+0xA8`, free taps `+0xED/+0xEE`. Retain the verified
  common judgement/score collections, score counters, Booster pointer, target
  float index, and sound group 2.
- [x] Trace each callback against its old entry registers before reusing
  it. Bind a feature-local adapter only where the decoded ABI differs; keep
  recognition, grade, score, and timing arithmetic shared.
- [x] Compile both configurations. Verify by source/native inspection that
  disabled judgement still has every target its lifecycle callbacks read.

## Task 4: Finish simple optional profiles and the independent NESYS port

**Files:**

- Modify `src/Patches/SongUnlock/SongUnlockProfile.cpp`.
- Modify `src/Input/Switch/SwitchInputProfile.cpp`.
- Modify `src/Audio/Asio/AsioCloseProfile.cpp`.
- Modify `src/Nesys/Network/NesysPingProfile.cpp`.
- Inspect `src/Audio/AudioFeature.cpp`,
  `src/Loader/NesysVersionedStartupPlan.cpp`, and
  `src/Nesys/Network/NesysNetworkConfig.cpp`.

**Interfaces:** Preserve existing `BuildSongUnlockPlan`, `BuildSwitchInputPlan`,
ASIO profile/runtime preparation, `PingProfileFor`, and `BuildNesysPingPlan`.

- [x] Populate the old Switch held/pressed/diagonal RVAs
  `0x225790/0x225860/0x1ABDF0` with the exact ABI from
  `05_small_patches/SMALL_PATCHES_206.md`.
- [x] Add the ordinary ASIO close seam at `0x20965E`, preserving the
  native final `CoUninitialize` ownership and existing ASIO callback lifetime.
- [x] Add the old SongUnlock gate:

```text
RVA 223F10: 0F 85 CE 01 00 00 -> E9 CF 01 00 00 90
Native branch target: 2240E4
```

  This is the 2.06 all-song path based on system config `+0x24`, with its own
  song vector at `+0x758`. Do not import the later scheduler `+0x8C` gate or
  synthesize missing songs/difficulties.
- [x] Add `service_2861` original/locally-verified ping profiles with RVA
  `0x8E20` and the 32-byte prefix in the new audit. Reuse
  `OnServicePingAddress`; EAX remains the input target, ECX the ping owner.
- [x] Ensure service startup contributes only its own versioned ping plan.
  Keep API, registry, process launch and address policy unchanged.
- [x] Compile Debug and Release. Real ASIO and NESYS acceptance remains
  deferred to Task 8's operator run.

## Task 5: Add AutoPlay as one complete 2.06 safety feature

**Files:**

- Modify `src/Patches/AutoPlay/AutoPlayProfile.cpp`.
- Inspect `src/Patches/AutoPlay/AutoPlayPatch.cpp`,
  `src/Patches/AutoPlay/AutoPlayMarker.cpp`, and their headers.
- Update the AutoPlay safety design with the older build's save-path variant.

**Interfaces:** Preserve `BuildAutoPlayPlan`, `PrepareAutoPlayRuntime`,
`ActivateAutoPlayMarker`, and the five-operation feature-plan shape. Keep the
existing configuration flag; no second AutoPlay or no-save switch is added.

- [x] Implement 2.06 native text `0x5ABF0` and marker `0x49FB9` from the
  audit's exact bytes. Preserve cdecl `(float x, float y, uint32_t argb,
  const char* format, ...)`, and resolve the function only from approval.
- [x] Replace the 4.71-only parser operation with this old-profile operation:

```cpp
BytePatchOperation{
    {FeatureId::auto_play, "suppress_card_save",
     VersionedOperationKind::byte_patch, 0x001EF52A, 2,
     PatternOf<0x74, 0x0F>(), PatternOf<0x90, 0x90>(), 2},
    PatternOf<0x90, 0x90>()},
```

- [x] Add the `+0xA6` getter operation at `0x30B1A` before the `+0xA5`
  getter operation at `0x30AFA`; each replaces its six-byte load with
  `B0 01 90 90 90 90`. Preserve the installation order: marker preparation,
  save suppression, complete-IsMute, native AutoPlay; activate the marker
  only after every required write/hook has succeeded.
- [x] Record the native containment chain: old state zero now takes state
  13 before card-save construction; states 13–17 retain completion handling;
  result exporter `0x1AF100` exits on `+0xA5` before opening a file. Do not
  force immediate terminal success or block all NESYS communication.
- [x] Confirm AutoPlay disabled contributes no operations or native reads.
  Preserve the existing feature-wide fatal behavior for an enabled mismatch.
- [x] Compile both configurations. No claim of score-save protection in a
  running game is made before the Task 8 observations.

## Task 6: Port framerate, audio-clock variants and countdown capabilities

**Files:**

- Modify `src/Patches/Framerate/FramerateGameProfile.h/.cpp`.
- Modify `src/Patches/Framerate/FramerateNativeAbi.h`.
- Modify `src/Patches/Framerate/FrameratePatchPlan.cpp`,
  `FramerateFeature.cpp`, `FrameTimingHooks.cpp`, `EffectTimingHooks.cpp`,
  `FramerateHookTransforms.h/.cpp`, and `MenuTimingHooks.cpp` as required by
  the specific native differences below.
- Modify `src/Patches/Countdown/CountdownProfile.h/.cpp`.
- Inspect `src/Patches/Framerate/FramerateTimingRuntime.h/.cpp` and
  the profile's binding switch.

**Interfaces:** Keep `BuildFrameratePlan` and `BuildCountdownPlan`. Profiles
expose spans over build-owned static arrays so absent hooks/targets/calls are
actually absent from iteration. Do not fill 4.71-sized arrays with zero RVAs.

```cpp
// In FramerateGameProfile: static backing arrays outlive every prepared plan.
std::span<const FramerateWriteContract> writes;
std::span<const FramerateHookContract> hooks;
std::span<const FramerateTargetContract> targets;

// In CountdownProfile:
std::span<const game_version::VersionedOperation> operations;
```

- [x] Add the always-required OuterFrame hook `0x49F40`, including the
  ordinary 60 Hz/DirectSound profile. Populate the 17 write roles and 48
  applicable hook roles from `08_framerate/FRAMERATE_206.md`.
- [x] Preserve operand-only writes where the refactored implementation
  writes scalar immediates. Decode old instruction prefixes to locate each
  operand precisely; do not apply an instruction-start RVA to a four-byte
  value writer. Pay particular attention to the three countdown encoding
  variants and relocated palette/chart float operands.
- [x] Represent native register selection in the feature profile. Add a
  feature-local EAX/ECX/EDX selector, and bind the existing authored-operand
  callbacks from profile metadata. A suitable narrow interface is:

```cpp
enum class FramerateRegister { eax, ecx, edx };
// FramerateHookContract, used by the authored-operand hook IDs:
std::optional<FramerateRegister> authored_operand_register;
// FramerateNativeLayout, read by the relevant callbacks:
FramerateRegister tune_countdown_owner;
FramerateRegister countdown_asset_source;
FramerateRegister tutorial_elapsed_source;
bool unlock_reward_prompt_available;
```

  Read/write only the selected register. The original relocated instruction
  must retain its native destination, stack store and continuation. If a
  callback deliberately emulates that store, include its exact destination
  in the feature-owned ABI rather than assuming the 4.71 instruction.
  Populate these selectors explicitly for both builds. Reject an authored
  operand hook without its required selector; do not silently choose a
  register from the hook ID. Include `<span>` and `<optional>` in the headers
  that own the new members.
- [x] Populate the verified old ABI differences:

| Native role | 2.06 selection |
| --- | --- |
| Countdown compare owner | EAX, field `+0x1D14` |
| Authored operand lifetime A / frame A | EAX / ECX |
| Authored operand lifetime B / frame B | EDX / EAX |
| Direct effect frame operand | ECX |
| Countdown asset source/destination | EDX / native `[ECX+8]` store |
| Tutorial elapsed source/store | EAX / native `EBP-0x84` store |
| Judgement / semantic Tune locals | `EBP-0x334` / `EBP-0x36C` |
| Signed phase local | `EBP-0x204` |
| Player-position array | `+0x1D4C` |
| Ranking / HitChart continuation | `0x1EA97F` / `0x22C007` |
| Audio-resync suppressed continuation | `0x20CD07` |

- [x] Make the audio target's name describe a continuation rather than
  assuming an epilogue. The old continuation must preserve the later native
  debug/overlay work. Register every helper/continuation read by the selected
  DirectSound, WASAPI legacy/shared-clock, and ASIO clock branches.
- [x] Use the native target RVAs `get_config=0x11C0`,
  `get_sound_manager=0x1E41D0`, `get_group_cursor=0x1E5C60`, and
  `advance_gameplay_effect=0x1CD3F0`; source operand is `0x2B95E0`.
- [x] Omit `RemoteCadenceA/B`, the three `UnlockReward*Store` rows, and
  their three unused continuation targets from the 2.06 manifests.
  `unlock_reward_prompt_available=false` must prevent prompt-owner inspection
  before any absent-layout read, while general MovieClip tick gating remains.
- [x] Add the 26 countdown calls in
  `09_countdown_timer/COUNTDOWN_TIMER_206.md`, replacing each native delta
  call with `D9 EE 90 90 90`. Omit the later UnlockReward, Trophy and reward
  transition pairs; preserve all thirteen old semantic pairs.
- [x] Trace selected operations for native and non-native timing with each
  audio backend against real IDA targets. Inspect overlaps with absolute
  judgement and widescreen in the final native address set. This is analysis,
  not a fabricated-memory execution test.
- [x] Compile Debug and Release. Preserve timing arithmetic and existing
  runtime diagnostics; 2.06 high-FPS acceptance remains unobserved.

## Task 7: Populate the complete current widescreen behavior

**Files:**

- Modify `src/Patches/WindowedWidescreen/WindowedWidescreenProfile.cpp`.
- Inspect its header, `WindowedWidescreenAbi.h/.cpp`, `GameplayHudHooks.cpp`,
  `NetworkStatusHooks.cpp`, `RenderHooks.cpp`, and `NativeCanvasCompositor.cpp`.
- Update the widescreen native evidence documentation with the older profile.

**Interfaces:** Preserve `ProfileFor`, `BuildWidescreenPlan`, profile-owned
layout, existing hook binding and runtime ownership. The current 86/9 contract
and 83-binding shape can represent this port; no capacity-only expansion is
needed merely to add the 2.06 build.

- [x] Populate all 86 byte contracts from the new audit's final table and
  seven original pointer contracts from the earlier widescreen report. Use
  exact old bytes and whole-instruction spans; carry longer original-byte
  context where the current profile validates it.
- [x] Add the newer pointer cells `0x2806F0 -> 0x0D5FA0` and
  `0x27DE18 -> 0x0C1870`, resolving pointers against the loaded base through
  the current vtable registry. Set the old draw visitor vtable to `0x27DDCC`.
- [x] Set `tune_effect_collection_offset=0x1D60` and batch pointer
  `0x3AA95C`. Import the other versioned vtables/globals from the old report.
  Preserve collection `+0x0C/+0x10`, root manager `+0x74`, counter local
  `EBP-0x14`, matrix stack `+0x1A0`, and verified common renderer fields.
- [x] Use the semantic Test Mode pair `0x207F3C/0x207F41`.
  Do not use the rejected alignment candidate `0x207F1A` as an end hook.
- [x] Preserve all twenty bar pairs and four counter pairs. The packet
  allocation hook is `0x1CD700`, packet begin/end are `0x1CDC14/0x1CDC19`,
  and root begin/end are `0x1CDD38/0x1CDD3D`.
- [x] Keep the current six primary judgement text slots and nine reached
  tutorial slots. Confirm the semantic selectors in `0x215560` and producer
  `0x1C1560`; do not include initialized-but-unselected B7. Preserve exclusion
  of track-position and result roots.
- [x] Check the producer/drain lifecycle: invalidate reused packet addresses,
  retain selected ownership until submit, restore the enclosing draw space,
  and require an empty packet/owner scope at the effects-pass end. Reuse the
  compositor policy and keep private effect queues outside general flushes.
- [x] Compile Debug and Release. These builds do not establish HUD placement,
  marker visibility, reset handling, or packet runtime behavior.

## Task 8: Integrate, document and obtain real-process acceptance

**Files:**

- Inspect `src/Loader/GameVersionedStartupPlan.cpp`,
  `src/Loader/NesysVersionedStartupPlan.cpp`, and
  `src/Loader/VersionedStartupExecutor.cpp`.
- Update the existing support documentation and the new native audit with
  implementation status and compilation results.
- Record future operator observations in
  `docs/reverse-engineering/gc206-runtime-acceptance.md` when a run is authorized.

- [x] Confirm every mandatory feature has a 2.06 plan, including disabled
  judgement lifecycle and ordinary OuterFrame. Confirm every enabled optional
  feature either supplies its complete supported plan or rejects preflight.
- [x] Recheck native spans and dependencies introduced by the final source
  change. Confirm one complete approved versioned plan precedes installation,
  with the current game/service and API-hook boundaries intact.
- [x] From an x86 MSVC developer shell, build the complete two preset graphs:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
cmake --preset msvc32-release
cmake --build --preset msvc32-release
git diff --check
git status --short
```

  Do not add or run synthetic native-patch tests to replace an operator run.
  Review the final diff and keep any commits limited to this port's files.
- [x] Prepare a concrete runtime artifact/configuration and the checks below
  for review. Wait for explicit deployment and exact process-action approval.
  Do not restart the existing 4.71 game, service, IDE, or IDA daemon.
- [ ] During the authorized run, record executable/DLL identities, settings,
  `loader-log.txt`, `loader-service-log.txt`, and operator observations for:

| Run | Required observations |
| --- | --- |
| 2.06 ordinary 60 Hz, DirectSound | Boot, Test Mode/timing rows, RFID/card, input, storage, audio, NESYS child identity |
| Song unlock and Switch input | Native song availability, pressed/held/diagonal behavior |
| AutoPlay off/on | Native judgement and hidden-note completion; persistent marker; no result CSV; ordinary card/result save operations absent; native completion handling and unchanged protected server/card results |
| Widescreen | Selected bars, names, all four counter draw forms, primary judgement text, note tutorials, network icons, Test Mode, and device reset; track effects remain correctly placed |
| High-FPS and countdown | Authored chart/judgement/score timing, menu progression, thirteen countdown pairs, audio clock behavior for each supported backend |
| 4.71/2.97 regression | Existing supported configuration still boots and preserves the previously accepted AutoPlay/widescreen/audio/input behavior |

- [x] Mark each observation as passed, failed with exact evidence, or not
  performed. Keep compilation/static completion separate from runtime
  acceptance. A plan-only or build-only result must not be called cabinet
  support.

## Implementation clarifications

- Source review found the shared scalar writer already preserves each native
  instruction prefix and replaces its final four-byte operand. The old
  countdown encodings therefore use that existing writer with exact old bytes.
- The 2.06 effect-timing diagnostic inventory covers its 32 mapped timing hooks;
  the 4.71 registration/duration inventory is not copied as old-build evidence.
- All pending operator observations are recorded as not performed in the
  implementation record. No deployment or process action was taken.

## Completion criteria

The implementation is ready for operator validation when both build graphs
compile, all required native contracts are reconciled with the real binaries,
and the final source preserves complete preflight and process ownership.
The version is runtime-accepted only after the relevant authorized observations
above succeed. No runtime result is inferred from the September 2 audit, the
new address mappings, the refactor's old 4.71 smoke tests, or compilation.
