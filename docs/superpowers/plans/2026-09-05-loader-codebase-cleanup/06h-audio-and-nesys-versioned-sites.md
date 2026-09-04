# Audio and NESYS Versioned-Site Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the game-process ASIO ordinary-close detour and the NESYS-process ping redirect into separate feature-owned profiles and process-specific global preflight barriers.

**Architecture:** ASIO contributes one game-versioned mid hook only when ASIO is selected. NESYS contributes one NESYS-versioned mid hook only when the NESYS feature plan enables `service_ping_redirect`. Both use RuntimeImage contracts and the central HookRegistry; neither owns a SafetyHook object or a prepare/activate/rollback mini-transaction.

**Tech Stack:** C++23, RuntimeImage, typed game/NESYS build profiles, SafetyHook v0.7.0 mid hooks through HookRegistry, Win32 x86, CMake/Ninja/MSVC, IDA-CLI.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06g first.
- Keep the game and NESYS loaded images, build enums, profile selectors,
  startup plans, logs, and hook registries explicitly separated.
- The ASIO close contract is enabled only for
  `AudioBackend::asio`. DirectSound and WASAPI contribute no fixed game site.
- `service_ping_redirect` is enabled only for the NESYS process when network
  virtualization is enabled. The game process never contributes the NESYS
  ping RVA.
- Preflight proves identity/contracts; successful SafetyHook creation and
  enablement prove installation. Do not collapse these into one status.
- Revalidate both native seams against the actual binary/IDB before moving
  them. Do not infer an older-game or older-NESYS profile from the 4.71 data.
- Do not add fake executable memory, a mock hook engine, copied binary
  fixtures, or callback-recording tests.

---

## Task 1: Revalidate the ASIO ordinary-close seam

**Files:**

- Create: `.codex-tmp/loader-cleanup-asio-close-profile.py` (untracked)
- Read: `H:\gc\game471.exe.i64`
- Read: `src/Audio/AudioPatch.cpp`
- Read: `src/Audio/AudioContractFatal.*`

- [ ] **Step 1: Recheck the site and instruction boundary in IDA**

At RVA `0x0023C853`, require the exact 16-byte contract:

```text
FF 15 3C D6 6A 00 8B E5 5D C3 CC CC CC 55 8B EC
```

Record the containing function, the indirect call's semantic role, the
instruction span SafetyHook must relocate, and all predecessors that establish
this as ordinary ASIO shutdown rather than process detach or an error-only
path.

- [ ] **Step 2: Recheck callback ownership and ABI**

Verify that a SafetyHook `MidHookFn` (`void(safetyhook::Context&)`) at this site
may release the published non-DirectSound audio owner before execution
continues. Preserve the current exactly-once exchange, null-owner fatal
contract, deletion timing, and no-throw boundary.

- [ ] **Step 3: Record the evidence**

Add the function/address/bytes/ABI/control-flow facts to
`docs/architecture/loader-cleanup-baseline.md`. Do not record a hash of the IDA
helper script.

---

## Task 2: Add the feature-owned ASIO-close profile and callback

**Files:**

- Create: `src/Audio/Asio/AsioCloseProfile.h`
- Create: `src/Audio/Asio/AsioCloseProfile.cpp`
- Create: `src/Audio/Asio/AsioCloseHook.h`
- Create: `src/Audio/Asio/AsioCloseHook.cpp`
- Create: `src/Audio/AudioRuntimeState.h`
- Create: `src/Audio/AudioRuntimeState.cpp`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::audio::asio {

struct AsioCloseProfile final {
    game_version::GameBuild build;
    game_version::GameImageVariant variant;
    std::array<game_version::VersionedOperation, 1> operations;
};

[[nodiscard]] std::optional<AsioCloseProfile>
ProfileFor(game_version::GameBuild,
           game_version::GameImageVariant) noexcept;

void OnOrdinaryAsioClose(safetyhook::Context&) noexcept;

} // namespace gc::audio::asio

namespace gc::audio {

[[nodiscard]] std::expected<void, AudioRuntimeError>
PrepareAndPublishAudioRuntime(AudioSettings settings) noexcept;

void ReleaseAudioRuntimeAtOrdinaryAsioClose() noexcept;

} // namespace gc::audio
```

- [ ] **Step 1: Move the fixed native facts into the profile**

The 4.71 clean and legacy-patched variants each select a profile containing
RVA `0x0023C853`, the verified 16-byte original contract, mid-hook kind,
protected instruction span, callback, and install order. The hook does not
alter bytes permanently, so both known variants use the same original site
contract.

- [ ] **Step 2: Move audio-owner lifetime out of the monolith**

Move `ProductionDetourState`, its atomic publication, construction, access,
and exactly-once destruction into `AudioRuntimeState.*`. Keep controller,
diagnostics, WASAPI, and ASIO factory ownership unchanged. Do not add a generic
service locator or expose mutable state to unrelated modules.

- [ ] **Step 3: Keep the callback beside ASIO**

`OnOrdinaryAsioClose` calls
`ReleaseAudioRuntimeAtOrdinaryAsioClose`. A missing owner remains an
`AudioContractFatalReason::AsioOwnershipFailure` with the failing RVA. The
callback catches no recoverable condition and lets no exception escape.

- [ ] **Step 4: Delete local hook mechanics**

Remove `g_asio_close_hook`, `kSupportedImageBase`, `kAsioCloseRva`,
`kAsioCloseBytes`, `ReadExecutableBytes`, `CreateAsioOrdinaryCloseHook`, and
the pending/local SafetyHook flow from `AudioPatch.cpp`.

Compile the profile/callback sources in the game-facing `gc_audio` target.
Do not make the reusable lower-level `gc_asio` target depend on GameVersion,
RuntimeImage, or the loader startup graph merely because the files live beside
ASIO code.

---

## Task 3: Compose ASIO close into the game barrier

**Files:**

- Modify: `src/Loader/GameVersionedStartupPlan.cpp`
- Modify: `src/Loader/VersionedStartupExecutor.cpp`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Loader/DllMain.cpp`

- [ ] **Step 1: Contribute the site only for ASIO**

When the validated audio selection is ASIO, require an ASIO-close profile and
append its one site/hook operation to `GameVersionedStartupPlan`. For
DirectSound or WASAPI, append nothing. A missing enabled profile is an
unsupported-build fatal error.

- [ ] **Step 2: Publish runtime state before enabling the callback**

Construct and publish the non-DirectSound audio owner before the registry
enables the ASIO-close hook. The hook is created disabled, its binding and
original metadata are published, and only then is it enabled in declared
order.

- [ ] **Step 3: Keep the DirectSound export detour outside the barrier**

The DirectSound `DirectSoundCreate8` export registration remains a
non-versioned HookPlan from Plan 04. The game startup composition validates
both plans before mutation, installs the versioned ASIO hook when applicable,
then installs the export detour. Any install failure invokes the common
terminal fatal reporter and aborts; do not destroy the published owner or
disable an already installed hook.

---

## Task 4: Revalidate the NESYS ping redirect seam

**Files:**

- Create: `.codex-tmp/loader-cleanup-nesys-ping-profile.py` (untracked)
- Read: `H:\gc\NesysService.exe.i64`
- Read: `H:\gc\NesysService.exe`
- Read: `src/Nesys/Network/SyntheticNetworkAdapter.*`
- Read: `src/Nesys/NesysServiceProcess.*`

- [ ] **Step 1: Recheck the exact site**

At NESYS RVA `0x00008E40`, require the exact 32-byte contract:

```text
51 53 55 56 57 50 8B D9 8D 6B 04 6A 10 55 C7 44
24 1C 00 00 00 00 E8 02 73 02 00 83 C4 0C 8D 73
```

Verify the containing ping/address path, hook instruction boundary, saved EAX
slot used by `ApplyServicePingTarget`, and the replacement target
`127.0.0.1`.

- [ ] **Step 2: Confirm process-role gating from current code**

Record that `ResolveNesysFeaturePlan` sets `service_ping_redirect` only when
`role == ProcessRole::Service` and network virtualization is enabled. Preserve
that exact condition; registry-only virtualization and the game role do not
need NESYS build detection for this site.

- [ ] **Step 3: Name the build from evidence**

Use file-version/native evidence when available to replace the neutral
`NesysBuild::current_supported` name introduced in Plan 03. Do not borrow the
game's 4.71 version label. Retain the exact known identity:

```text
size:   368640
sha256: 487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2
```

---

## Task 5: Add the NESYS ping profile

**Files:**

- Create: `src/Nesys/Network/NesysPingProfile.h`
- Create: `src/Nesys/Network/NesysPingProfile.cpp`
- Modify: `src/Nesys/Network/SyntheticNetworkAdapter.h`
- Modify: `src/Nesys/Network/SyntheticNetworkAdapter.cpp`
- Modify: `src/Nesys/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::nesys_service {

struct NesysPingProfile final {
    NesysBuild build;
    NesysImageVariant variant;
    std::array<game_version::VersionedOperation, 1> operations;
};

[[nodiscard]] std::optional<NesysPingProfile>
PingProfileFor(NesysBuild, NesysImageVariant) noexcept;

void OnServicePingAddress(safetyhook::Context&) noexcept;

} // namespace gc::nesys_service
```

- [ ] **Step 1: Move all native facts into the profile**

Move `kServicePingRva`, `kServicePingSignature`, hook kind/span, callback, and
install order to the selected NESYS profile. Keep the semantic loopback value
and synthetic-adapter policy beside `SyntheticNetworkAdapter`.

- [ ] **Step 2: Preserve the callback's exact register behavior**

The profile callback adapts the SafetyHook context to the existing
`ApplyServicePingTarget` contract. Preserve which saved register/stack value is
replaced, all untouched state, and the no-throw boundary.

- [ ] **Step 3: Delete the local lifecycle**

Remove `ValidateServicePingSignature`, `PreflightServicePingRedirect`,
`PrepareServicePingRedirect`, `ActivateServicePingRedirect`,
`RollbackServicePingRedirect`, the local `MidHook`, direct byte reader, and
all prepare/activate/rollback state. HookRegistry owns the installed hook for
the NESYS process lifetime.

---

## Task 6: Compose the NESYS barrier and export-hook plan

**Files:**

- Modify: `src/Loader/NesysVersionedStartupPlan.cpp`
- Modify: `src/Nesys/NesysServicePatch.cpp`
- Modify: `src/Loader/DllMain.cpp`

- [ ] **Step 1: Detect NESYS only when a fixed site is enabled**

If `NesysFeaturePlan::service_ping_redirect` is false, build no NESYS
versioned plan. If true, detect the loaded NESYS image and require a complete
ping profile. An exact known hash selects it directly; an unknown structural
candidate must match the complete original ping contract before approval.

- [ ] **Step 2: Keep NESYS export hooks non-versioned**

Build and collision-validate the synthetic-adapter, resolver, registry,
request-diagnostic, launcher, and process hooks through the central HookPlan.
Do not include API/export targets in the NESYS byte-contract barrier and do not
put the ping RVA into the export-hook plan.

- [ ] **Step 3: Install in terminal-failure order**

Resolve configuration and initialize immutable feature state; validate the
complete NESYS versioned and export plans; create/publish hook bindings; install
the ping hook; then install export hooks in existing declared order. Every
failure logs the typed cause, shows one popup, and aborts. Remove
the residual service-ping prepare/activate failure coupling that survived Plan
04 and the `return false` failure path; `NesysHookTransaction` itself is
already gone.

---

## Task 7: Prove all fixed-RVA direct hooks have entered a barrier

**Files:**

- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Run the fixed-site ownership audit**

```powershell
rg -n 'create_(inline|mid)|SafetyHookFactory|safetyhook::(InlineHook|MidHook)|GetModuleHandleW\(nullptr\).*\+|k[A-Za-z]+Rva' src
rg -n 'PreflightServicePingRedirect|PrepareServicePingRedirect|ActivateServicePingRedirect|RollbackServicePingRedirect|g_asio_close_hook|CreateAsioOrdinaryCloseHook' src
```

Classify every result. Direct SafetyHook construction must exist only in
`gc_hooking`; concrete executable RVAs must exist only in feature profiles or
documented non-hook read-only native contracts. The second command must return
no production matches.

- [ ] **Step 2: Update the manifest totals**

Append the final game/NESYS versioned site totals and process gating to the
baseline. State separately that API/export hooks have address collision
validation but are outside byte-pattern preflight.

- [ ] **Step 3: Run static verification**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Do not claim ASIO shutdown, audio quality, NESYS ping, child-process, network,
or request-pipeline runtime acceptance.

- [ ] **Step 4: Commit**

```powershell
git add -- src\Audio src\Nesys src\Loader\GameVersionedStartupPlan.cpp src\Loader\NesysVersionedStartupPlan.cpp src\Loader\VersionedStartupExecutor.cpp src\Loader\DllMain.cpp docs\architecture\loader-cleanup-baseline.md
git commit -m "Migrate audio and NESYS versioned hooks"
```
