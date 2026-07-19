# Complete High-FPS Timing Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct every proven high-FPS timing-domain regression while retaining target-rate gameplay, chart rendering, stage transforms, and input polling for `game471.exe`.

**Architecture:** Keep absolute-time and target-frame simulation native, scale only native-tick duration initializers, and convert to authored 60 Hz only at final asset/cadence boundaries. The complete transformed patch remains one fail-closed transaction containing exactly 17 checked writes and 41 checked hooks; native 60 FPS retains only the outer cap-validation hook.

**Tech Stack:** C++23, Win32/x86, SafetyHook 0.6.9, plog, CMake/Ninja presets, CTest, WinDbgX, and the daemon-backed `H:\gc\game471.exe.i64` IDA database.

**Design:** [Complete High-FPS Timing Fix Design](../specs/2026-07-19-complete-high-fps-timing-fix-design.md)

**Implementation baseline:** `ceb2aad` (`docs: specify complete high fps timing fix`)

## Global Constraints

- Treat `H:\gc\artifacts\GCLoader` as the source/commit tree and `H:\gc` as the runtime/deployment tree.
- Do not modify `game471.exe` on disk and do not deploy a DLL while `game471.exe` is running.
- At `target_fps = 60`, install no transformed timing write or hook; retain only `OuterFrame` for external-cap validation.
- Keep judgement windows, note times, offsets, IFBL float waits, stage transforms/colors, and audio cursor calculations in milliseconds or seconds.
- Keep `Tune+0x10`, chart frames, native counters, input sampling, and sequence tasks at the configured target cadence.
- Scale only positive native-tick durations with checked round-half-up `value * target_fps / 60`; preserve zero and negative signed sentinels bit-for-bit.
- Preserve MovieClip, gameplay-effect, countdown, player-position, stage-clip, and other authored asset indices at 60 frames per second.
- Preserve IFBL type `0x17`/`0x18` loop cardinality exactly. For type `0x11`, preserve polling yields 0/1 and duration-scale only values greater than 1.
- Never reinstall complete-task `NewsUpdate` or `NoticeUpdate` gates, `IfblLoop` scaling, or `PlayerPositionCountdown` gating.
- Replace the QPC-authored accumulator with deterministic rational phase. QPC remains required for the external-cap monitor and five-second statistics cadence.
- Do not patch the shared `60.0F` object or globally revert `Tune+0x18`, `0x006F4604`, or `0x006FC280`.
- Keep all 17 writes and 41 hooks in one preflight-first transaction. Any mismatch, install failure, memory-read failure, or checked-conversion failure must fail closed through the existing one-shot fatal path.
- Keep `OuterFrame` last in the hook contract array so cap validation begins only after every transformed hook is installed.
- Do not add a global pressed-edge latch, edge stretching, input accessor detour, or transition queue. Card-result input is an explicit runtime evidence gate.
- Use the existing daemon through `AgentSession.connect(r'H:\gc\game471.exe.i64', request_timeout_s=120)` for any additional binary question; do not start a competing IDA session.
- Build only with the repository's `msvc32-debug` and `msvc32-release` presets. Static verification is not gameplay acceptance.

## File and Responsibility Map

| File | Responsibility in this correction |
|---|---|
| `src/Patches/Framerate/FramerateAuthoredClock.h/.cpp` | Deterministic 60 Hz phase clock plus checked positive-duration and player-position elapsed mapping. |
| `src/Patches/Framerate/FramerateHookTransforms.h/.cpp` | New unit-testable operand layouts and register/memory transformations used by SafetyHook callbacks. |
| `src/Patches/Framerate/FrameratePatchPlan.h/.cpp` | Exact 17-write plan, final 41-hook ID set, RVAs, expected bytes, and ordering. |
| `src/Patches/Framerate/FrameratePatchTransaction.h/.cpp` | Exact fixed capacities for 17 writes and 41 hooks; transaction behavior otherwise remains unchanged. |
| `src/Patches/Framerate/FrameratePatch.cpp` | Hook storage/bindings, deterministic cadence publication, effect/countdown/player callbacks, runtime counters, and transaction composition. |
| `src/Patches/Framerate/FramerateDiagnostics.h/.cpp` | Startup patch summary and fail-closed diagnostic wording. |
| `src/Patches/CMakeLists.txt` | Compile the new transform implementation into `gc_runtime_patches`. |
| `tests/Patches/Framerate/FramerateAuthoredClockTests.cpp` | Phase sequences and domain-conversion arithmetic. |
| `tests/Patches/Framerate/FrameratePatchPlanTests.cpp` | Exact 17-write/41-hook matrix and original-byte contracts. |
| `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp` | Failure injection at all 17 write and 41 hook positions. |
| `tests/Patches/Framerate/FramerateRuntimeTests.cpp` | Operand layout, register isolation, safe read/error paths, and complete runtime binding coverage. |
| `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp` | Native/transformed startup summary fields and one-shot fatal behavior. |
| `.planning/debug/high-fps-timing-domains/` | Non-release runtime traces and operator acceptance evidence; never link these files into the DLL. |

---

### Task 1: Add Deterministic Authored Timing and Domain Helpers

**Files:**
- Modify: `src/Patches/Framerate/FramerateAuthoredClock.h:8-33`
- Modify: `src/Patches/Framerate/FramerateAuthoredClock.cpp:6-87`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h:75-83`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp:320-333`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp:652-670`
- Modify: `tests/Patches/Framerate/FramerateAuthoredClockTests.cpp:21-93`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp:345-352`

**Interfaces:**
- Consumes: `FramerateProfile::target_fps()`, `ScaleDurationFrames(std::int32_t)`, and `MapToAuthored60(std::uint32_t)`.
- Produces: `Authored60PhaseClock(const FramerateProfile&)`, `bool Authored60PhaseClock::Advance()`, `ScalePositiveDuration(const FramerateProfile&, std::uint32_t)`, and `MapPlayerPositionElapsedToAuthored60(const FramerateProfile&, std::uint32_t, std::uint32_t)`.

- [ ] **Step 1: Write failing phase-clock and conversion tests**

Add these cases to `FramerateAuthoredClockTests.cpp` before defining the new APIs:

```cpp
for (const std::uint32_t target :
     {60U, 61U, 120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto profile = FramerateProfile::Create(target).value();
    Authored60PhaseClock clock{profile};
    std::uint32_t ticks = 0;
    for (std::uint32_t call = 0; call < target; ++call) {
        ticks += clock.Advance() ? 1U : 0U;
    }
    failures += Expect(ticks == 60, "phase clock emits 60 ticks per target second");

    Authored60PhaseClock left{profile};
    Authored60PhaseClock right{profile};
    for (std::uint32_t call = 0; call < target * 2; ++call) {
        failures += Expect(
            left.Advance() == right.Advance(),
            "phase clock reconstruction is deterministic");
    }
}

{
    const auto profile = FramerateProfile::Create(240).value();
    Authored60PhaseClock clock{profile};
    constexpr std::array expected{true, false, false, false, true, false, false, false};
    for (const bool value : expected) {
        failures += Expect(clock.Advance() == value, "240 phase sequence");
    }
}

{
    const auto profile = FramerateProfile::Create(144).value();
    failures += Expect(
        ScalePositiveDuration(profile, 25).value() == 60,
        "positive duration scales rationally");
    failures += Expect(
        ScalePositiveDuration(profile, 0).value() == 0 &&
            ScalePositiveDuration(profile, UINT32_MAX).value() == UINT32_MAX,
        "signed nonpositive duration sentinels survive");
}

{
    const auto profile = FramerateProfile::Create(240).value();
    failures += Expect(
        MapPlayerPositionElapsedToAuthored60(profile, 120, 480).value() == 0,
        "player position starts at authored frame zero");
    failures += Expect(
        MapPlayerPositionElapsedToAuthored60(profile, 120, 476).value() == 1,
        "player position maps target elapsed to authored frame");
    failures += Expect(
        MapPlayerPositionElapsedToAuthored60(profile, 120, 0).value() == 120,
        "player position completes at authored duration");
    failures += Expect(
        MapPlayerPositionElapsedToAuthored60(profile, 120, 481).value() == UINT32_MAX,
        "player position preserves negative elapsed sentinel");
}
```

Replace the `ScalePositiveFrameCount` assertions in `FrameratePatchPlanTests.cpp` with calls to `ScalePositiveDuration`; remove the declaration from `FrameratePatchPlan.h` only after the replacement API exists.

- [ ] **Step 2: Run the authored-clock and patch-plan tests to verify the new symbols are missing**

```powershell
cmake --build --preset msvc32-release --target FramerateAuthoredClockTests FrameratePatchPlanTests
```

Expected: compilation fails because `Authored60PhaseClock`, `ScalePositiveDuration`, and `MapPlayerPositionElapsedToAuthored60` are not defined.

- [ ] **Step 3: Implement the deterministic clock and checked conversions**

Add this public contract to `FramerateAuthoredClock.h`:

```cpp
class Authored60PhaseClock {
public:
    explicit Authored60PhaseClock(
        const FramerateProfile& profile) noexcept;

    [[nodiscard]] bool Advance() noexcept;

private:
    std::uint32_t target_fps_{};
    std::uint32_t phase_{};
};

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
ScalePositiveDuration(
    const FramerateProfile& profile,
    std::uint32_t raw_value) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
MapPlayerPositionElapsedToAuthored60(
    const FramerateProfile& profile,
    std::uint32_t raw_total,
    std::uint32_t scaled_remaining) noexcept;
```

Implement it in `FramerateAuthoredClock.cpp`:

```cpp
Authored60PhaseClock::Authored60PhaseClock(
    const FramerateProfile& profile) noexcept
    : target_fps_{profile.target_fps()},
      phase_{target_fps_ - 60U} {
}

bool Authored60PhaseClock::Advance() noexcept {
    phase_ += 60U;
    if (phase_ < target_fps_) {
        return false;
    }
    phase_ -= target_fps_;
    return true;
}

std::expected<std::uint32_t, FramerateProfileError>
ScalePositiveDuration(
    const FramerateProfile& profile,
    std::uint32_t raw_value) noexcept {
    const auto signed_value = static_cast<std::int32_t>(raw_value);
    if (signed_value <= 0) {
        return raw_value;
    }
    const auto scaled = profile.ScaleDurationFrames(signed_value);
    if (!scaled) {
        return std::unexpected(scaled.error());
    }
    return static_cast<std::uint32_t>(scaled.value());
}

std::expected<std::uint32_t, FramerateProfileError>
MapPlayerPositionElapsedToAuthored60(
    const FramerateProfile& profile,
    std::uint32_t raw_total,
    std::uint32_t scaled_remaining) noexcept {
    const auto scaled_total = ScalePositiveDuration(profile, raw_total);
    if (!scaled_total) {
        return std::unexpected(scaled_total.error());
    }
    const std::uint32_t elapsed_target =
        scaled_total.value() - scaled_remaining;
    return MapPositiveTargetFrameToAuthored60(profile, elapsed_target);
}
```

Delete `ScalePositiveFrameCount` from `FrameratePatchPlan.h/.cpp`. Change the existing IFBL wait and temporary IFBL loop call sites in `FrameratePatch.cpp` to `ScalePositiveDuration`; Task 4 removes the loop hook entirely.

- [ ] **Step 4: Run focused tests and verify deterministic arithmetic**

```powershell
cmake --build --preset msvc32-release --target FramerateAuthoredClockTests FrameratePatchPlanTests FramerateRuntimeTests
ctest --preset msvc32-release -R '^Framerate(AuthoredClock|PatchPlan|Runtime)Tests$'
```

Expected: all three focused executables build and CTest reports zero failures. The 240 sequence is exactly `tick, skip, skip, skip` and every tested rate emits 60 ticks per `target_fps` calls.

- [ ] **Step 5: Commit the timing primitives**

```powershell
git add -- src/Patches/Framerate/FramerateAuthoredClock.h src/Patches/Framerate/FramerateAuthoredClock.cpp src/Patches/Framerate/FrameratePatchPlan.h src/Patches/Framerate/FrameratePatchPlan.cpp src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FramerateAuthoredClockTests.cpp tests/Patches/Framerate/FrameratePatchPlanTests.cpp
git commit -m "refactor: add deterministic authored timing primitives"
```

---

### Task 2: Extract Unit-Testable Hook Transformations and Operand Layouts

**Files:**
- Create: `src/Patches/Framerate/FramerateHookTransforms.h`
- Create: `src/Patches/Framerate/FramerateHookTransforms.cpp`
- Modify: `src/Patches/CMakeLists.txt:1-10`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp:1-96`

**Interfaces:**
- Consumes: Task 1's `ScalePositiveDuration` and `MapPlayerPositionElapsedToAuthored60`, plus `safetyhook::Context`.
- Produces: `AuthoredFrameOperand`, `PlayerPositionDurationOperand`, `RuntimeReadU32`, `RedirectEaxToAuthoredOperand`, `RedirectEcxToAuthoredOperand`, `RedirectEdxToAuthoredOperand`, `MapCountdownAssetFrame`, `ScalePlayerPositionDurationEax`, `MapPlayerPositionAssetFrame`, and `PreparePlayerPositionDenominator`.

- [ ] **Step 1: Write failing operand-layout, register-isolation, and read-failure tests**

Include `FramerateHookTransforms.h` from `FramerateRuntimeTests.cpp`. Add a fake reader with controlled output and captured address:

```cpp
namespace {

std::uintptr_t g_read_address{};
std::uint32_t g_read_value{};
bool g_read_succeeds{true};

bool ReadTransformValue(
    std::uintptr_t address,
    std::uint32_t& value) noexcept {
    g_read_address = address;
    if (!g_read_succeeds) {
        return false;
    }
    value = g_read_value;
    return true;
}

} // namespace
```

Add the following assertions to `main()`:

```cpp
static_assert(offsetof(AuthoredFrameOperand, frame_milliseconds) == 0x18);
static_assert(offsetof(PlayerPositionDurationOperand, duration_frames) == 0xC4);

AuthoredFrameOperand authored_operand{};
safetyhook::Context redirected{};
redirected.eax = 1;
redirected.ecx = 2;
redirected.edx = 3;
redirected.eip = 4;
RedirectEcxToAuthoredOperand(redirected, authored_operand);
failures += Expect(
    redirected.eax == 1 &&
        redirected.ecx == reinterpret_cast<std::uintptr_t>(&authored_operand) &&
        redirected.edx == 3 && redirected.eip == 4,
    "authored operand changes only selected register");

const auto profile240 = FramerateProfile::Create(240).value();
safetyhook::Context countdown{};
countdown.ecx = 480;
countdown.eip = 0x1111;
failures += Expect(
    MapCountdownAssetFrame(countdown, profile240).has_value() &&
        countdown.ecx == 120 && countdown.eip == 0x1111,
    "countdown maps final asset frame and executes original store");

safetyhook::Context initializer{};
initializer.eax = 120;
initializer.eip = 0x2222;
failures += Expect(
    ScalePlayerPositionDurationEax(initializer, profile240).has_value() &&
        initializer.eax == 480 && initializer.eip == 0x2222,
    "player initializer scales EAX and executes original store");

safetyhook::Context asset{};
asset.eax = 120;
asset.edx = 0x1000;
asset.ecx = 3;
asset.eip = 0x2000;
g_read_value = 476;
g_read_succeeds = true;
failures += Expect(
    MapPlayerPositionAssetFrame(
        asset, profile240, ReadTransformValue).has_value() &&
        g_read_address == 0x1000 + 3 * 4 + 0x1D54 &&
        asset.eax == 1 && asset.eip == 0x2007,
    "player asset hook reads indexed remaining and skips seven bytes");

PlayerPositionDurationOperand duration_operand{};
safetyhook::Context denominator{};
denominator.eax = 0x3000;
denominator.eip = 0x4000;
g_read_value = 120;
failures += Expect(
    PreparePlayerPositionDenominator(
        denominator, profile240, duration_operand, ReadTransformValue).has_value() &&
        g_read_address == 0x30C4 &&
        duration_operand.duration_frames == 480 &&
        denominator.eax == reinterpret_cast<std::uintptr_t>(&duration_operand) &&
        denominator.eip == 0x4000,
    "denominator redirects EAX and leaves original fild active");

g_read_succeeds = false;
safetyhook::Context failed_asset{};
failed_asset.eax = 120;
failed_asset.edx = 0x5000;
failed_asset.ecx = 1;
failed_asset.eip = 0x6000;
failures += Expect(
    !MapPlayerPositionAssetFrame(
        failed_asset, profile240, ReadTransformValue) &&
        failed_asset.eax == 120 && failed_asset.eip == 0x6000,
    "read failure leaves player context unchanged");
```

Add the remaining register, sentinel, completion, and denominator-failure cases explicitly:

```cpp
safetyhook::Context eax_redirect{};
eax_redirect.eax = 1;
eax_redirect.ecx = 2;
eax_redirect.edx = 3;
RedirectEaxToAuthoredOperand(eax_redirect, authored_operand);
failures += Expect(
    eax_redirect.eax == reinterpret_cast<std::uintptr_t>(&authored_operand) &&
        eax_redirect.ecx == 2 && eax_redirect.edx == 3,
    "EAX authored redirect preserves ECX and EDX");

safetyhook::Context edx_redirect{};
edx_redirect.eax = 1;
edx_redirect.ecx = 2;
edx_redirect.edx = 3;
RedirectEdxToAuthoredOperand(edx_redirect, authored_operand);
failures += Expect(
    edx_redirect.eax == 1 && edx_redirect.ecx == 2 &&
        edx_redirect.edx == reinterpret_cast<std::uintptr_t>(&authored_operand),
    "EDX authored redirect preserves EAX and ECX");

for (const std::uint32_t sentinel : {0U, UINT32_MAX}) {
    safetyhook::Context sentinel_context{};
    sentinel_context.eax = sentinel;
    failures += Expect(
        ScalePlayerPositionDurationEax(
            sentinel_context, profile240).has_value() &&
            sentinel_context.eax == sentinel,
        "player initializer preserves signed nonpositive sentinel");
}

safetyhook::Context overflow_context{};
overflow_context.eax = static_cast<std::uint32_t>(INT32_MAX);
const auto profile500 = FramerateProfile::Create(500).value();
failures += Expect(
    !ScalePlayerPositionDurationEax(
        overflow_context, profile500) &&
        overflow_context.eax == static_cast<std::uint32_t>(INT32_MAX),
    "player initializer rejects overflow without mutation");

safetyhook::Context completed_asset{};
completed_asset.eax = 120;
completed_asset.edx = 0x7000;
completed_asset.ecx = 0;
completed_asset.eip = 0x8000;
g_read_value = 0;
g_read_succeeds = true;
failures += Expect(
    MapPlayerPositionAssetFrame(
        completed_asset, profile240, ReadTransformValue).has_value() &&
        completed_asset.eax == 120 && completed_asset.eip == 0x8007,
    "completed player duration maps to authored frame 120");

PlayerPositionDurationOperand unchanged_operand{};
unchanged_operand.duration_frames = 77;
safetyhook::Context failed_denominator{};
failed_denominator.eax = 0x9000;
failed_denominator.eip = 0xA000;
g_read_succeeds = false;
failures += Expect(
    !PreparePlayerPositionDenominator(
        failed_denominator,
        profile240,
        unchanged_operand,
        ReadTransformValue) &&
        failed_denominator.eax == 0x9000 &&
        failed_denominator.eip == 0xA000 &&
        unchanged_operand.duration_frames == 77,
    "denominator read failure leaves context and operand unchanged");

for (const std::uint32_t target :
     {60U, 61U, 120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto profile = FramerateProfile::Create(target).value();
    std::uint32_t previous = 0;
    for (std::uint32_t frame = 0; frame <= target * 2U; ++frame) {
        safetyhook::Context mapped_context{};
        mapped_context.ecx = frame;
        failures += Expect(
            MapCountdownAssetFrame(mapped_context, profile).has_value() &&
                mapped_context.ecx >= previous,
            "countdown asset mapping is monotonic");
        previous = mapped_context.ecx;
    }
    failures += Expect(
        previous == 120,
        "two target seconds map to 120 authored countdown frames");
}
```

- [ ] **Step 2: Run the runtime test to verify the transform module is absent**

```powershell
cmake --build --preset msvc32-release --target FramerateRuntimeTests
```

Expected: compilation fails because `FramerateHookTransforms.h` and its symbols do not exist.

- [ ] **Step 3: Define the fixed-layout operands and transform API**

Create `FramerateHookTransforms.h` with this complete public surface:

```cpp
#pragma once

#include "Patches/Framerate/FramerateAuthoredClock.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <safetyhook.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace gc::framerate {

enum class FramerateHookTransformError {
    MemoryRead,
    ProfileConversion,
};

struct AuthoredFrameOperand {
    std::array<std::byte, 0x18> padding{};
    float frame_milliseconds{1000.0F / 60.0F};
};

struct PlayerPositionDurationOperand {
    std::array<std::byte, 0xC4> padding{};
    std::int32_t duration_frames{};
};

static_assert(offsetof(AuthoredFrameOperand, frame_milliseconds) == 0x18);
static_assert(offsetof(PlayerPositionDurationOperand, duration_frames) == 0xC4);

using RuntimeReadU32 = bool (*)(
    std::uintptr_t address,
    std::uint32_t& value) noexcept;

void RedirectEaxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept;
void RedirectEcxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept;
void RedirectEdxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
MapCountdownAssetFrame(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
ScalePlayerPositionDurationEax(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
MapPlayerPositionAssetFrame(
    safetyhook::Context& context,
    const FramerateProfile& profile,
    RuntimeReadU32 read_u32) noexcept;

[[nodiscard]] std::expected<void, FramerateHookTransformError>
PreparePlayerPositionDenominator(
    safetyhook::Context& context,
    const FramerateProfile& profile,
    PlayerPositionDurationOperand& operand,
    RuntimeReadU32 read_u32) noexcept;

} // namespace gc::framerate
```

- [ ] **Step 4: Implement transformations without mutating context on failure**

Create `FramerateHookTransforms.cpp`. Use this structure; every calculation completes before the destination register or operand is changed:

```cpp
#include "Patches/Framerate/FramerateHookTransforms.h"

#include <bit>
#include <cstdint>

namespace gc::framerate {

namespace {

std::uint32_t OperandAddress(const void* operand) noexcept {
    static_assert(sizeof(void*) == sizeof(std::uint32_t));
    return static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(operand));
}

} // namespace

void RedirectEaxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept {
    context.eax = OperandAddress(&operand);
}

void RedirectEcxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept {
    context.ecx = OperandAddress(&operand);
}

void RedirectEdxToAuthoredOperand(
    safetyhook::Context& context,
    const AuthoredFrameOperand& operand) noexcept {
    context.edx = OperandAddress(&operand);
}

std::expected<void, FramerateHookTransformError>
MapCountdownAssetFrame(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept {
    const auto mapped = MapPositiveTargetFrameToAuthored60(
        profile, context.ecx);
    if (!mapped) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    context.ecx = mapped.value();
    return {};
}

std::expected<void, FramerateHookTransformError>
ScalePlayerPositionDurationEax(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept {
    const auto scaled = ScalePositiveDuration(profile, context.eax);
    if (!scaled) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    context.eax = scaled.value();
    return {};
}

std::expected<void, FramerateHookTransformError>
MapPlayerPositionAssetFrame(
    safetyhook::Context& context,
    const FramerateProfile& profile,
    RuntimeReadU32 read_u32) noexcept {
    std::uint32_t remaining{};
    const std::uint32_t address =
        context.edx + context.ecx * 4U + 0x1D54U;
    if (read_u32 == nullptr || !read_u32(address, remaining)) {
        return std::unexpected(FramerateHookTransformError::MemoryRead);
    }
    const auto mapped = MapPlayerPositionElapsedToAuthored60(
        profile, context.eax, remaining);
    if (!mapped) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    context.eax = mapped.value();
    context.eip += 7U;
    return {};
}

std::expected<void, FramerateHookTransformError>
PreparePlayerPositionDenominator(
    safetyhook::Context& context,
    const FramerateProfile& profile,
    PlayerPositionDurationOperand& operand,
    RuntimeReadU32 read_u32) noexcept {
    std::uint32_t raw_duration{};
    const std::uint32_t address = context.eax + 0xC4U;
    if (read_u32 == nullptr || !read_u32(address, raw_duration)) {
        return std::unexpected(FramerateHookTransformError::MemoryRead);
    }
    const auto scaled = ScalePositiveDuration(profile, raw_duration);
    if (!scaled) {
        return std::unexpected(
            FramerateHookTransformError::ProfileConversion);
    }
    const auto duration = std::bit_cast<std::int32_t>(scaled.value());
    operand.duration_frames = duration;
    context.eax = OperandAddress(&operand);
    return {};
}

} // namespace gc::framerate
```

Add `Framerate/FramerateHookTransforms.cpp` to `gc_runtime_patches` in `src/Patches/CMakeLists.txt`. Do not create a new hook installer or test executable.

- [ ] **Step 5: Run the transform and arithmetic tests**

```powershell
cmake --build --preset msvc32-release --target FramerateRuntimeTests FramerateAuthoredClockTests
ctest --preset msvc32-release -R '^Framerate(Runtime|AuthoredClock)Tests$'
```

Expected: both tests pass; read failures and overflow failures leave the input context unmodified, only the player asset helper advances EIP, and operand offsets are exactly `0x18` and `0xC4`.

- [ ] **Step 6: Commit the transform seam**

```powershell
git add -- src/Patches/Framerate/FramerateHookTransforms.h src/Patches/Framerate/FramerateHookTransforms.cpp src/Patches/CMakeLists.txt tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "refactor: isolate framerate hook transformations"
```

---

### Task 3: Scale Non-Song Menu Repeat and Expand the Atomic Transaction

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h:20-27`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp:152-283`
- Modify: `src/Patches/Framerate/FrameratePatchTransaction.h:11-13`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp:96-238`
- Modify: `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp:31-251`

**Interfaces:**
- Consumes: `FramerateProfile::ScaleDurationFrames` and the existing checked-write transaction.
- Produces: a `FramerateDirectPatchPlan` with exactly 17 writes plus `menu_repeat_initial` and `menu_repeat_interval`, and transaction capacities exactly equal to 17 writes and 41 hooks.

- [ ] **Step 1: Change direct-plan expectations to 17 and add exact menu-value tests**

Extend `FrameratePatchPlanTests.cpp` with direct reads at the two data RVAs:

```cpp
failures += Expect(plan.count == 17, "high target has 17 direct writes");
failures += Expect(
    ReadInstructionImmediate(plan, 0x00382CE8, 0) ==
        static_cast<std::uint32_t>(
            profile.ScaleDurationFrames(16).value()),
    "non-song initial repeat duration");
failures += Expect(
    ReadInstructionImmediate(plan, 0x00382CEC, 0) ==
        static_cast<std::uint32_t>(
            profile.ScaleDurationFrames(3).value()),
    "non-song repeat interval");
failures += Expect(
    plan.menu_repeat_initial == profile.ScaleDurationFrames(16).value() &&
        plan.menu_repeat_interval == profile.ScaleDurationFrames(3).value(),
    "plan exposes menu values for startup diagnostics");

failures += Expect(
    native_plan.menu_repeat_initial == 16 &&
        native_plan.menu_repeat_interval == 3,
    "native plan exposes original menu values without writes");
```

Add these rows to the exact expected-write matrix:

```cpp
{0x00382CE8, Pattern({0x10, 0x00, 0x00, 0x00})},
{0x00382CEC, Pattern({0x03, 0x00, 0x00, 0x00})},
```

Add the exact target table:

```cpp
struct MenuCase {
    std::uint32_t target;
    std::uint32_t initial;
    std::uint32_t interval;
};
constexpr std::array menu_cases{
    MenuCase{61, 16, 3},
    MenuCase{120, 32, 6},
    MenuCase{144, 38, 7},
    MenuCase{165, 44, 8},
    MenuCase{240, 64, 12},
    MenuCase{360, 96, 18},
    MenuCase{500, 133, 25},
};
for (const auto& item : menu_cases) {
    const auto plan = BuildFramerateDirectPatchPlan(
        kFakeBase,
        FramerateProfile::Create(item.target).value(),
        kFakeTargetOperand).value();
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00382CE8, 0) == item.initial &&
            ReadInstructionImmediate(plan, 0x00382CEC, 0) == item.interval,
        "exact non-song repeat replacements");
}
```

- [ ] **Step 2: Expand transaction tests to exercise every final capacity position**

Start the test with exact final-capacity assertions:

```cpp
static_assert(kMaximumFramerateWrites == 17);
static_assert(kMaximumFramerateHooks == 41);
```

Change the transaction fixture to fixed final capacities:

```cpp
struct Fixture {
    FakeMemory memory{};
    std::array<std::byte, 1024> original_bytes{};
    FakeHook hook_state{};
    std::array<FakeHookContext, kMaximumFramerateHooks> hook_contexts{};
    std::array<CheckedWrite, kMaximumFramerateWrites> writes{};
    std::array<HookOperation, kMaximumFramerateHooks> hooks{};

    Fixture();
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};
```

Resize `FakeMemory::bytes` to 1024. Initialize one-byte writes at `16 + index * 4` and one-byte hook contracts at `256 + index * 4` with this constructor:

```cpp
Fixture::Fixture() {
    for (std::size_t index = 0; index < writes.size(); ++index) {
        const auto address = static_cast<std::uintptr_t>(16 + index * 4);
        const auto expected = Pattern({
            static_cast<std::uint8_t>(0x10 + index)});
        const auto replacement = Pattern({
            static_cast<std::uint8_t>(0x80 + index)});
        const char* name = "fake write";
        if (index == 15) {
            name = "non-song menu repeat initial duration";
        } else if (index == 16) {
            name = "non-song menu repeat interval";
        }
        writes[index] = {
            .address = address,
            .expected = expected,
            .replacement = replacement,
            .name = name,
        };
        memory.bytes[address] = expected.bytes[0];
    }

    for (std::size_t index = 0; index < hooks.size(); ++index) {
        const auto address = static_cast<std::uintptr_t>(256 + index * 4);
        const auto expected = Pattern({
            static_cast<std::uint8_t>(0x70 + index)});
        memory.bytes[address] = expected.bytes[0];
        hook_contexts[index] = {&hook_state, index};
        hooks[index] = {
            .address = address,
            .expected = expected,
            .name = "fake hook",
            .context = &hook_contexts[index],
            .install = InstallFakeHook,
            .reset = ResetFakeHook,
        };
    }
    original_bytes = memory.bytes;
}
```

Replace the hard-coded failure loops with:

```cpp
for (int failed_write = 0;
     failed_write < static_cast<int>(kMaximumFramerateWrites);
     ++failed_write) {
    auto fixture = MakeFixture();
    fixture.memory.fail_write_call = failed_write;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(!result, "every write failure is rejected");
    failures += Expect(
        fixture.memory.bytes == fixture.original_bytes,
        "every write failure restores all 17 writes");
}

for (int failed_hook = 0;
     failed_hook < static_cast<int>(kMaximumFramerateHooks);
     ++failed_hook) {
    auto fixture = MakeFixture();
    fixture.hook_state.fail_on_call = failed_hook;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(!result, "every hook failure is rejected");
    failures += Expect(
        fixture.memory.bytes == fixture.original_bytes &&
            fixture.hook_state.reset ==
                ReverseIndices(static_cast<std::size_t>(failed_hook + 1)),
        "every hook failure restores writes and hooks in reverse order");
}
```

Add separate over-capacity assertions for `kMaximumFramerateWrites + 1` and `kMaximumFramerateHooks + 1`.
Name the fixture writes corresponding to indices 15 and 16 `"non-song menu repeat initial duration"` and `"non-song menu repeat interval"`; after every injected hook failure, explicitly assert their original bytes as well as the complete byte array so the new globals are visibly covered by rollback.

Replace the successful-install hard-coded `{0, 1, 2, 3}` expectation with:

```cpp
std::vector<std::size_t> ForwardIndices(std::size_t count) {
    std::vector<std::size_t> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(index);
    }
    return result;
}

failures += Expect(
    result.has_value() && transaction.committed() &&
        fixture.hook_state.installed ==
            ForwardIndices(kMaximumFramerateHooks) &&
        fixture.memory.bytes != fixture.original_bytes,
    "successful transaction retains all 17 writes and 41 hooks");
```

In the incomplete-rollback case, set:

```cpp
fixture.hook_state.fail_on_call = 0;
fixture.memory.fail_write_call =
    static_cast<int>(kMaximumFramerateWrites);
```

The first 17 write calls then succeed, hook 0 fails, and the first restoration write fails, preserving the existing `rollback_complete=false` proof with the expanded fixture.

- [ ] **Step 3: Run the tests to verify the old 15/16/32 capacities fail**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests FrameratePatchTransactionTests
```

Expected: tests fail because the direct plan still has 15 slots/writes and transaction capacities are still 16/32.

- [ ] **Step 4: Add the two checked data writes and exact final capacities**

Change the plan and transaction declarations:

```cpp
struct FramerateDirectPatchPlan {
    std::array<CheckedWrite, 17> writes{};
    std::size_t count{};
    std::int32_t menu_repeat_initial{16};
    std::int32_t menu_repeat_interval{3};

    [[nodiscard]] std::span<const CheckedWrite> view() const noexcept {
        return {writes.data(), count};
    }
};

inline constexpr std::size_t kMaximumFramerateWrites = 17;
inline constexpr std::size_t kMaximumFramerateHooks = 41;
```

In `BuildFramerateDirectPatchPlan`, calculate both menu values before returning transformed mode, store them in the plan, and add:

```cpp
AddWrite(
    plan, executable_base, 0x00382CE8,
    ValuePattern(16U),
    ValuePattern(static_cast<std::uint32_t>(menu_repeat_initial.value())),
    "non-song menu repeat initial duration") &&
AddWrite(
    plan, executable_base, 0x00382CEC,
    ValuePattern(3U),
    ValuePattern(static_cast<std::uint32_t>(menu_repeat_interval.value())),
    "non-song menu repeat interval") &&
```

Return `ProfileConversion` if either conversion fails. Retain zero direct writes at 60 FPS; its exposed menu values remain 16 and 3.

- [ ] **Step 5: Verify direct bytes and exhaustive rollback**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests FrameratePatchTransactionTests
ctest --preset msvc32-release -R '^Framerate(PatchPlan|PatchTransaction)Tests$'
```

Expected: both tests pass. The transaction test attempts all 17 direct-write failures and all 41 hook-install failures, and the plan test verifies original values `16`/`3` at RVAs `0x00382CE8`/`0x00382CEC`.

- [ ] **Step 6: Commit the menu and transaction correction**

```powershell
git add -- src/Patches/Framerate/FrameratePatchPlan.h src/Patches/Framerate/FrameratePatchPlan.cpp src/Patches/Framerate/FrameratePatchTransaction.h tests/Patches/Framerate/FrameratePatchPlanTests.cpp tests/Patches/Framerate/FrameratePatchTransactionTests.cpp
git commit -m "fix: scale non-song menu timing transactionally"
```

---

### Task 4: Restore Native Sequence Tasks, IFBL Loops, and Player Decrement

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h:29-55`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp:69-148`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp:32-162,262-485,579-694,876-1058`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp:240-314`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp:70-75`

**Interfaces:**
- Consumes: Task 1's `Authored60PhaseClock`, the 21 retained hook contracts, QPC-based `FramerateMonitor`, and existing MovieClip/BGM callbacks.
- Produces: a 21-hook intermediate transformed runtime with deterministic authored cadence and no complete News/Notice, IFBL-loop, or player-decrement hook.

- [ ] **Step 1: Write failing assertions for the four removed contracts and deterministic runtime ownership**

Update the expected hook matrix by deleting the four rows at RVAs `0x00218A50`, `0x002544D0`, `0x00230AB6`, and `0x0024F0C6`. Add:

```cpp
failures += Expect(
    transformed_hooks.size() == 21,
    "intermediate transformed mode has 21 retained hooks");
for (const auto removed_rva :
     {0x00218A50U, 0x002544D0U, 0x00230AB6U, 0x0024F0C6U}) {
    const bool present = std::any_of(
        transformed_hooks.begin(), transformed_hooks.end(),
        [removed_rva](const auto& hook) {
            return hook.rva == removed_rva;
        });
    failures += Expect(!present, "invalid timing contract is absent");
}
failures += Expect(
    transformed_hooks.back().id == FramerateHookId::OuterFrame,
    "outer frame remains last");
```

Keep the runtime test that requires a binding for every returned contract. Change its intermediate capacity assertion to `static_assert(kMaximumFramerateHooks == 41)`.

- [ ] **Step 2: Run plan/runtime tests and observe the four invalid contracts**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests FramerateRuntimeTests
ctest --preset msvc32-release -R '^Framerate(PatchPlan|Runtime)Tests$'
```

Expected: `FrameratePatchPlanTests` fails because the current transformed list still contains 25 contracts and the four forbidden RVAs.

- [ ] **Step 3: Remove obsolete IDs, storage, callbacks, counters, and binding cases**

Delete these enum values and every corresponding runtime member/case/function:

```text
NewsUpdate
NoticeUpdate
IfblLoop
PlayerPositionCountdown
```

Specifically remove `news_update`, `notice_update`, `ifbl_loop`, and `player_position_countdown` hook storage; News/Notice call/skip, IFBL-loop-store, and player decrement/skip counters; `HookNewsUpdate`, `HookNoticeUpdate`, `HookIfblLoop`, and `HookPlayerPositionCountdown`; and their `AssignHookCallbacks` switch cases. Do not replace them with no-op hooks.

Update the outer contract name to `"outer-frame cap validation and deterministic authored phase"`. Keep these inner-domain paths intact:

```text
MovieClipGoto / MovieClipAdvance
PaletteCompare / StageClipFrame
IfblWait
StageBgmPreload
TuneCountdownCompare
AudioSkipMargin / AudioSkipInterval / AudioResyncDiagnostic
GameplayEffectAdvance
EffectCadence6/5/4/16A/16B/8
RemoteCadenceA/B
GameplayBlink
OuterFrame
```

Use this retained-site checklist during the edit; none of these instructions/data objects may be removed or reclassified:

| Absolute EA | RVA | Retained policy |
|---:|---:|---|
| `0x004DF940` | `0x000DF940` | Gate ordinary MovieClip advance with deterministic authored phase; preserve goto-depth bypass. |
| `0x006309D4` | `0x002309D4` | Preserve IFBL type-`0x11` polling yields 0/1; scale only values greater than 1. |
| `0x0061001A` | `0x0021001A` | Gate only the pre-`0x12` stage-BGM increment. |
| `0x00644054` | `0x00244054` | Map only the final stage clip-mask frame index to authored 60. |
| `0x0064BC69` | `0x0024BC69` | Preserve target-frame-to-current-ms multiplication. |
| `0x0064CC7B` | `0x0024CC7B` | Preserve target-frame-to-current-ms multiplication. |
| `0x0064CCAC` | `0x0024CCAC` | Preserve target-frame-to-current-ms multiplication. |
| `0x0064D827` | `0x0024D827` | Preserve target-frame-to-current-ms multiplication. |
| `0x0064F75B` | `0x0024F75B` | Preserve player-position ratio multiplication. |
| `0x0064FD2E` | `0x0024FD2E` | Preserve player-position ratio multiplication. |
| `0x006FC0A0` | `0x002FC0A0` | Keep `Tune+0x18`/gameplay frame milliseconds at `1000 / target_fps`. |
| `0x006F4604` | `0x002F4604` | Keep visual/palette/lane milliseconds at `1000 / target_fps`. |
| `0x006FC280` | `0x002FC280` | Keep target-counter seconds conversion at `1 / target_fps`. |

- [ ] **Step 4: Replace QPC-authored accumulation with the phase clock**

Change `FramerateRuntimeState` to own the clock after `profile` and remove `previous_qpc`, `authored_accumulator`, and `authored_clock_started`:

```cpp
FramerateProfile profile;
FramerateMonitor monitor;
Authored60PhaseClock authored_clock;
std::int64_t qpc_frequency{};
FrameratePlatformActions platform{};
FrameratePatchTransaction transaction;
FramerateHookStorage hooks;
FramerateRuntimeCounters counters;
std::atomic_bool fatal_published{false};
std::atomic_bool authored_60hz_tick{true};
std::int64_t previous_stats_qpc{};
```

Initialize it with `authored_clock{profile}` after moving `profile_value` into `profile`. Delete `kAuthoredUiStepSeconds` and `kMaximumAccumulatedSeconds`. Replace `UpdateAuthored60HzTick(std::int64_t)` with:

```cpp
void UpdateAuthored60HzTick() noexcept {
    const bool tick = g_runtime->authored_clock.Advance();
    g_runtime->authored_60hz_tick.store(tick, std::memory_order_release);
    auto& counter = tick
        ? g_runtime->counters.authored_ticks
        : g_runtime->counters.authored_non_ticks;
    counter.fetch_add(1, std::memory_order_relaxed);
}
```

Initialize `previous_stats_qpc` on the first call inside `MaybeLogRuntimeStats`:

```cpp
if (g_runtime->previous_stats_qpc == 0) {
    g_runtime->previous_stats_qpc = now;
    return;
}
```

In `HookOuterFrame`, keep QPC acquisition and `FramerateMonitor::Observe` unchanged. For transformed timing call `UpdateAuthored60HzTick()` and then `MaybeLogRuntimeStats(now.QuadPart)`. MovieClip and pre-state BGM continue reading `IsAuthored60HzTick()`; News, Notice, IFBL loop, and player decrement now execute untouched binary code every native update/render.

- [ ] **Step 5: Run the focused suite and audit forbidden runtime names**

```powershell
cmake --build --preset msvc32-release --target FramerateAuthoredClockTests FrameratePatchPlanTests FramerateRuntimeTests iDmacDrv32
ctest --preset msvc32-release -R '^Framerate(AuthoredClock|PatchPlan|Runtime)Tests$'

$obsolete = rg -n "NewsUpdate|NoticeUpdate|IfblLoop|PlayerPositionCountdown|authored_accumulator|kAuthoredUiStepSeconds|kMaximumAccumulatedSeconds" src/Patches/Framerate tests/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $obsolete | Write-Host
    throw 'obsolete gate or QPC-authored accumulator remains'
}
if ($LASTEXITCODE -ne 1) { throw "obsolete-name audit failed: $LASTEXITCODE" }
```

Expected: builds/tests pass and `rg` finds none of the removed hook/accumulator names. QPC references remain in monitor/cap validation/statistics code.

- [ ] **Step 6: Commit the native-task restoration**

```powershell
git add -- src/Patches/Framerate/FrameratePatchPlan.h src/Patches/Framerate/FrameratePatchPlan.cpp src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FrameratePatchPlanTests.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "fix: restore native sequence and control flow timing"
```

---

### Task 5: Redirect Authored Gameplay Effects and Map Countdown Assets

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h:29-55`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp:69-148`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp:36-162,262-469,808-875`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp:240-314`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp:21-96`

**Interfaces:**
- Consumes: Task 2's process-lifetime `AuthoredFrameOperand` and register/countdown transforms.
- Produces: 12 local authored-millisecond operand hooks plus one countdown asset-frame hook, bringing the transformed intermediate hook count from 21 to 34.

- [ ] **Step 1: Add failing exact-contract tests for all 13 hooks**

Add these enum IDs to the expected test code and require exactly 34 transformed contracts:

```text
GreatGoodLifetimeOperand
GreatGoodFrameOperand
EffectLifetimeAOperand
EffectFrameAOperand
EffectLifetimeBOperand
EffectFrameBOperand
DirectEffectFrameOperand
ChartEffectFrameAOperand
ChartEffectFrameBOperand
ChartEffectFrameCOperand
ChartEffectFrameDOperand
FixedVisualFrameOperand
GameplayCountdownAssetFrame
```

Append this exact contract matrix immediately before `OuterFrame` in the test's expected array:

| ID | RVA | Expected bytes | Redirect/behavior |
|---|---:|---|---|
| `GreatGoodLifetimeOperand` | `0x002464A8` | `D8 48 18` | dead EAX to authored operand |
| `GreatGoodFrameOperand` | `0x00246528` | `D8 71 18` | dead ECX to authored operand |
| `EffectLifetimeAOperand` | `0x00248F00` | `D8 49 18` | dead ECX to authored operand |
| `EffectFrameAOperand` | `0x00248F8C` | `D8 72 18` | dead EDX to authored operand |
| `EffectLifetimeBOperand` | `0x0024912B` | `D8 49 18` | dead ECX to authored operand |
| `EffectFrameBOperand` | `0x002491E0` | `D8 72 18` | dead EDX to authored operand |
| `DirectEffectFrameOperand` | `0x00249C14` | `D8 72 18` | dead EDX to authored operand |
| `ChartEffectFrameAOperand` | `0x0024BC8B` | `D8 71 18` | dead ECX to authored operand |
| `ChartEffectFrameBOperand` | `0x0024CC8A` | `D8 71 18` | dead ECX to authored operand |
| `ChartEffectFrameCOperand` | `0x0024CCBE` | `D8 72 18` | dead EDX to authored operand |
| `ChartEffectFrameDOperand` | `0x0024D836` | `D8 70 18` | dead EAX to authored operand |
| `FixedVisualFrameOperand` | `0x00250AD5` | `D8 71 18` | dead ECX to authored operand |
| `GameplayCountdownAssetFrame` | `0x00249A9C` | `89 48 08` | map positive ECX; original store executes |

Keep `OuterFrame` last. In `FramerateRuntimeTests`, assert every returned contract has a binding and repeat the register-isolation tests for all three operand redirect helpers.

- [ ] **Step 2: Run plan/runtime tests and verify the 13 contracts are absent**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests FramerateRuntimeTests
```

Expected: compilation or assertions fail because the 13 hook IDs/contracts/bindings do not exist.

- [ ] **Step 3: Add the exact IDs/contracts and process-lifetime storage**

Add the 13 IDs before `OuterFrame` and the exact RVA/byte rows above to `kHookContracts`. Use contract names that preserve the IDA liveness proof, for example:

```cpp
{FramerateHookId::GreatGoodLifetimeOperand, 0x002464A8,
    Pattern(0xD8, 0x48, 0x18),
    "GREAT/GOOD lifetime authored-ms operand (dead EAX)"},
{FramerateHookId::GameplayCountdownAssetFrame, 0x00249A9C,
    Pattern(0x89, 0x48, 0x08),
    "gameplay countdown authored asset-frame mapping"},
```

Add one `safetyhook::MidHook` storage member per ID. Add `AuthoredFrameOperand authored_frame_operand{}` to `FramerateRuntimeState`; because `g_runtime` is emplaced once and never reset, the operand address remains stable for every x87 relocation.

- [ ] **Step 4: Bind register-specific callbacks and retain original instructions**

Define only these shared callbacks; the hook ID/storage member determines the contract site:

```cpp
void HookAuthoredOperandEax(safetyhook::Context& context) {
    RedirectEaxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEcx(safetyhook::Context& context) {
    RedirectEcxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookAuthoredOperandEdx(safetyhook::Context& context) {
    RedirectEdxToAuthoredOperand(
        context, g_runtime->authored_frame_operand);
    g_runtime->counters.authored_operand_redirects.fetch_add(
        1, std::memory_order_relaxed);
}

void HookGameplayCountdownAssetFrame(safetyhook::Context& context) {
    if (!MapCountdownAssetFrame(context, g_runtime->profile)) {
        FatalRuntimeConversion("gameplay countdown asset-frame mapping");
        return;
    }
    g_runtime->counters.countdown_asset_mappings.fetch_add(
        1, std::memory_order_relaxed);
}
```

Bind EAX to `GreatGoodLifetimeOperand` and `ChartEffectFrameDOperand`; ECX to `GreatGoodFrameOperand`, `EffectLifetimeAOperand`, `EffectLifetimeBOperand`, `ChartEffectFrameAOperand`, `ChartEffectFrameBOperand`, and `FixedVisualFrameOperand`; EDX to `EffectFrameAOperand`, `EffectFrameBOperand`, `DirectEffectFrameOperand`, and `ChartEffectFrameCOperand`. Bind the countdown ID to its own callback.

Do not advance EIP in the 12 operand callbacks or countdown callback: SafetyHook must execute each original three-byte x87 instruction or `mov [eax+8],ecx` exactly once with the transformed register.

- [ ] **Step 5: Verify the 34-hook intermediate runtime**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests FramerateRuntimeTests iDmacDrv32
ctest --preset msvc32-release -R '^Framerate(PatchPlan|Runtime)Tests$'
```

Expected: plan/runtime tests pass with 34 contracts, all 13 new byte patterns are exact, all have runtime bindings, and `OuterFrame` remains last.

- [ ] **Step 6: Commit authored gameplay effects and countdown mapping**

```powershell
git add -- src/Patches/Framerate/FrameratePatchPlan.h src/Patches/Framerate/FrameratePatchPlan.cpp src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FrameratePatchPlanTests.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "fix: map gameplay effects to authored frames"
```

---

### Task 6: Convert Player-Position Duration, Ratio, and Asset Domains Coherently

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h:29-68`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp:69-170`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp:36-170,262-500,864-920`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp:240-330`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp:21-120`

**Interfaces:**
- Consumes: Task 2's player-position transforms and `PlayerPositionDurationOperand`, plus `ReadU32Safe` as the injectable production reader.
- Produces: four scaled initialization hooks, one authored asset sink, and two scaled denominator hooks, completing the final 41-hook plan.

- [ ] **Step 1: Add failing exact-contract tests for the seven player hooks**

Add these IDs before `OuterFrame`:

```text
PlayerPositionInitA
PlayerPositionInitB
PlayerPositionInitC
PlayerPositionInitD
PlayerPositionAssetFrame
PlayerPositionDenominatorA
PlayerPositionDenominatorB
```

Require exactly 41 transformed hooks and add the exact rows:

| ID | RVA | Expected bytes | Behavior |
|---|---:|---|---|
| `PlayerPositionInitA` | `0x00263240` | `89 84 91 54 1D 00 00` | scale positive EAX; original store executes |
| `PlayerPositionInitB` | `0x002632B2` | `89 84 8A 54 1D 00 00` | scale positive EAX; original store executes |
| `PlayerPositionInitC` | `0x0026359B` | `89 84 8A 54 1D 00 00` | scale positive EAX; original store executes |
| `PlayerPositionInitD` | `0x00263615` | `89 84 8A 54 1D 00 00` | scale positive EAX; original store executes |
| `PlayerPositionAssetFrame` | `0x0024EF43` | `2B 84 8A 54 1D 00 00` | compute/map elapsed EAX; skip seven-byte subtraction |
| `PlayerPositionDenominatorA` | `0x0024F76D` | `DB 80 C4 00 00 00` | dynamic scaled operand; original `fild` executes |
| `PlayerPositionDenominatorB` | `0x0024FD40` | `DB 80 C4 00 00 00` | dynamic scaled operand; original `fild` executes |

Assert `OuterFrame` is index 40 and every contract has a runtime binding. Retain the existing multiplication sites `0x0064F75B` and `0x0064FD2E` unpatched.

- [ ] **Step 2: Run plan/runtime tests and verify the final seven hooks are absent**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests FramerateRuntimeTests
```

Expected: tests fail because transformed mode still has 34 hooks and no player-position contracts/bindings.

- [ ] **Step 3: Add exact contracts, storage, and dynamic operand state**

Add one `safetyhook::MidHook` storage member per new ID and add this stable state member:

```cpp
PlayerPositionDurationOperand player_position_duration_operand{};
```

Add grouped counters:

```cpp
std::atomic_uint64_t player_position_initializations{0};
std::atomic_uint64_t player_position_asset_mappings{0};
std::atomic_uint64_t player_position_denominator_redirects{0};
```

Add the seven exact contracts from Step 1 immediately before `OuterFrame` and expand the compile-time hook array to 41.

- [ ] **Step 4: Bind the player callbacks with exact original-instruction policy**

Use these callbacks:

```cpp
void HookPlayerPositionInitialization(safetyhook::Context& context) {
    if (!ScalePlayerPositionDurationEax(context, g_runtime->profile)) {
        FatalRuntimeConversion("player-position duration initialization");
        return;
    }
    g_runtime->counters.player_position_initializations.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionAssetFrame(safetyhook::Context& context) {
    if (!MapPlayerPositionAssetFrame(
            context, g_runtime->profile, &ReadU32Safe)) {
        FatalRuntimeConversion("player-position asset-frame mapping");
        return;
    }
    g_runtime->counters.player_position_asset_mappings.fetch_add(
        1, std::memory_order_relaxed);
}

void HookPlayerPositionDenominator(safetyhook::Context& context) {
    if (!PreparePlayerPositionDenominator(
            context,
            g_runtime->profile,
            g_runtime->player_position_duration_operand,
            &ReadU32Safe)) {
        FatalRuntimeConversion("player-position denominator scaling");
        return;
    }
    g_runtime->counters.player_position_denominator_redirects.fetch_add(
        1, std::memory_order_relaxed);
}
```

Bind all four init IDs to `HookPlayerPositionInitialization`, the asset ID to `HookPlayerPositionAssetFrame`, and both denominator IDs to `HookPlayerPositionDenominator`, with separate owned `MidHook` storage members.

The init and denominator callbacks leave EIP unchanged so the original store/`fild` executes. The asset transform advances EIP by exactly seven because it replaces the original subtraction. The original decrement at `0x0064F0C6` executes every native render with no hook.

- [ ] **Step 5: Verify the complete 41-hook plan and player transforms**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests FramerateRuntimeTests FrameratePatchTransactionTests iDmacDrv32
ctest --preset msvc32-release -R '^Framerate(PatchPlan|PatchTransaction|Runtime)Tests$'
```

Expected: all tests pass with exactly 41 transformed contracts, `OuterFrame` last, and exhaustive transaction capacity already covering every hook position.

- [ ] **Step 6: Commit coherent player-position timing**

```powershell
git add -- src/Patches/Framerate/FrameratePatchPlan.h src/Patches/Framerate/FrameratePatchPlan.cpp src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FrameratePatchPlanTests.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "fix: scale player position timing coherently"
```

---

### Task 7: Finish Startup/Runtime Diagnostics and Integrated Contracts

**Files:**
- Modify: `src/Patches/Framerate/FramerateDiagnostics.h:25-27`
- Modify: `src/Patches/Framerate/FramerateDiagnostics.cpp:98-138`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp:64-124,935-1016,1134-1198`
- Modify: `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp:59-80`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp:21-110`

**Interfaces:**
- Consumes: final direct plan metadata, final hook span, immutable/dynamic operands, and grouped runtime counters.
- Produces: `FramerateStartupPatchSummary`, exact startup/commit counts, bounded five-second grouped statistics, and final 17/41 integration assertions.

- [ ] **Step 1: Write failing native/transformed startup-summary tests**

Add this summary type usage to `FramerateDiagnosticsTests.cpp`:

```cpp
const FramerateStartupPatchSummary transformed_summary{
    .direct_write_count = 17,
    .hook_count = 41,
    .menu_repeat_initial = 38,
    .menu_repeat_interval = 7,
    .authored_frame_milliseconds = 1000.0F / 60.0F,
};
ReportFramerateStartup(
    FramerateProfile::Create(144).value(),
    transformed_summary,
    Actions());
failures += Expect(
    Contains(validated_startup.infos[0], "mode=transformed") &&
        Contains(validated_startup.infos[0], "authored_clock=deterministic_phase") &&
        Contains(validated_startup.infos[0], "direct_writes=17") &&
        Contains(validated_startup.infos[0], "hooks=41") &&
        Contains(validated_startup.infos[0], "menu_repeat=38/7") &&
        Contains(validated_startup.infos[0], "news_notice_updates=native") &&
        Contains(validated_startup.infos[0], "ifbl_loops=original") &&
        Contains(validated_startup.infos[0], "player_decrement=native") &&
        Contains(validated_startup.infos[0], "countdown_asset=authored60") &&
        Contains(validated_startup.infos[0], "player_duration=dynamic_scaled"),
    "transformed startup logs complete timing ownership");
```

Add the native block and replace the existing 200 FPS formula-only block with the following so no removed overload remains:

```cpp
DiagnosticState native_startup;
g_state = &native_startup;
ReportFramerateStartup(
    FramerateProfile::Create(60).value(),
    FramerateStartupPatchSummary{
        .direct_write_count = 0,
        .hook_count = 1,
        .menu_repeat_initial = 16,
        .menu_repeat_interval = 3,
        .authored_frame_milliseconds = 1000.0F / 60.0F,
    },
    Actions());
failures += Expect(
    Contains(native_startup.infos[0], "mode=native") &&
        Contains(native_startup.infos[0], "authored_clock=native_bypass") &&
        Contains(native_startup.infos[0], "direct_writes=0") &&
        Contains(native_startup.infos[0], "hooks=1"),
    "native startup reports bypass and one cap hook");

DiagnosticState formula_startup;
g_state = &formula_startup;
ReportFramerateStartup(
    FramerateProfile::Create(200).value(),
    FramerateStartupPatchSummary{
        .direct_write_count = 17,
        .hook_count = 41,
        .menu_repeat_initial = 53,
        .menu_repeat_interval = 10,
        .authored_frame_milliseconds = 1000.0F / 60.0F,
    },
    Actions());
failures += Expect(
    formula_startup.infos.size() == 1 &&
        formula_startup.warnings.size() == 1,
    "formula-only target retains exactly one warning");
```

After the existing runtime-failure assertions, publish the same failure a second time and require the latch to keep exactly one log, modal, termination, and fail-fast sequence:

```cpp
ReportFramerateRuntimeFailure(
    "second injected transform failure",
    runtime_latch,
    Actions());
failures += Expect(
    runtime.errors.size() == 1 &&
        runtime.messages.size() == 1 &&
        runtime.termination_codes.size() == 1 &&
        runtime.fail_fast_calls == 1,
    "runtime transform failure publication is one-shot");
```

- [ ] **Step 2: Run diagnostics tests and observe the missing summary API**

```powershell
cmake --build --preset msvc32-release --target FramerateDiagnosticsTests
```

Expected: compilation fails because `FramerateStartupPatchSummary` and the three-argument `ReportFramerateStartup` overload do not exist.

- [ ] **Step 3: Add the startup summary and exact ownership fields**

Add to `FramerateDiagnostics.h`:

```cpp
struct FramerateStartupPatchSummary {
    std::size_t direct_write_count{};
    std::size_t hook_count{};
    std::int32_t menu_repeat_initial{};
    std::int32_t menu_repeat_interval{};
    float authored_frame_milliseconds{};
};

void ReportFramerateStartup(
    const FramerateProfile& profile,
    const FramerateStartupPatchSummary& summary,
    FrameratePlatformActions actions) noexcept;
```

Include `<cstddef>`. Extend the startup line in `FramerateDiagnostics.cpp` with:

```cpp
<< " authored_clock="
<< (profile.native_timing() ? "native_bypass" : "deterministic_phase")
<< " direct_writes=" << summary.direct_write_count
<< " hooks=" << summary.hook_count
<< " menu_repeat=" << summary.menu_repeat_initial
<< "/" << summary.menu_repeat_interval
<< " authored_frame_ms=" << summary.authored_frame_milliseconds
<< " news_notice_updates=native"
<< " ifbl_loops=original"
<< " player_decrement=native"
<< " countdown_asset=authored60"
<< " player_duration=dynamic_scaled"
```

Retain the external-cap, validated-target warning, conditional `IntervalMode`, termination, and fail-fast behavior unchanged.

- [ ] **Step 4: Compose diagnostics only after both plans exist**

In `FrameratePatchInit`, build `direct_plan`, obtain the final contract span, and build hook operations before reporting startup. Then call:

```cpp
const auto hook_contracts = FramerateHookContracts(
    !g_runtime->profile.native_timing());
const auto hook_operations = BuildHookOperations(
    hook_contracts, *g_runtime);

ReportFramerateStartup(
    g_runtime->profile,
    FramerateStartupPatchSummary{
        .direct_write_count = direct_plan->view().size(),
        .hook_count = hook_operations.view().size(),
        .menu_repeat_initial = direct_plan->menu_repeat_initial,
        .menu_repeat_interval = direct_plan->menu_repeat_interval,
        .authored_frame_milliseconds =
            g_runtime->authored_frame_operand.frame_milliseconds,
    },
    actions);
```

After `transaction.Install` succeeds, emit one bounded line:

```cpp
PLOG_INFO << "FrameratePatch: transaction committed"
          << " direct_writes=" << direct_plan->view().size()
          << " hooks=" << hook_operations.view().size();
```

- [ ] **Step 5: Replace obsolete periodic fields with grouped new counters**

Keep the five-second interval. Remove News/Notice, IFBL-loop, player decrement/skip, and accumulator fields. Add:

```cpp
<< " authored_operands="
<< counters.authored_operand_redirects.load(std::memory_order_relaxed)
<< " countdown_asset="
<< counters.countdown_asset_mappings.load(std::memory_order_relaxed)
<< " player_position="
<< counters.player_position_initializations.load(std::memory_order_relaxed)
<< "/asset="
<< counters.player_position_asset_mappings.load(std::memory_order_relaxed)
<< "/denominator="
<< counters.player_position_denominator_redirects.load(
       std::memory_order_relaxed)
```

Retain authored MovieClip tick/skip, stage BGM tick/skip, IFBL wait, gameplay cadence, stage clip, countdown compare, and audio diagnostic groups. Do not add per-frame or per-effect log calls.

- [ ] **Step 6: Run all focused framerate tests**

```powershell
cmake --build --preset msvc32-release --target FramerateProfileTests FramerateAuthoredClockTests FramerateMonitorTests FramerateDiagnosticsTests FrameratePatchPlanTests FrameratePatchTransactionTests FramerateRuntimeTests iDmacDrv32
ctest --preset msvc32-release -R '^Framerate(Profile|AuthoredClock|Monitor|Diagnostics|PatchPlan|PatchTransaction|Runtime)Tests$'
```

Expected: all seven focused framerate tests pass. Native mode reports 0/1 operations, transformed mode reports 17/41, and every final contract has a runtime binding.

- [ ] **Step 7: Commit diagnostics and final integration**

```powershell
git add -- src/Patches/Framerate/FramerateDiagnostics.h src/Patches/Framerate/FramerateDiagnostics.cpp src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FramerateDiagnosticsTests.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "chore: report corrected framerate timing domains"
```

---

### Task 8: Run Complete Static Verification and Review the Owned Diff

**Files:**
- Verify: every file changed by Tasks 1-7
- Verify: `build-msvc32-debug/dist/iDmacDrv32.dll`
- Verify: `build-msvc32-release/dist/iDmacDrv32.dll`
- Do not modify: `H:\gc\iDmacDrv32.dll`, `H:\gc\config.toml`, or `H:\gc\game471.exe`

**Interfaces:**
- Consumes: seven independently committed implementation deliverables.
- Produces: fresh Debug/RelWithDebInfo build evidence, complete CTest evidence, exact policy audits, and a reviewed candidate ready for operator testing.

- [ ] **Step 1: Configure and build both supported x86 presets from scratch-enough current state**

```powershell
cmake --preset msvc32-debug
if ($LASTEXITCODE -ne 0) { throw "Debug configure failed: $LASTEXITCODE" }
cmake --build --preset msvc32-debug
if ($LASTEXITCODE -ne 0) { throw "Debug build failed: $LASTEXITCODE" }

cmake --preset msvc32-release
if ($LASTEXITCODE -ne 0) { throw "RelWithDebInfo configure failed: $LASTEXITCODE" }
cmake --build --preset msvc32-release
if ($LASTEXITCODE -ne 0) { throw "RelWithDebInfo build failed: $LASTEXITCODE" }
```

Expected: both Win32 builds exit zero and produce `dist/iDmacDrv32.dll` under each preset directory.

- [ ] **Step 2: Run the complete test suite in both configurations**

```powershell
ctest --preset msvc32-debug
if ($LASTEXITCODE -ne 0) { throw "Debug CTest failed: $LASTEXITCODE" }

ctest --preset msvc32-release
if ($LASTEXITCODE -ne 0) { throw "RelWithDebInfo CTest failed: $LASTEXITCODE" }
```

Expected: `100% tests passed, 0 tests failed` in both presets. Record the exact test counts in the execution handoff.

- [ ] **Step 3: Run timing-domain and forbidden-patch audits**

```powershell
$obsolete = rg -n "NewsUpdate|NoticeUpdate|IfblLoop|PlayerPositionCountdown|authored_accumulator|kAuthoredUiStepSeconds|kMaximumAccumulatedSeconds" src/Patches/Framerate tests/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $obsolete | Write-Host
    throw 'obsolete high-FPS timing mechanism remains'
}
if ($LASTEXITCODE -ne 1) { throw "obsolete audit failed: $LASTEXITCODE" }

$limiter = rg -n "Sleep\(|sleep_for|busy.?wait|create_(inline|mid).*Present|kRva.*Present|Hook.*Present" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $limiter | Write-Host
    throw 'framerate patch introduced internal pacing or a presentation hook'
}
if ($LASTEXITCODE -ne 1) { throw "limiter audit failed: $LASTEXITCODE" }

$inputPatch = rg -n "0x00055C80|0x001A5E80|0x00455C80|0x005A5E80" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $inputPatch | Write-Host
    throw 'global input edge or card-result handler was patched'
}
if ($LASTEXITCODE -ne 1) { throw "input audit failed: $LASTEXITCODE" }

$forbiddenRatio = rg -n "target_fps(_)?\s*/\s*60|target_fps\(\)\s*/\s*60" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $forbiddenRatio | Write-Host
    throw 'truncated integer FPS ratio remains'
}
if ($LASTEXITCODE -ne 1) { throw "ratio audit failed: $LASTEXITCODE" }

$wrongDomainHooks = rg -n "0024BC69|0024CC7B|0024CCAC|0024D827|0024F75B|0024FD2E" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $wrongDomainHooks | Write-Host
    throw 'target-ms reconstruction or ratio multiplier was hooked'
}
if ($LASTEXITCODE -ne 1) { throw "retained-domain audit failed: $LASTEXITCODE" }

$requiredRetainedRvas = @('002FC0A0', '002F4604', '002FC280', '00244054')
foreach ($rva in $requiredRetainedRvas) {
    rg -n $rva src/Patches/Framerate/FrameratePatchPlan.cpp
    if ($LASTEXITCODE -ne 0) {
        throw "required retained timing RVA is missing: $rva"
    }
}

$sharedSixtyWrite = rg -n "002FBBAC|0x2FBBAC|kRva.*Shared.*60" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $sharedSixtyWrite | Write-Host
    throw 'shared 60.0 data object is referenced by the patch set'
}
if ($LASTEXITCODE -ne 1) { throw "shared-60 audit failed: $LASTEXITCODE" }
```

Expected: no obsolete gates/QPC accumulator, no internal limiter, no global input/card handler hook, no truncated target/60 arithmetic, no hook at the retained target-ms/ratio sites, and the three target-rate constants plus stage final-sink mapping remain represented.

- [ ] **Step 4: Verify exact operation counts and owned-file scope**

```powershell
build-msvc32-release\tests\Patches\FrameratePatchPlanTests.exe
if ($LASTEXITCODE -ne 0) { throw 'exact patch-plan test failed' }
build-msvc32-release\tests\Patches\FrameratePatchTransactionTests.exe
if ($LASTEXITCODE -ne 0) { throw 'transaction test failed' }

git diff --check ceb2aad..HEAD
if ($LASTEXITCODE -ne 0) { throw 'implementation history has whitespace errors' }
git diff --name-status ceb2aad..HEAD
git diff --stat ceb2aad..HEAD
git log --oneline --decorate ceb2aad..HEAD
git status --short
```

Expected: the exact tests exit zero; changed files are limited to the framerate source/tests, `src/Patches/CMakeLists.txt`, and this plan; seven implementation commits follow the plan; unrelated `.planning` evidence and `tall Microsoft.Gaming.GDKq` remain untouched.

- [ ] **Step 5: Record the static-verification boundary honestly**

The execution handoff must state exact build/test results and also state that static proof does not establish legal/News transition duration, card input acceptance, menu feel, GREAT/GOOD playback, player-position smoothness, stage/chart behavior, judgement equivalence, or audio acceptance. Do not copy the candidate into `H:\gc` during this task.

---

### Task 9: Perform Operator Runtime Acceptance and Card-Input Evidence Capture

**Files:**
- Runtime executable: `H:\gc\game471.exe`
- Runtime configuration: `H:\gc\config.toml`
- Runtime log: `H:\gc\loader-log.txt`
- Candidate DLL: `H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll`
- Evidence directory: `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\traces`
- Results document: `H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\RESULTS.md`

**Interfaces:**
- Consumes: Task 8's statically verified candidate, an operator-controlled external cap exactly matching `target_fps`, WinDbgX, and the already-running IDA daemon for any failed acceptance branch.
- Produces: 60/120/144/240 observed evidence and an explicit user acceptance/rejection. It does not authorize a speculative input patch.

- [ ] **Step 1: Deploy only after explicit operator authorization and a stopped game**

```powershell
$game = Get-Process game471 -ErrorAction SilentlyContinue
if ($game) { throw 'game471.exe is running; stop it normally before deployment' }

$candidate = 'H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll'
$runtime = 'H:\gc\iDmacDrv32.dll'
if (-not (Test-Path -LiteralPath $candidate)) {
    throw 'verified release candidate is missing'
}
Copy-Item -LiteralPath $candidate -Destination $runtime -Force
$hashes = Get-FileHash -Algorithm SHA256 -LiteralPath @($candidate, $runtime)
if ($hashes[0].Hash -ne $hashes[1].Hash) {
    throw 'deployed DLL hash does not match the verified candidate'
}
$hashes
```

Expected: no process is terminated by the workflow, operator configuration is preserved, and candidate/runtime SHA-256 hashes match.

- [ ] **Step 2: Capture the 60 FPS native baseline**

Set `target_fps = 60` through the existing configuration path and set the external cap to 60. Start the game normally. Preserve a clean `loader-log.txt` and record:

```text
DLL SHA-256
target_fps and external cap source/value
legal notice visible start/end timestamps
News 0.5/15/0.5 wait timestamps
non-song initial/repeat timings
card-result deliberate press count and accepted action count
judgement current-ms/window observations
GREAT/GOOD wall duration and authored frame sequence
countdown duration/frame sequence
player-position ratio/asset progression
stage clip visibility and transform smoothness
audio resync/crackle observations
```

Require the startup line to report `mode=native`, `authored_clock=native_bypass`, `direct_writes=0`, and `hooks=1`.

- [ ] **Step 3: Run the same matrix at 120, 144, and 240 FPS**

For each rate, set both `target_fps` and the external cap to the same value, restart the process, and use a fresh log. Require:

- legal notice wall time approximately 2.0 seconds, within 20 ms or one native frame of baseline, whichever is larger;
- News waits approximately 0.5/15/0.5 seconds rather than scaling by `target_fps / 60`;
- ordinary MovieClip and stage-BGM preload cadence at exactly 60 authored ticks per target second;
- non-song repeat beginning near 266.7 ms and repeating near 50 ms, while song-selection repeat remains baseline-equivalent;
- target-rate judgement/chart timing aligned to song milliseconds;
- GREAT/GOOD and representative effect resources using the same authored frames and wall duration as baseline;
- countdown lasting approximately two seconds while mapping to 120 authored frames;
- player-position remaining/ratio state changing at target cadence and its asset frame changing at authored cadence;
- stage transforms remaining target-smooth and clip-mask assets remaining visible;
- no new audio crackle, premature BGM state transition, or resync storm.

Require transformed startup/commit lines to report 17 writes, 41 hooks, deterministic phase, scaled menu values, and a successful complete transaction.

- [ ] **Step 4: Capture card-result edge/handler evidence at 60 and 240 with WinDbgX**

Attach WinDbgX only after the card-result flow is reachable. Open a log and set read-only breakpoints:

The fixed breakpoint-ID syntax and command strings follow Microsoft's [`bp` command reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/bp--bu--bm--set-breakpoint-). The writable `$t0` sequence counter and automatic `$dbgtime` value follow Microsoft's [pseudo-register reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/pseudo-register-syntax).

```text
.logopen /t H:\gc\artifacts\GCLoader\.planning\debug\high-fps-timing-domains\traces\card-input.txt

r @$t0=0

bl

bp0 00455cee "r @$t0=@$t0+1; .time; .printf \"seq=%I64u dbgtime=%I64x edge held=%08x pressed=%08x released=%08x repeat=%08x current_ptr=%08x\\n\", @$t0, @$dbgtime, poi(poi(@esp+4)), poi(poi(@esp+8)), poi(poi(@esp+c)), poi(poi(@esp+10)), poi(@esp+4); gc"

bp1 005a5e80 "r @$t0=@$t0+1; .time; .printf \"seq=%I64u dbgtime=%I64x card_handler enter mode=%u completed=%u\\n\", @$t0, @$dbgtime, by(007f2197), by(007f2196); gc"

bp2 005a5f8c "r @$t0=@$t0+1; .time; .printf \"seq=%I64u dbgtime=%I64x card_handler exit action=%u mode=%u completed=%u\\n\", @$t0, @$dbgtime, @eax, by(007f2197), by(007f2196); gc"

bl
g
```

The first breakpoint is the return of `GC120FPS_GWInputXio_ComputeHeldPressReleaseRepeatBits`; at that point the stack is restored and arguments point to held, pressed, released, and repeat output masks. The second/third breakpoints bracket the card-result handler whose logical pressed-edge checks are at `0x005A5EBA`, `0x005A5ECB`, `0x005A5F08`, and `0x005A5F19`.

The first `bl` must show no pre-existing breakpoint IDs 0, 1, or 2; stop and preserve the debugger state if any of those IDs are already owned. Make the same deliberate, well-separated presses after the confirmation prompt is visibly active at 60 and 240. Record the monotonically increasing `seq`, debugger event time, edge masks, handler entry/exit, return action, and flags `0x007F2197`/`0x007F2196`. Remove exactly the three trace breakpoints and close the log after each short capture:

```text
bc 0 1 2
.logclose
```

Acceptance requires one accepted action for each deliberate press at both rates, with no missing, duplicate, or stuck action.

- [ ] **Step 5: Stop rather than invent an input patch if the card gate fails**

If the 240 trace contains a produced pressed edge but no corresponding accepted handler action, or no produced edge for a deliberate post-prompt press, stop deployment and implementation completion. Reconnect to the existing IDA daemon, trace the exact non-accepting branch from the four logical pressed-edge calls above, and amend the design/spec with a scene-scoped consume-once policy. Do not add a global latch, stretch all edges, gate input, or modify the asynchronous worker based only on feel.

- [ ] **Step 6: Record results and request explicit user acceptance**

Append a per-rate table to `.planning/debug/high-fps-timing-domains/RESULTS.md` with columns:

```text
rate | DLL hash | cap validated | legal/News | menu repeat | card input | judgement/chart | GREAT/GOOD/effects | countdown/player | stage | audio | verdict
```

Report static evidence separately from observed runtime evidence. The patch is complete only after all four rows pass and the user explicitly confirms transitions, navigation, 2D animation, gameplay effects, chart/stage smoothness, judgement, and input feel are correct.
