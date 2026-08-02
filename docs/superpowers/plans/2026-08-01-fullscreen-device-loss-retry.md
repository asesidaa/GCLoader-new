# Fullscreen Alt+Tab Device-Loss Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the renderer's lost-device lifecycle and contain every identified buffer creation, Lock, and Unlock failure that can terminate `game471.exe` while returning to fullscreen after Alt+Tab.

**Architecture:** Extend the existing `RendererDeviceLoss` module from checkpoint commit `d732550`. Add one OnLost resource-lifecycle hook, one index-buffer creation hook, and two HRESULT redirect hooks; keep the existing vertex-buffer creation retry and empty-vector guard. All ten binary contracts preflight before six SafetyHook mid-hooks are published as one rollback-safe transaction.

**Tech Stack:** C++23, Win32 x86/MSVC, Direct3D 9 COM semantics, SafetyHook 0.7, CMake/Ninja, standalone CTest executable, daemon-backed IDA-CLI.

## Global Constraints

- Support only the current `game471.exe.i64` mapping at preferred image base `0x00400000`.
- Scope recovery to `fullscreen -> Alt+Tab away -> Alt+Tab back`; generic allocation failure, corrupt assets, and arbitrary renderer corruption remain out of scope.
- Assume `CheckDeviceLost=1`; do not read, rewrite, or re-encode the Shift-JIS `system.cfg`.
- Do not add `config.toml` or ConfigGUI settings.
- Preserve the two-hook baseline committed in `d732550`: vertex-buffer creation retry at RVA `0x000E79F7` and empty-vector draw guard at RVA `0x000E5578`.
- Keep successful Direct3D calls and nonempty renderer state on native execution paths without register or renderer-memory changes.
- Keep all hook callbacks `noexcept`, guarded against C++ and structured exceptions, allocation-free, and free of per-call logging.
- Preflight every hook site and redirect target before creating the first hook; reset all candidate hooks after any creation failure and publish ownership only after all six succeed.
- Touch only `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`, `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`, and `tests/Patches/RendererDeviceLossPatchTests.cpp` during implementation.
- Preserve the unrelated working-tree edit in `src/Rfid/Feature.cpp` without staging or modifying it.
- Do not deploy or mutate files under `H:\gc`; runtime acceptance remains user-run after static verification.

---

## File Structure

- Modify `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`: add the six remaining RVAs/patterns, the OnLost action seam, two HRESULT redirect transforms, and the expanded contract-site enum.
- Modify `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`: add guarded index-buffer detach/release adapters, four production callbacks, six-hook ownership/reset, and ten-contract transactional installation.
- Modify `tests/Patches/RendererDeviceLossPatchTests.cpp`: add lifecycle ordering tests, negative-HRESULT redirect tests, and complete ten-contract/six-hook transaction coverage.
- Do not modify CMake or loader integration: the existing test target, runtime-patch library membership, and game-process initialization already cover this module.

### Task 1: Complete OnLost Resource Teardown

**Files:**
- Modify: `tests/Patches/RendererDeviceLossPatchTests.cpp:48-490`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h:13-138`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp:18-370`

**Interfaces:**
- Consumes: `safetyhook::Context::esi` at OnLost tail RVA `0x000E67D8`, renderer initialized offset `0x484`, and index-buffer-holder offset `0x778`.
- Produces: `RendererDeviceLostActions`, `ApplyRendererDeviceLostCleanup(safetyhook::Context&, RendererDeviceLostActions) noexcept`, `RendererContractSite::DeviceLostTail`, and a third transactional hook.

- [ ] **Step 1: Add failing lifecycle and installer tests**

Add this production-facing action interface usage to the test, backed by a recorder that proves call order as well as values:

```cpp
struct LostResourceState {
    std::uintptr_t renderer{};
    std::size_t initialized_offset{};
    std::size_t holder_offset{};
    std::uintptr_t detached_buffer{};
    int phase{};
    int clear_calls{};
    int detach_calls{};
    int release_calls{};
    bool clear_succeeds{true};
    bool detach_succeeds{true};
    bool release_succeeds{true};
    bool release_saw_detached_state{};
};

bool ClearLostInitialized(
    void* opaque,
    std::uintptr_t renderer,
    std::size_t offset) noexcept {
    auto& state = *static_cast<LostResourceState*>(opaque);
    state.renderer = renderer;
    state.initialized_offset = offset;
    state.clear_calls++;
    state.phase = 1;
    return state.clear_succeeds;
}

bool DetachLostIndexBuffer(
    void* opaque,
    std::uintptr_t renderer,
    std::size_t offset,
    std::uintptr_t& detached) noexcept {
    auto& state = *static_cast<LostResourceState*>(opaque);
    state.renderer = renderer;
    state.holder_offset = offset;
    state.detach_calls++;
    if (!state.detach_succeeds || state.phase != 1) {
        return false;
    }
    state.phase = 2;
    detached = state.detached_buffer;
    return true;
}

bool ReleaseLostIndexBuffer(
    void* opaque,
    std::uintptr_t buffer) noexcept {
    auto& state = *static_cast<LostResourceState*>(opaque);
    state.release_calls++;
    state.release_saw_detached_state =
        state.phase == 2 && buffer == state.detached_buffer;
    state.phase = 3;
    return state.release_succeeds;
}
```

Add these cases in `main()`:

```cpp
auto lost = CanaryContext();
lost.esi = 0x15B61190U;
const auto lost_before = lost;
LostResourceState resources{.detached_buffer = 0x12345678U};
failures += Expect(
    ApplyRendererDeviceLostCleanup(
        lost,
        {
            .context = &resources,
            .clear_initialized = ClearLostInitialized,
            .detach_index_buffer = DetachLostIndexBuffer,
            .release_index_buffer = ReleaseLostIndexBuffer,
        }) &&
        resources.clear_calls == 1 && resources.detach_calls == 1 &&
        resources.release_calls == 1 &&
        resources.renderer == lost.esi &&
        resources.initialized_offset == kRendererInitializedOffset &&
        resources.holder_offset == kRendererIndexBufferHolderOffset &&
        resources.release_saw_detached_state &&
        ContextEquals(lost, lost_before),
    "OnLost detaches before releasing the default-pool index buffer");

auto already_empty = lost_before;
LostResourceState no_buffer{};
failures += Expect(
    ApplyRendererDeviceLostCleanup(
        already_empty,
        {
            .context = &no_buffer,
            .clear_initialized = ClearLostInitialized,
            .detach_index_buffer = DetachLostIndexBuffer,
            .release_index_buffer = ReleaseLostIndexBuffer,
        }) &&
        no_buffer.clear_calls == 1 && no_buffer.detach_calls == 1 &&
        no_buffer.release_calls == 0 &&
        ContextEquals(already_empty, lost_before),
    "OnLost accepts an already-null index buffer");

auto clear_rejected = lost_before;
LostResourceState failed_clear{
    .detached_buffer = 0x12345678U,
    .clear_succeeds = false,
};
failures += Expect(
    !ApplyRendererDeviceLostCleanup(
        clear_rejected,
        {
            .context = &failed_clear,
            .clear_initialized = ClearLostInitialized,
            .detach_index_buffer = DetachLostIndexBuffer,
            .release_index_buffer = ReleaseLostIndexBuffer,
        }) &&
        failed_clear.clear_calls == 1 && failed_clear.detach_calls == 0 &&
        failed_clear.release_calls == 0 &&
        ContextEquals(clear_rejected, lost_before),
    "failed initialized-state write prevents unsafe teardown");

auto detach_rejected = lost_before;
LostResourceState failed_detach{
    .detached_buffer = 0x12345678U,
    .detach_succeeds = false,
};
failures += Expect(
    !ApplyRendererDeviceLostCleanup(
        detach_rejected,
        {
            .context = &failed_detach,
            .clear_initialized = ClearLostInitialized,
            .detach_index_buffer = DetachLostIndexBuffer,
            .release_index_buffer = ReleaseLostIndexBuffer,
        }) &&
        failed_detach.clear_calls == 1 && failed_detach.detach_calls == 1 &&
        failed_detach.release_calls == 0 &&
        ContextEquals(detach_rejected, lost_before),
    "failed holder access never releases an unknown pointer");

auto release_rejected = lost_before;
LostResourceState failed_release{
    .detached_buffer = 0x12345678U,
    .release_succeeds = false,
};
failures += Expect(
    !ApplyRendererDeviceLostCleanup(
        release_rejected,
        {
            .context = &failed_release,
            .clear_initialized = ClearLostInitialized,
            .detach_index_buffer = DetachLostIndexBuffer,
            .release_index_buffer = ReleaseLostIndexBuffer,
        }) &&
        failed_release.clear_calls == 1 &&
        failed_release.detach_calls == 1 &&
        failed_release.release_calls == 1 &&
        failed_release.release_saw_detached_state &&
        ContextEquals(release_rejected, lost_before),
    "release failure cannot restore a detached index-buffer pointer");

auto null_renderer = lost_before;
null_renderer.esi = 0;
const auto null_renderer_before = null_renderer;
LostResourceState untouched_resources{};
failures += Expect(
    !ApplyRendererDeviceLostCleanup(
        null_renderer,
        {
            .context = &untouched_resources,
            .clear_initialized = ClearLostInitialized,
            .detach_index_buffer = DetachLostIndexBuffer,
            .release_index_buffer = ReleaseLostIndexBuffer,
        }) &&
        untouched_resources.clear_calls == 0 &&
        untouched_resources.detach_calls == 0 &&
        untouched_resources.release_calls == 0 &&
        ContextEquals(null_renderer, null_renderer_before),
    "null renderer state is rejected without side effects");
```

Add `DeviceLostTail` to the fake contract and hook models. The valid installer case must now assert five reads and these three hooks in order:

```cpp
constexpr std::array expected_hooks{
    RendererContractSite::DeviceLostTail,
    RendererContractSite::VertexBufferResult,
    RendererContractSite::VertexBufferLockGuard,
};
```

Add independent read-failure, one-byte-mismatch, and hook-failure cases for `DeviceLostTail`; every rejection must leave `reset_calls` at zero before hook creation or exactly one after a hook-creation failure.

- [ ] **Step 2: Run the focused build and witness RED**

Run:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests"
```

Expected: compilation fails because `RendererDeviceLostActions`, `ApplyRendererDeviceLostCleanup`, `kRendererIndexBufferHolderOffset`, and `RendererContractSite::DeviceLostTail` are not defined. An environment or CMake failure is not an acceptable RED.

- [ ] **Step 3: Implement the pure lifecycle transform**

Add these declarations to the header:

```cpp
inline constexpr std::uint32_t kDeviceLostTailRva = 0x000E67D8U;
inline constexpr std::size_t kRendererIndexBufferHolderOffset = 0x778U;

inline constexpr std::array<std::byte, 12> kDeviceLostTailPattern{
    std::byte{0x89}, std::byte{0xBE}, std::byte{0x18}, std::byte{0x01},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x89}, std::byte{0xBE},
    std::byte{0x1C}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
};

struct RendererDeviceLostActions {
    void* context{};
    bool (*clear_initialized)(
        void*, std::uintptr_t, std::size_t) noexcept{};
    bool (*detach_index_buffer)(
        void*,
        std::uintptr_t,
        std::size_t,
        std::uintptr_t&) noexcept{};
    bool (*release_index_buffer)(
        void*, std::uintptr_t) noexcept{};
};

[[nodiscard]] bool ApplyRendererDeviceLostCleanup(
    safetyhook::Context& context,
    RendererDeviceLostActions actions) noexcept;
```

Implement the exact ordered transform in the source:

```cpp
bool ApplyRendererDeviceLostCleanup(
    safetyhook::Context& context,
    RendererDeviceLostActions actions) noexcept {
    if (context.esi == 0 || actions.clear_initialized == nullptr ||
        actions.detach_index_buffer == nullptr ||
        actions.release_index_buffer == nullptr) {
        return false;
    }
    if (!actions.clear_initialized(
            actions.context,
            context.esi,
            kRendererInitializedOffset)) {
        return false;
    }

    std::uintptr_t detached = 0;
    if (!actions.detach_index_buffer(
            actions.context,
            context.esi,
            kRendererIndexBufferHolderOffset,
            detached)) {
        return false;
    }
    return detached == 0 ||
           actions.release_index_buffer(actions.context, detached);
}
```

- [ ] **Step 4: Add guarded production teardown and the third hook**

Include `<Unknwn.h>` for `IUnknown`, add `device_lost_tail_hook` to
`RendererDeviceLossRuntime`, and implement leaf adapters with
`__try/__except`:

```cpp
bool ProductionDetachIndexBuffer(
    void*,
    std::uintptr_t renderer,
    std::size_t offset,
    std::uintptr_t& detached) noexcept {
    if (renderer == 0 || offset != kRendererIndexBufferHolderOffset ||
        renderer > std::numeric_limits<std::uintptr_t>::max() - offset) {
        return false;
    }
    __try {
        const auto holder =
            *reinterpret_cast<std::uintptr_t*>(renderer + offset);
        if (holder == 0) {
            return false;
        }
        auto* const slot = reinterpret_cast<std::uintptr_t*>(holder);
        detached = *slot;
        *slot = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        detached = 0;
        return false;
    }
}

bool ProductionReleaseIndexBuffer(
    void*, std::uintptr_t buffer) noexcept {
    if (buffer == 0) {
        return true;
    }
    __try {
        reinterpret_cast<IUnknown*>(buffer)->Release();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void OnDeviceLostTail(safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLostCleanup(
            context,
            {
                .clear_initialized = ProductionClearInitialized,
                .detach_index_buffer = ProductionDetachIndexBuffer,
                .release_index_buffer = ProductionReleaseIndexBuffer,
            }));
    } catch (...) {
    }
}
```

Extend `RendererContractSite`, preflight `kDeviceLostTailPattern`, route `DeviceLostTail` to `OnDeviceLostTail` in `ProductionInstallHook`, install it before the two existing hooks, and reset it after the later hooks during rollback. Global ownership must remain unpublished until all three hooks exist.

- [ ] **Step 5: Run the focused test and witness GREEN**

Run:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests iDmacDrv32 && ctest --preset msvc32-debug -R "^RendererDeviceLossPatchTests$" --output-on-failure"
```

Expected: both targets build and `RendererDeviceLossPatchTests` passes.

- [ ] **Step 6: Commit the lifecycle repair**

```powershell
git add -- src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp tests/Patches/RendererDeviceLossPatchTests.cpp
git commit -m "fix: complete renderer on-lost cleanup"
```

### Task 2: Retry Failed Index-Buffer Recreation

**Files:**
- Modify: `tests/Patches/RendererDeviceLossPatchTests.cpp`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`

**Interfaces:**
- Consumes: existing `ApplyRendererDeviceLossRetry`, initializer epilogue RVA `0x000E7EE9`, and index-buffer creation HRESULT in `EAX` at RVA `0x000E7A84` with renderer in `ESI`.
- Produces: `RendererContractSite::IndexBufferResult`, an index-buffer result byte contract, and a fourth transactional hook using the existing retry transform.

- [ ] **Step 1: Add failing index-result transaction tests**

Extend the fake installer so `IndexBufferResult` has its own address and exact nine-byte read. Update the successful case to require six preflight reads and this four-hook order:

```cpp
constexpr std::array expected_hooks{
    RendererContractSite::DeviceLostTail,
    RendererContractSite::VertexBufferResult,
    RendererContractSite::IndexBufferResult,
    RendererContractSite::VertexBufferLockGuard,
};
```

Add `IndexBufferResult` to the contract-site read/mismatch loop and hook-failure loop. For an `IndexBufferResult` hook failure, assert six reads, three install attempts, one reset, and no published success.

Keep the existing negative creation-HRESULT test as the independent behavioral oracle: it must still clear `ESI+0x484` and redirect only `EIP` to the initializer epilogue.

- [ ] **Step 2: Run the focused build and witness RED**

Run:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests"
```

Expected: compilation fails because `kIndexBufferResultRva`, `kIndexBufferResultPattern`, and `RendererContractSite::IndexBufferResult` do not exist.

- [ ] **Step 3: Implement the index-buffer result hook**

Add the exact binary declarations:

```cpp
inline constexpr std::uint32_t kIndexBufferResultRva = 0x000E7A84U;
inline constexpr std::array<std::byte, 9> kIndexBufferResultPattern{
    std::byte{0x85}, std::byte{0xC0}, std::byte{0x7D},
    std::byte{0x13}, std::byte{0x68}, std::byte{0xE4},
    std::byte{0xA5}, std::byte{0x71}, std::byte{0x00},
};
```

Add `index_buffer_result_hook` to the runtime owner and use a dedicated callback so production routing remains explicit:

```cpp
void OnIndexBufferCreateResult(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossRetry(
            context,
            kPreferredImageBase,
            {
                .clear_initialized = ProductionClearInitialized,
            }));
    } catch (...) {
    }
}
```

Preflight the new result site, install it after the vertex-buffer result hook, route its enum value to `OnIndexBufferCreateResult`, and include it in reverse-order reset. Negative results now skip the native thrown-integer path at `0x004E7A96`; nonnegative results execute the original `test eax,eax` and `jge` instructions.

- [ ] **Step 4: Run the focused test and witness GREEN**

Run:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests iDmacDrv32 && ctest --preset msvc32-debug -R "^RendererDeviceLossPatchTests$" --output-on-failure"
```

Expected: both targets build and the focused test passes with four committed candidate hooks.

- [ ] **Step 5: Commit the recreation retry**

```powershell
git add -- src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp tests/Patches/RendererDeviceLossPatchTests.cpp
git commit -m "fix: retry renderer index-buffer creation"
```

### Task 3: Contain Direct Lock and Buffered Unlock Failures

**Files:**
- Modify: `tests/Patches/RendererDeviceLossPatchTests.cpp`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`

**Interfaces:**
- Consumes: Direct-batch Lock HRESULT in `EAX` at RVA `0x000E691E` and buffered Unlock HRESULT in `EAX` at RVA `0x000E5662`.
- Produces: `ApplyRendererDeviceLossDirectLockSkip`, `ApplyRendererDeviceLossUnlockCompletion`, four new contract-site enum values, and the final two hooks in the six-hook transaction.

- [ ] **Step 1: Add failing pure redirect tests**

Add one negative and three nonnegative cases for each transform:

```cpp
auto direct_lock_failure = CanaryContext();
direct_lock_failure.eax = 0x88760868U;
const auto direct_before = direct_lock_failure;
failures += Expect(
    ApplyRendererDeviceLossDirectLockSkip(
        direct_lock_failure,
        kPreferredImageBase) &&
        ContextEqualsExceptEip(
            direct_lock_failure,
            direct_before,
            kPreferredImageBase + kDirectBatchCleanupRva),
    "failed direct Lock skips unchecked geometry copy");

auto unlock_failure = CanaryContext();
unlock_failure.eax = 0x88760868U;
const auto unlock_before = unlock_failure;
failures += Expect(
    ApplyRendererDeviceLossUnlockCompletion(
        unlock_failure,
        kPreferredImageBase) &&
        ContextEqualsExceptEip(
            unlock_failure,
            unlock_before,
            kPreferredImageBase + kBufferedUnlockContinuationRva),
    "failed buffered Unlock completes native batch state");

for (const std::uint32_t result : {0U, 1U, 0x7FFFFFFFU}) {
    auto direct_success = CanaryContext();
    direct_success.eax = result;
    const auto direct_success_before = direct_success;
    failures += Expect(
        !ApplyRendererDeviceLossDirectLockSkip(
            direct_success,
            kPreferredImageBase) &&
            ContextEquals(direct_success, direct_success_before),
        "nonnegative direct Lock preserves native context");

    auto unlock_success = CanaryContext();
    unlock_success.eax = result;
    const auto unlock_success_before = unlock_success;
    failures += Expect(
        !ApplyRendererDeviceLossUnlockCompletion(
            unlock_success,
            kPreferredImageBase) &&
            ContextEquals(unlock_success, unlock_success_before),
        "nonnegative buffered Unlock preserves native context");
}
```

Add wrong-image-base cases for both functions; a negative HRESULT with base `kPreferredImageBase + 0x1000` must leave the complete context unchanged.

```cpp
auto wrong_base_direct = direct_before;
failures += Expect(
    !ApplyRendererDeviceLossDirectLockSkip(
        wrong_base_direct,
        kPreferredImageBase + 0x1000U) &&
        ContextEquals(wrong_base_direct, direct_before),
    "direct Lock redirect rejects an unexpected image base");

auto wrong_base_unlock = unlock_before;
failures += Expect(
    !ApplyRendererDeviceLossUnlockCompletion(
        wrong_base_unlock,
        kPreferredImageBase + 0x1000U) &&
        ContextEquals(wrong_base_unlock, unlock_before),
    "buffered Unlock redirect rejects an unexpected image base");
```

- [ ] **Step 2: Expand failing installer tests to ten contracts and six hooks**

Replace the fake state's per-site byte arrays with one `ExpectedContractBytes(RendererContractSite)` switch returning a `std::span<const std::byte>` for all ten production arrays. `ReadInstallMemory` must copy that span and flip its first output byte only when `state.mismatch == site`; this keeps the fake focused on transaction behavior rather than duplicating a second manifest.

```cpp
std::span<const std::byte> ExpectedContractBytes(
    RendererContractSite site) noexcept {
    using namespace gc::renderer_device_loss;
    switch (site) {
    case RendererContractSite::DeviceLostTail:
        return kDeviceLostTailPattern;
    case RendererContractSite::VertexBufferResult:
        return kVertexBufferResultPattern;
    case RendererContractSite::IndexBufferResult:
        return kIndexBufferResultPattern;
    case RendererContractSite::InitializerEpilogue:
        return kRendererEpiloguePattern;
    case RendererContractSite::VertexBufferLockGuard:
        return kVertexBufferLockGuardPattern;
    case RendererContractSite::VertexBufferLockFailure:
        return kVertexBufferLockFailurePattern;
    case RendererContractSite::DirectLockResult:
        return kDirectLockResultPattern;
    case RendererContractSite::DirectBatchCleanup:
        return kDirectBatchCleanupPattern;
    case RendererContractSite::BufferedUnlockResult:
        return kBufferedUnlockResultPattern;
    case RendererContractSite::BufferedUnlockContinuation:
        return kBufferedUnlockContinuationPattern;
    case RendererContractSite::None:
        return {};
    }
    return {};
}

bool ReadInstallMemory(
    void* opaque,
    std::uintptr_t address,
    std::span<std::byte> output) noexcept {
    auto& state = *static_cast<FakeInstallState*>(opaque);
    ++state.reads;
    const auto site = SiteForAddress(state, address);
    if (site == RendererContractSite::None ||
        site == state.read_failure) {
        return false;
    }
    const auto source = ExpectedContractBytes(site);
    if (source.empty() || output.size() != source.size()) {
        return false;
    }
    std::ranges::copy(source, output.begin());
    if (site == state.mismatch) {
        output.front() ^= std::byte{0xFF};
    }
    return true;
}
```

The final authoritative test sets are:

```cpp
constexpr std::array contract_sites{
    RendererContractSite::DeviceLostTail,
    RendererContractSite::VertexBufferResult,
    RendererContractSite::IndexBufferResult,
    RendererContractSite::InitializerEpilogue,
    RendererContractSite::VertexBufferLockGuard,
    RendererContractSite::VertexBufferLockFailure,
    RendererContractSite::DirectLockResult,
    RendererContractSite::DirectBatchCleanup,
    RendererContractSite::BufferedUnlockResult,
    RendererContractSite::BufferedUnlockContinuation,
};

constexpr std::array hook_sites{
    RendererContractSite::DeviceLostTail,
    RendererContractSite::VertexBufferResult,
    RendererContractSite::IndexBufferResult,
    RendererContractSite::VertexBufferLockGuard,
    RendererContractSite::DirectLockResult,
    RendererContractSite::BufferedUnlockResult,
};
```

The success case must assert `reads == 10`, `install_calls == 6`, exact site/address order, and `reset_calls == 0`. For each contract, read failure and mismatch must produce the precise site with zero installs. For each hook, creation failure must occur at its one-based position after ten successful reads and cause exactly one reset.

- [ ] **Step 3: Run the focused build and witness RED**

Run:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests"
```

Expected: compilation fails on the two transforms, four RVAs, four patterns,
and four contract-site enum values introduced by these tests.

- [ ] **Step 4: Implement the pure negative-HRESULT redirects**

Add these declarations to the header:

```cpp
inline constexpr std::uint32_t kDirectLockResultRva = 0x000E691EU;
inline constexpr std::uint32_t kDirectBatchCleanupRva = 0x000E6AD6U;
inline constexpr std::uint32_t kBufferedUnlockResultRva = 0x000E5662U;
inline constexpr std::uint32_t kBufferedUnlockContinuationRva =
    0x000E5679U;

[[nodiscard]] bool ApplyRendererDeviceLossDirectLockSkip(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept;

[[nodiscard]] bool ApplyRendererDeviceLossUnlockCompletion(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept;
```

Implement one private helper and two explicit production-facing wrappers:

```cpp
bool ApplyNegativeResultRedirect(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    std::uint32_t target_rva) noexcept {
    if (image_base != kPreferredImageBase ||
        static_cast<std::int32_t>(context.eax) >= 0) {
        return false;
    }
    context.eip = static_cast<std::uint32_t>(image_base + target_rva);
    return true;
}

bool ApplyRendererDeviceLossDirectLockSkip(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept {
    return ApplyNegativeResultRedirect(
        context, image_base, kDirectBatchCleanupRva);
}

bool ApplyRendererDeviceLossUnlockCompletion(
    safetyhook::Context& context,
    std::uintptr_t image_base) noexcept {
    return ApplyNegativeResultRedirect(
        context, image_base, kBufferedUnlockContinuationRva);
}
```

- [ ] **Step 5: Add exact contracts and the last two production hooks**

Add these exact byte arrays:

```cpp
inline constexpr std::array<std::byte, 11> kDirectLockResultPattern{
    std::byte{0x8B}, std::byte{0x4C}, std::byte{0x24}, std::byte{0x14},
    std::byte{0x51}, std::byte{0x8B}, std::byte{0x8E}, std::byte{0xE4},
    std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
};
inline constexpr std::array<std::byte, 12> kDirectBatchCleanupPattern{
    std::byte{0x8B}, std::byte{0xB6}, std::byte{0xE4}, std::byte{0x01},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x8B}, std::byte{0x5E},
    std::byte{0x10}, std::byte{0x39}, std::byte{0x5E}, std::byte{0x0C},
};
inline constexpr std::array<std::byte, 9> kBufferedUnlockResultPattern{
    std::byte{0x85}, std::byte{0xC0}, std::byte{0x7D},
    std::byte{0x13}, std::byte{0x68}, std::byte{0xE4},
    std::byte{0xA5}, std::byte{0x71}, std::byte{0x00},
};
inline constexpr std::array<std::byte, 12>
kBufferedUnlockContinuationPattern{
    std::byte{0x8B}, std::byte{0x86}, std::byte{0x80}, std::byte{0x04},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x8B}, std::byte{0x8E},
    std::byte{0x44}, std::byte{0x07}, std::byte{0x00}, std::byte{0x00},
};
```

Add `direct_lock_result_hook` and `buffered_unlock_result_hook` to the runtime. Their callbacks call only the corresponding pure wrapper inside `try/catch (...)`. Extend `RendererContractSite`, site-name logging, the preflight sequence, the production hook switch, reverse-order reset, and the success log.

Represent the complete preflight set explicitly so the installer cannot begin
hook creation after checking only a prefix:

```cpp
struct ContractSpec {
    RendererContractSite site;
    std::uint32_t rva;
    std::span<const std::byte> pattern;
};

const std::array contract_specs{
    ContractSpec{RendererContractSite::DeviceLostTail, kDeviceLostTailRva, kDeviceLostTailPattern},
    ContractSpec{RendererContractSite::VertexBufferResult, kVertexBufferResultRva, kVertexBufferResultPattern},
    ContractSpec{RendererContractSite::IndexBufferResult, kIndexBufferResultRva, kIndexBufferResultPattern},
    ContractSpec{RendererContractSite::InitializerEpilogue, kRendererInitializerEpilogueRva, kRendererEpiloguePattern},
    ContractSpec{RendererContractSite::VertexBufferLockGuard, kVertexBufferLockGuardRva, kVertexBufferLockGuardPattern},
    ContractSpec{RendererContractSite::VertexBufferLockFailure, kVertexBufferLockFailureRva, kVertexBufferLockFailurePattern},
    ContractSpec{RendererContractSite::DirectLockResult, kDirectLockResultRva, kDirectLockResultPattern},
    ContractSpec{RendererContractSite::DirectBatchCleanup, kDirectBatchCleanupRva, kDirectBatchCleanupPattern},
    ContractSpec{RendererContractSite::BufferedUnlockResult, kBufferedUnlockResultRva, kBufferedUnlockResultPattern},
    ContractSpec{RendererContractSite::BufferedUnlockContinuation, kBufferedUnlockContinuationRva, kBufferedUnlockContinuationPattern},
};
for (const auto& contract : contract_specs) {
    auto checked = preflight(
        contract.site, contract.rva, contract.pattern);
    if (!checked) {
        return checked;
    }
}
```

Use this hook order in both production and tests:

```cpp
struct HookSpec {
    RendererContractSite site;
    std::uint32_t rva;
};

constexpr std::array hook_specs{
    HookSpec{RendererContractSite::DeviceLostTail, kDeviceLostTailRva},
    HookSpec{RendererContractSite::VertexBufferResult, kVertexBufferResultRva},
    HookSpec{RendererContractSite::IndexBufferResult, kIndexBufferResultRva},
    HookSpec{RendererContractSite::VertexBufferLockGuard, kVertexBufferLockGuardRva},
    HookSpec{RendererContractSite::DirectLockResult, kDirectLockResultRva},
    HookSpec{RendererContractSite::BufferedUnlockResult, kBufferedUnlockResultRva},
};
```

All ten preflights must finish before iterating `hook_specs`. If any `install_hook` call fails, invoke `reset_hook` exactly once and return `HookInstall` with that exact site.

- [ ] **Step 6: Run focused Debug and Release verification**

Run:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --build --preset msvc32-debug --target RendererDeviceLossPatchTests iDmacDrv32 && ctest --preset msvc32-debug -R "^RendererDeviceLossPatchTests$" --output-on-failure"
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --build --preset msvc32-release --target RendererDeviceLossPatchTests iDmacDrv32 && ctest --preset msvc32-release -R "^RendererDeviceLossPatchTests$" --output-on-failure"
```

Expected: the DLL and focused test build in both configurations and both focused CTest runs pass.

- [ ] **Step 7: Commit the transition-time guards**

```powershell
git add -- src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp tests/Patches/RendererDeviceLossPatchTests.cpp
git commit -m "fix: contain lost-device buffer failures"
```

### Task 4: Verify Binary Contracts and Complete Repository Graphs

**Files:**
- Verify: `H:\gc\game471.exe.i64`
- Verify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Verify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- Verify: `tests/Patches/RendererDeviceLossPatchTests.cpp`

**Interfaces:**
- Consumes: completed ten-contract/six-hook implementation and current IDA database.
- Produces: static verification evidence and a precise runtime-acceptance handoff; it does not deploy the DLL or claim gameplay success.

- [ ] **Step 1: Re-read all ten contracts through the existing IDA daemon**

From `H:\IDACLI`, use `AgentSession.start(..., daemon=True, require_ida=True)`, call `probe_backend(require_ida=True)`, and read the exact lengths at these absolute addresses:

```python
contracts = {
    "device_lost_tail": (0x004E67D8, 12),
    "vertex_buffer_result": (0x004E79F7, 7),
    "index_buffer_result": (0x004E7A84, 9),
    "initializer_epilogue": (0x004E7EE9, 7),
    "empty_vector_check": (0x004E5578, 9),
    "lock_failure_epilogue": (0x004E55E2, 12),
    "direct_lock_result": (0x004E691E, 11),
    "direct_batch_cleanup": (0x004E6AD6, 12),
    "buffered_unlock_result": (0x004E5662, 9),
    "buffered_unlock_continuation": (0x004E5679, 12),
}
__result__ = {
    name: ida_bytes.get_bytes(address, size).hex(" ")
    for name, (address, size) in contracts.items()
}
```

Expected: every value exactly matches the corresponding header array; any difference blocks completion rather than weakening the preflight.

- [ ] **Step 2: Run the complete x86 Debug graph**

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug && ctest --preset msvc32-debug -j 4 --output-on-failure"
```

Expected: configuration and complete build succeed and CTest reports zero failures.

- [ ] **Step 3: Run the complete x86 Release graph**

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat"" >nul && cmake --preset msvc32-release && cmake --build --preset msvc32-release && ctest --preset msvc32-release -j 4 --output-on-failure"
```

Expected: configuration and complete build succeed and CTest reports zero failures.

- [ ] **Step 4: Inspect repository hygiene and owned history**

Run:

```powershell
git diff --check
git status --short --branch
git log -6 --oneline
git show --stat --oneline HEAD~2..HEAD
```

Expected: no whitespace errors or generated build artifacts; the only unrelated working-tree path remains `src/Rfid/Feature.cpp`; implementation commits contain only the three owned renderer files.

- [ ] **Step 5: Hand off fullscreen runtime acceptance**

Report the exact IDA contract values, Debug and Release test totals, and source commits. State that static verification is complete but gameplay acceptance is pending. The user-run exercise is: confirm Shift-JIS `system.cfg` has `CheckDeviceLost=1`, launch fullscreen, reach an actively rendering scene, Alt+Tab away and back several times, and confirm the process remains alive and rendering resumes.

Do not copy `iDmacDrv32.dll` into `H:\gc` unless the user separately requests deployment.
