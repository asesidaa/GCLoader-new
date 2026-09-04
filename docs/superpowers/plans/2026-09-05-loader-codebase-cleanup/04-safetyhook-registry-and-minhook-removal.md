# SafetyHook Registry and MinHook Removal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make SafetyHook v0.7.0 the sole third-party hook implementation, centralize process-lifetime inline/mid ownership and collision validation, migrate every exported-function hook, and remove MinHook completely.

**Architecture:** `gc_hooking` resolves exports to process addresses, validates a complete non-versioned hook plan, creates hooks disabled when callback state must be published, retains originals and hook objects for process lifetime, then enables each hook. It keys exclusivity by resolved address so forwarded exports and aliases cannot receive incompatible detours.

**Tech Stack:** C++23, Win32 x86 exports, SafetyHook v0.7.0 `InlineHook`/`MidHook`, `std::expected`, `std::variant`, CMake FetchContent.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 03 first.
- Export resolution/collision validation is not game-version preflight. Keep
  both gates explicit in diagnostics and startup flow.
- Exactly one physical hook may own a resolved address. Multiple consumers are
  legal only behind a named shared dispatcher added in Plan 05.
- Any hook included in the startup plan is required. Creation or enable failure
  logs the exact SafetyHook error, shows one popup, and aborts. In particular,
  the crash-filter hook no longer silently degrades after an install failure.
- `StartDisabled` is local ordering for publishing an original trampoline or
  callback state; it is not a global prepare/commit/rollback protocol.
- Do not add a generic hook backend, MinHook compatibility wrapper, injected
  hook function table, or fake hook tests.
- Successful hook ownership intentionally survives until process termination.
  Do not add live DLL unload support.

---

## Task 1: Replace `MinHookTransaction` with concrete hook-plan types

**Files:**

- Create: `src/Platform/Win32/Hooking/HookIdentity.h`
- Create: `src/Platform/Win32/Hooking/HookError.h`
- Create: `src/Platform/Win32/Hooking/HookPlan.h`
- Create: `src/Platform/Win32/Hooking/HookPlan.cpp`
- Delete after migration: `src/Platform/Win32/Hooking/MinHookTransaction.h`
- Delete after migration: `src/Platform/Win32/Hooking/MinHookTransaction.cpp`
- Modify: `src/Platform/Win32/Hooking/CMakeLists.txt`

**Interfaces:**

```cpp
namespace gc::hooking {

struct HookIdentity final {
    std::string_view feature;
    std::string_view site;
};

struct ExportTarget final {
    std::wstring_view module;
    std::string_view name;
};

struct ResolvedHookTarget final {
    HookIdentity identity;
    std::uintptr_t address{};
    std::optional<ExportTarget> export_target;
};

enum class HookKind : std::uint8_t { inline_detour, mid_detour };
enum class HookSharing : std::uint8_t { exclusive, named_dispatcher };
enum class HookStage : std::uint8_t {
    invalid_plan,
    resolve_module,
    resolve_export,
    collision,
    create,
    publish_original,
    enable,
};

struct HookError final {
    HookStage stage{};
    HookIdentity identity{};
    std::uintptr_t address{};
    std::optional<ExportTarget> export_target;
    HookIdentity collision_peer{};
    DWORD win32_error{};
    std::uint32_t safetyhook_error{};
};

class HookPlan final {
public:
    template <typename Function>
    [[nodiscard]] std::expected<void, HookError> AddInlineExport(
        HookIdentity,
        ExportTarget,
        Function detour,
        Function* original_storage,
        HookSharing = HookSharing::exclusive) noexcept;

    [[nodiscard]] std::expected<void, HookError> AddMidAddress(
        HookIdentity,
        std::uintptr_t address,
        safetyhook::MidHookFn callback,
        HookSharing = HookSharing::exclusive) noexcept;

    [[nodiscard]] std::expected<ValidatedHookPlan, HookError>
    ResolveAndValidate() const noexcept;
};

} // namespace gc::hooking
```

- [ ] **Step 1: Resolve every export before installing any hook**

Use `GetModuleHandleW` and `GetProcAddress`; capture `GetLastError`
immediately. Store the final function address returned by the loader. Do not
key collisions by module/name text because forwarded exports may alias.

- [ ] **Step 2: Validate the complete plan**

Reject null detours/callbacks, missing original storage for inline hooks that
call through, duplicate identities, duplicate exclusive addresses, and any
shared registration whose dispatcher name differs. Preserve request order as
the explicit install order after validation.

- [ ] **Step 3: Compile before migrating callers**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_hooking
```

Expected: `gc_hooking` links SafetyHook and no longer compiles the MinHook
transaction once its callers have moved in the next tasks.

---

## Task 2: Add process-lifetime SafetyHook ownership

**Files:**

- Create: `src/Platform/Win32/Hooking/HookRegistry.h`
- Create: `src/Platform/Win32/Hooking/HookRegistry.cpp`
- Create: `src/Platform/Win32/Hooking/HookDiagnostics.h`
- Create: `src/Platform/Win32/Hooking/HookDiagnostics.cpp`
- Modify: `src/Platform/Win32/Hooking/CMakeLists.txt`

**Interfaces:**

```cpp
class HookRegistry final {
public:
    [[nodiscard]] static HookRegistry& ProcessLifetime() noexcept;
    [[nodiscard]] std::expected<void, HookError>
    Install(const ValidatedHookPlan&) noexcept;
};
```

- [ ] **Step 1: Store stable hook records**

Back the registry with stable-address process-lifetime records, such as
`std::deque<std::variant<safetyhook::InlineHook, safetyhook::MidHook>>` plus
resolved identity metadata. Allocate the singleton with `new` and never
destroy it during DLL detach.

- [ ] **Step 2: Implement safe inline publication ordering**

For each inline request:

1. call `safetyhook::InlineHook::create(..., StartDisabled)`;
2. retain the returned hook in stable registry storage;
3. publish `hook.original<Function>()` into the typed caller-owned slot;
4. enable the retained hook and require success.

For a mid hook whose callback state must be visible, require the feature to
publish that process-lifetime state before `Install` reaches the request.
Never enable first and publish the original afterward.

- [ ] **Step 3: Translate exact SafetyHook errors**

Map `InlineHook::Error::Type` and `MidHook::Error::Type` to the numeric field
without losing stage, feature, site, address, or export text. Formatting is
separate and includes both the symbolic stage and numeric library error.

- [ ] **Step 4: Make failure terminal without reverse rollback**

If request N fails after requests 0 through N-1 enabled, immediately call the
startup abort publisher. Do not reset earlier hooks, disable all hooks, or
attempt to resume startup. Local destruction of a never-enabled failed
candidate is allowed; it is not reverse rollback.

---

## Task 3: Migrate locale, crash-filter, Ttx, and Raw Input exports

**Files:**

- Modify: `src/Locale/JapaneseLocaleCompatibility.h`
- Modify: `src/Locale/JapaneseLocaleCompatibility.cpp`
- Modify: `src/Locale/CMakeLists.txt`
- Modify: `src/Diagnostics/CrashDumpHandler.h`
- Modify: `src/Diagnostics/CrashDumpHandler.cpp`
- Modify: `src/Diagnostics/CMakeLists.txt`
- Modify: `src/SystemPath/TtxInitGuard.h`
- Modify: `src/SystemPath/TtxInitGuard.cpp`
- Modify: `src/SystemPath/CMakeLists.txt`
- Modify: `src/Input/Win32/RawInputRegistrationGuard.h`
- Modify: `src/Input/Win32/RawInputRegistrationGuard.cpp`
- Modify: `src/Input/CMakeLists.txt`

- [ ] **Step 1: Convert each installer into a plan contributor**

Expose explicit functions named for the feature, for example:

```cpp
std::expected<void, hooking::HookError>
AddJapaneseLocaleHooks(hooking::HookPlan&, ProcessRole) noexcept;
std::expected<void, hooking::HookError>
AddCrashDumpHook(hooking::HookPlan&) noexcept;
std::expected<void, hooking::HookError>
AddTtxInitHook(hooking::HookPlan&, TtxInitGuard&) noexcept;
std::expected<void, hooking::HookError>
AddRawInputRegistrationHook(hooking::HookPlan&) noexcept;
```

Keep feature policy and callback state in the feature. The contributors only
describe concrete targets, detours, and original slots.

- [ ] **Step 2: Remove local hook lifecycle abstractions**

Delete Japanese locale's `MinHookTransaction`, crash dump's owned transaction,
`TtxGuardInstallActions`, Ttx's `create_disabled`/`enable`/`reset` lambdas, and
Raw Input's local SafetyHook owner/mutex. Their hooks become registry-owned.

- [ ] **Step 3: Preserve callback contracts**

Keep locale code-page policy, crash dump generation, Ttx failure publication,
Raw Input protected HID usages, calling conventions, exception containment,
and `LastError` behavior byte-for-byte at the callback boundary.

---

## Task 4: Migrate Kernel32/RFID and NESYS export hooks

**Files:**

- Modify: `src/Win32Hooks/Kernel32Hooks.h`
- Modify: `src/Win32Hooks/Kernel32Hooks.cpp`
- Modify: `src/Win32Hooks/CMakeLists.txt`
- Modify: `src/Rfid/Feature.h`
- Modify: `src/Rfid/Feature.cpp`
- Modify: `src/Nesys/NesysServicePatch.h`
- Modify: `src/Nesys/NesysServicePatch.cpp`
- Modify: `src/Nesys/Diagnostics/RequestPipelineDiagnostics.*`
- Delete: `src/Nesys/NesysHookTransaction.h`
- Delete: `src/Nesys/NesysHookTransaction.cpp`
- Modify: `src/Nesys/CMakeLists.txt`

- [ ] **Step 1: Move the existing Kernel32 request set into `HookPlan`**

Preserve the current conditional export set exactly. Keep one Kernel32 detour
per export during this plan; Plan 05 changes only internal dispatch ownership.

- [ ] **Step 2: Replace NESYS's second transaction implementation**

Convert every `ApiHookRequest` created by NESYS process patching and request
diagnostics into `HookPlan` registrations. Preserve role-dependent export
selection and process-lifetime original slots. Delete `OwnedMinHookTransaction`,
its initialize/create/commit/rollback states, and all `MH_STATUS` reporting.

- [ ] **Step 3: Keep NESYS fixed-RVA ping separate**

Do not migrate `kServicePingRva` in this task. It is a versioned mid hook and
belongs to Plan 06h. Its direct SafetyHook owner is an explicit temporary
migration item in the baseline ledger.

---

## Task 5: Migrate DirectSound export hooking out of `AudioPatch`

**Files:**

- Create: `src/Audio/DirectSound/DirectSoundHook.h`
- Create: `src/Audio/DirectSound/DirectSoundHook.cpp`
- Modify: `src/Audio/AudioPatch.h`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/AudioPatchInternal.h`
- Modify: `src/Audio/CMakeLists.txt`

- [ ] **Step 1: Extract the concrete export contribution**

Move `DirectSoundCreate8Detour`, its original function slot, and the concrete
plan contribution for `dsound.dll!DirectSoundCreate8` beside
`DirectSoundFacade`. Leave audio backend/controller policy in Audio.

- [ ] **Step 2: Delete Audio's embedded MinHook transaction**

Remove `AudioMinHookApi`, all `MH_Initialize`/create/enable/disable/remove calls,
`RollbackResult`, transaction stages named for MinHook, and MinHook includes.
Hook success is the registry's `Install` result.

- [ ] **Step 3: Keep ASIO close out of this export plan**

The ordinary-close site at game RVA `0x0023C853` remains a versioned mid hook
until Plan 06h. Do not confuse it with `DirectSoundCreate8` export resolution.

---

## Task 6: Compose and install the complete non-versioned hook plan

**Files:**

- Create: `src/Loader/NonVersionedHookPlan.h`
- Create: `src/Loader/NonVersionedHookPlan.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Build plans by process role**

Game composition explicitly adds locale, crash-filter, Raw Input, Ttx,
Kernel32/RFID, game-side NESYS, and DirectSound registrations that are enabled
by the validated configuration. NESYS composition adds locale and NESYS-
process registrations only. Preserve the current state-construction order
needed by callbacks.

- [ ] **Step 2: Validate before the first exported-function hook**

Call `ResolveAndValidate()` once after all role-specific contributors have
been added. On failure call `AbortProcess`. Only then call the process registry
once to install in declared order.

- [ ] **Step 3: Remove per-feature install calls from DllMain**

DllMain should not invoke separate hook transactions. It may still invoke
legacy fixed-RVA patch initializers until Plans 06a through 06h and will be
fully thinned in Plan 09.

---

## Task 7: Remove MinHook from the repository

**Files:**

- Modify: `cmake/Dependencies.cmake`
- Modify: root `CMakeLists.txt`
- Modify: every nested `CMakeLists.txt` that names MinHook
- Modify: `src/CMakeLists.txt`
- Delete: all remaining project-owned MinHook source/header files

- [ ] **Step 1: Remove dependency acquisition and corresponding-source metadata**

Delete the MinHook FetchContent declaration, `minhook` from
`GC_CORRESPONDING_SOURCE_DEPENDENCIES`, its origin/revision variables, include
directories, link targets, and package audit expectations.

- [ ] **Step 2: Audit all text references**

Run:

```powershell
rg -n -i 'minhook|MH_[A-Za-z0-9_]+' CMakeLists.txt cmake src tests tools docs\architecture\loader-cleanup-baseline.md
```

Expected: only the historical `before` rows in the baseline Markdown remain.
No CMake, production, test, or tool source reference remains.

- [ ] **Step 3: Audit direct SafetyHook construction**

```powershell
rg -n 'InlineHook::create|MidHook::create|create_inline|create_mid' src
```

Expected at this checkpoint: export-hook construction appears only in
`Platform/Win32/Hooking`; listed fixed-RVA feature hooks remain temporary and
are named in Plans 06a through 06h.

---

## Task 8: Verify and commit unified hooking

- [ ] **Step 1: Run complete static verification**

```powershell
cmake --fresh --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --fresh --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

Fresh configure is required because a FetchContent dependency was removed.
Do not claim any detour executed in a target process.

- [ ] **Step 2: Commit**

```powershell
git add -- CMakeLists.txt cmake src
git commit -m "Unify hooks on SafetyHook"
```
