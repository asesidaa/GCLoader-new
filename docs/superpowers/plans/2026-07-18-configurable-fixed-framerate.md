# Configurable Fixed Framerate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 120-FPS-only runtime patch with an immutable 60-500 FPS target profile, externally paced startup validation, and fail-closed transactional installation for `game471.exe`.

**Architecture:** Configuration produces one validated `target_fps`, which constructs a pure `FramerateProfile` and fixed-storage `FramerateMonitor` before executable memory changes. A binary-backed patch plan converts only the proven timing domains, while a transaction preflights every byte contract and rolls back all direct writes and SafetyHook objects on failure. The existing outer-frame hook supplies both authored-60-Hz timing and startup-only cadence samples; GCLoader never paces presentation itself.

**Tech Stack:** C++23, Win32 x86, SafetyHook, reflect-cpp/TOML, ImGui/SDL3, plog, CMake/Ninja, CTest, and the existing `game471.exe` IDA database.

**Design:** [Configurable Fixed Framerate Design](../specs/2026-07-18-configurable-fixed-framerate-design.md)

## Global Constraints

- `target_fps` is required and must be a whole number from 60 through 500 inclusive.
- Reject `enable_120fps_timer_patches` whenever it appears, including alongside `target_fps`.
- Treat 60 FPS as native timing: install cadence validation only and leave every high-framerate timing site unchanged.
- Treat 120, 144, 165, 240, and 360 FPS as explicitly gameplay-validated targets; allow other in-range values with exactly one startup warning.
- Keep the driver or RTSS cap authoritative. Do not add sleeping, spinning, D3D presentation hooks, or any other limiter to GCLoader.
- Do not assume `target_fps / 60` is integral. Use checked 64-bit rational arithmetic for duration and authored-frame conversions.
- Preserve millisecond judgement, audio cursor, note-source, and stage transform/color domains.
- Preserve the wall-clock-authored 60-Hz boundaries for ordinary MovieClip advance, news, notice, and stage BGM preload.
- Do not patch the shared `60.0f` data object; redirect only the three proven x87 operands to profile-owned target-rate storage.
- Validate cadence with a five-second warm-up, two-second median windows, plus or minus three percent tolerance, and three consecutive windows for success or fatal mismatch.
- Permanently disable cadence monitoring after three matching windows. Do not abort later gameplay because of stalls after startup validation succeeds.
- Never generally recommend `IntervalMode = 1`. Include that hint only for configured targets above 60 when measured cadence is itself within plus or minus three percent of 60 FPS.
- Preflight every direct patch and hook-site byte contract before mutation. Any install failure must remove every installed hook and restore every changed byte before fatal reporting.
- `game471.exe` is PE32 with preferred image base `0x00400000` and `DYNAMIC_BASE` disabled; reject a different loaded base before mutation because several verified instruction contracts contain absolute addresses.
- Keep the runtime target immutable for the process lifetime; changing the configuration requires a restart.
- Build only as Win32/x86. Automated checks do not substitute for operator-observed gameplay acceptance.

## File and Responsibility Map

| File | Responsibility |
|---|---|
| `src/Config/TargetFps.h` | Shared 60-500 range, validated-rate policy, and GUI constants. |
| `src/Config/config.h`, `src/Config/config.cpp` | Required field, strict legacy-key rejection, shared parsing/validation, and `ConfigManager` accessor. |
| `tools/ConfigGUI/Main.cpp`, `config.toml` | Bounded target control, external-cap help text, and native-60 sample configuration. |
| `src/Patches/Framerate/FramerateProfile.h/.cpp` | Immutable derived values and checked rational conversions. |
| `src/Patches/Framerate/FramerateMonitor.h/.cpp` | Allocation-free warm-up/window/median/streak state machine. |
| `src/Patches/Framerate/FramerateDiagnostics.h/.cpp` | One-shot logs, modal text, conditional `IntervalMode` guidance, termination, and fail-fast fallback. |
| `src/Patches/Framerate/FrameratePatchTransaction.h/.cpp` | Fixed-capacity preflight, checked writes, hook leases, reverse rollback, and rollback verification. |
| `src/Patches/Framerate/FrameratePatchPlan.h/.cpp` | Exact `game471.exe` RVA/byte contracts and target-derived direct-patch payloads. |
| `src/Patches/Framerate/FrameratePatch.cpp/.h` | QPC integration, SafetyHook storage, domain-specific callbacks, transaction composition, and public initialization. |
| `src/Loader/DllMain.cpp` | Fail-closed loader composition and target-neutral startup logging. |
| `src/Patches/CMakeLists.txt`, `tests/Patches/CMakeLists.txt` | Production-source ownership and focused test targets. |
| `tests/Config/ConfigFeatureTests.cpp` | Strict TOML migration, range, round-trip, and serialization coverage. |
| `tests/Patches/Framerate/*Tests.cpp` | Profile, monitor, diagnostics, transaction, patch-plan, and hook-math coverage. |

---

### Task 1: Replace the Boolean with a Strict Target-FPS Configuration Contract

**Files:**
- Create: `src/Config/TargetFps.h`
- Modify: `src/Config/config.h`
- Modify: `src/Config/config.cpp`
- Modify: `tests/Config/ConfigFeatureTests.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `config.toml`

**Interfaces:**
- Consumes: Existing `InputConfig`, reflect-cpp TOML serialization, registry validation, and ConfigGUI dirty/save state.
- Produces: `gc::config::TargetFpsConfigValue`, `gc::config::IsTargetFpsInRange(std::uint32_t)`, `gc::config::IsGameplayValidatedTargetFps(std::uint32_t)`, `gc::config::ParseAndValidateInputConfig(std::string_view)`, `gc::config::ValidateInputConfig(const InputConfig&)`, and `ConfigManager::GetTargetFps()`.

- [ ] **Step 1: Change the config fixtures and add strict migration tests**

Add an include of the not-yet-created `Config/TargetFps.h` to `tests/Config/ConfigFeatureTests.cpp`, then replace every fixture field with `target_fps = 60` or `target_fps = 240`. Change the two parsing helpers to exercise the production parser:

```cpp
InputConfig parse_config(const std::string& toml) {
    auto result = gc::config::ParseAndValidateInputConfig(toml);
    if (!result) {
        std::cerr << "Failed to parse test config: " << result.error() << "\n";
        std::exit(1);
    }
    return std::move(result.value());
}

int expect_parse_failure(const std::string& toml, const char* name) {
    if (!gc::config::ParseAndValidateInputConfig(toml)) {
        return 0;
    }
    std::cerr << "Expected parse failure for " << name << "\n";
    return 1;
}
```

Add these exact assertions to `main()` after the existing experimental-config round-trip checks:

```cpp
constexpr std::uint32_t accepted_targets[]{
    60, 61, 120, 144, 165, 240, 360, 500,
};
for (const auto target : accepted_targets) {
    auto text = replace_once(
        std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig,
        "target_fps = 60",
        "target_fps = " + std::to_string(target));
    const auto parsed = parse_config(text);
    failures += expect_u32(
        parsed.experimental().target_fps(), target, "accepted target_fps");
    const auto round_trip = parse_config(rfl::toml::write(parsed));
    failures += expect_u32(
        round_trip.experimental().target_fps(), target, "target_fps round trip");
}

const auto native_config =
    std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;
failures += expect_parse_failure(
    replace_once(native_config, "target_fps = 60", "target_fps = 59"),
    "target_fps below range");
failures += expect_parse_failure(
    replace_once(native_config, "target_fps = 60", "target_fps = 501"),
    "target_fps above range");
failures += expect_parse_failure(
    replace_once(native_config, "target_fps = 60", "target_fps = 120.0"),
    "fractional target_fps");
failures += expect_parse_failure(
    replace_once(native_config, "target_fps = 60\n", ""),
    "missing target_fps");
failures += expect_parse_failure(
    replace_once(
        native_config,
        "target_fps = 60",
        "enable_120fps_timer_patches = false"),
    "obsolete boolean only");
failures += expect_parse_failure(
    replace_once(
        native_config,
        "target_fps = 60",
        "target_fps = 60\nenable_120fps_timer_patches = false"),
    "mixed target and obsolete boolean");

auto invalid_for_gui = parse_config(native_config);
invalid_for_gui.experimental().target_fps = 59;
failures += expect_bool(
    gc::config::ValidateInputConfig(invalid_for_gui).has_value(),
    false,
    "GUI persistence rejects out-of-range target_fps");

failures += expect_bool(
    gc::config::IsGameplayValidatedTargetFps(60) &&
        gc::config::IsGameplayValidatedTargetFps(120) &&
        gc::config::IsGameplayValidatedTargetFps(144) &&
        gc::config::IsGameplayValidatedTargetFps(165) &&
        gc::config::IsGameplayValidatedTargetFps(240) &&
        gc::config::IsGameplayValidatedTargetFps(360) &&
        !gc::config::IsGameplayValidatedTargetFps(200),
    true,
    "gameplay-validated target set");
```

- [ ] **Step 2: Run the focused test and verify RED**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target ConfigFeatureTests
```

Expected: compilation fails because `TargetFps.h`, `ParseAndValidateInputConfig`, and `target_fps` do not yet exist.

- [ ] **Step 3: Add the shared target policy and required data field**

Create `src/Config/TargetFps.h` with exactly:

```cpp
#pragma once

#include <cstdint>

namespace gc::config {

using TargetFpsConfigValue = unsigned long;
static_assert(sizeof(TargetFpsConfigValue) == sizeof(std::uint32_t));

inline constexpr std::uint32_t kMinimumTargetFps = 60;
inline constexpr std::uint32_t kMaximumTargetFps = 500;

[[nodiscard]] constexpr bool IsTargetFpsInRange(
    std::uint32_t value) noexcept {
    return value >= kMinimumTargetFps && value <= kMaximumTargetFps;
}

[[nodiscard]] constexpr bool IsGameplayValidatedTargetFps(
    std::uint32_t value) noexcept {
    switch (value) {
    case 60:
    case 120:
    case 144:
    case 165:
    case 240:
    case 360:
        return true;
    default:
        return false;
    }
}

} // namespace gc::config
```

In `src/Config/config.h`, include `<expected>`, `<string_view>`, and `Config/TargetFps.h`. Replace the obsolete field and accessor with:

```cpp
struct ExperimentalConfig {
    rfl::Rename<"target_fps", gc::config::TargetFpsConfigValue>
        target_fps = gc::config::kMinimumTargetFps;
    rfl::Rename<"enable_testmode_storage_redirect", bool>
        enable_testmode_storage_redirect = false;
    rfl::Rename<"enable_timer_freeze_patches", bool>
        enable_timer_freeze_patches = false;
    rfl::Rename<"enable_nesys_service_adapter_patch", bool>
        enable_nesys_service_adapter_patch = true;
    rfl::Rename<"enable_wasapi_exclusive_audio", bool>
        enable_wasapi_exclusive_audio = false;
    rfl::Rename<
        "wasapi_exclusive_buffer_ms",
        WasapiBufferMillisecondsConfigValue>
        wasapi_exclusive_buffer_ms = 10;
};

namespace gc::config {

[[nodiscard]] std::expected<void, std::string> ValidateInputConfig(
    const InputConfig& config);
[[nodiscard]] std::expected<InputConfig, std::string>
ParseAndValidateInputConfig(std::string_view text);

} // namespace gc::config
```

Add this `ConfigManager` accessor and remove `GetEnable120FpsTimerPatches()`:

```cpp
std::uint32_t GetTargetFps() const {
    return static_cast<std::uint32_t>(
        config.experimental.value().target_fps.value());
}
```

- [ ] **Step 4: Centralize parsing, validation, and legacy-key rejection**

Replace the direct reflect-cpp read in `src/Config/config.cpp` with shared functions using `<toml++/toml.hpp>`:

```cpp
namespace gc::config {

std::expected<void, std::string> ValidateInputConfig(
    const InputConfig& value) {
    const auto target = static_cast<std::uint32_t>(
        value.experimental().target_fps());
    if (!IsTargetFpsInRange(target)) {
        return std::unexpected(
            "Invalid [experimental].target_fps; expected an integer from 60 through 500");
    }

    try {
        ValidateInputPollHertz(value.input_poll_hz());
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }

    if (!gc::nesys_service::IsDottedDecimalIpv4(
            value.nesys().server_ip())) {
        return std::unexpected(
            "Invalid [nesys].server_ip; expected dotted-decimal IPv4");
    }

    const auto registry_validation =
        gc::registry_config::ValidateRegistryConfig(value.registry());
    if (!registry_validation.valid()) {
        return std::unexpected(
            gc::registry_config::FirstRegistryValidationError(
                registry_validation));
    }
    return {};
}

std::expected<InputConfig, std::string> ParseAndValidateInputConfig(
    std::string_view text) {
    try {
        const auto syntax = toml::parse(text);
        if (const auto* experimental = syntax["experimental"].as_table();
            experimental != nullptr &&
            experimental->contains("enable_120fps_timer_patches")) {
            return std::unexpected(
                "Obsolete [experimental].enable_120fps_timer_patches is not supported; replace it with target_fps = 60 through 500");
        }

        auto parsed = rfl::toml::read<InputConfig>(std::string{text});
        if (!parsed) {
            return std::unexpected(
                "Failed to parse config file: " + parsed.error().what());
        }
        if (auto validation = ValidateInputConfig(parsed.value());
            !validation) {
            return std::unexpected(validation.error());
        }
        return std::move(parsed.value());
    } catch (const toml::parse_error& error) {
        return std::unexpected(
            "Failed to parse config file: " + std::string{error.what()});
    }
}

} // namespace gc::config
```

Read the whole `config.toml` in `ConfigManager::ConfigManager()`, call `ParseAndValidateInputConfig`, throw its exact error on failure, and retain the existing successful JSON diagnostic.

- [ ] **Step 5: Replace the ConfigGUI checkbox with a bounded control**

Use `gc::config::ParseAndValidateInputConfig(toml_content)` for GUI loading. Replace the 120-FPS checkbox with:

```cpp
auto& target_fps = g_config.experimental().target_fps();
constexpr gc::config::TargetFpsConfigValue minimum_target =
    gc::config::kMinimumTargetFps;
constexpr gc::config::TargetFpsConfigValue maximum_target =
    gc::config::kMaximumTargetFps;
if (ImGui::SliderScalar(
        "Target FPS",
        ImGuiDataType_U32,
        &target_fps,
        &minimum_target,
        &maximum_target,
        "%u",
        ImGuiSliderFlags_AlwaysClamp)) {
    g_config_dirty = true;
}
ImGui::SameLine();
ImGui::TextDisabled("(?)");
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Fixed framerate expected for this game launch (60-500).\n"
        "GCLoader does not apply a frame cap. Configure the same cap in your driver or RTSS.\n"
        "Restart the game after changing this value.");
}
if (!gc::config::IsGameplayValidatedTargetFps(
        static_cast<std::uint32_t>(target_fps))) {
    ImGui::TextColored(
        ImVec4(1.0F, 0.75F, 0.2F, 1.0F),
        "This in-range value is formula-driven but not individually gameplay-validated.");
}
```

Include `gc::config::ValidateInputConfig(g_config).has_value()` in `configuration_valid` before enabling Save. Do not put `IntervalMode` in GUI labels or help text.

- [ ] **Step 6: Update the distributed sample, build, and verify GREEN**

Replace the sample field in `config.toml` with:

```toml
[experimental]
target_fps = 60
```

Run:

```powershell
cmake --build --preset msvc32-release --target ConfigFeatureTests ConfigGUI
ctest --test-dir build-msvc32-release --output-on-failure -R '^ConfigFeatureTests$'
rg -n "enable_120fps_timer_patches|120 FPS timer patches|IntervalMode" src/Config tools/ConfigGUI config.toml
```

Expected: ConfigFeatureTests passes; ConfigGUI builds; the final search has no matches.

- [ ] **Step 7: Commit the strict configuration contract**

```powershell
git add -- src/Config/TargetFps.h src/Config/config.h src/Config/config.cpp tests/Config/ConfigFeatureTests.cpp tools/ConfigGUI/Main.cpp config.toml
git commit -m "feat: configure fixed framerate target"
```

---
### Task 2: Add the Immutable Framerate Profile and Rational Conversions

**Files:**
- Create: `src/Patches/Framerate/FramerateProfile.h`
- Create: `src/Patches/Framerate/FramerateProfile.cpp`
- Create: `tests/Patches/Framerate/FramerateProfileTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**
- Consumes: `gc::config::IsTargetFpsInRange` and `gc::config::IsGameplayValidatedTargetFps` from Task 1.
- Produces: `gc::framerate::FramerateProfile::Create(std::uint32_t)`, immutable target-derived getters, `ScaleDurationFrames(std::int32_t)`, and `MapToAuthored60(std::uint32_t)`.

- [ ] **Step 1: Register and write the failing profile test**

Append this target to `tests/Patches/CMakeLists.txt`:

```cmake
add_executable(FramerateProfileTests
        Framerate/FramerateProfileTests.cpp)
target_link_libraries(FramerateProfileTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateProfileTests COMMAND FramerateProfileTests)
```

Create `tests/Patches/Framerate/FramerateProfileTests.cpp` with this complete test program:

```cpp
#include "Patches/Framerate/FramerateProfile.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool Near(float actual, float expected, float tolerance = 0.00001F) {
    return std::fabs(actual - expected) <= tolerance;
}

} // namespace

int main() {
    using gc::framerate::FramerateProfile;
    int failures = 0;

    failures += Expect(!FramerateProfile::Create(59), "reject 59");
    failures += Expect(!FramerateProfile::Create(501), "reject 501");

    constexpr std::uint32_t validated[]{60, 120, 144, 165, 240, 360};
    for (const auto target : validated) {
        const auto created = FramerateProfile::Create(target);
        failures += Expect(created.has_value(), "create validated profile");
        if (!created) {
            continue;
        }
        const auto& profile = created.value();
        failures += Expect(profile.target_fps() == target, "retain target");
        failures += Expect(
            profile.gameplay_validated(), "validated target marker");
        failures += Expect(
            Near(profile.frame_milliseconds(), 1000.0F / target),
            "frame milliseconds");
        failures += Expect(
            Near(profile.frame_seconds(), 1.0F / target),
            "frame seconds");
        failures += Expect(
            profile.two_second_frames() == target * 2,
            "two-second frames");
        failures += Expect(
            profile.palette_frame_cap() == target,
            "palette frame cap");
        failures += Expect(
            Near(profile.render_smoothing_step(), 4.0F * 60.0F / target),
            "render smoothing");
        failures += Expect(
            Near(profile.render_offset_decay_step(), 5.0F * 60.0F / target),
            "render decay");
    }

    const auto native = FramerateProfile::Create(60).value();
    failures += Expect(native.native_timing(), "60 is native");
    const auto formula_only = FramerateProfile::Create(200).value();
    failures += Expect(
        !formula_only.gameplay_validated(), "200 is warning-only");

    const auto target120 = FramerateProfile::Create(120).value();
    failures += Expect(
        target120.ScaleDurationFrames(16).value() == 32,
        "120 initial repeat");
    failures += Expect(
        target120.ScaleDurationFrames(8).value() == 16,
        "120 next repeat");

    const auto target144 = FramerateProfile::Create(144).value();
    failures += Expect(
        target144.ScaleDurationFrames(16).value() == 38,
        "144 initial repeat nearest");
    failures += Expect(
        target144.ScaleDurationFrames(8).value() == 19,
        "144 next repeat nearest");

    const auto half_up = FramerateProfile::Create(90).value();
    failures += Expect(
        half_up.ScaleDurationFrames(1).value() == 2,
        "positive half rounds up");
    failures += Expect(
        half_up.ScaleDurationFrames(0).value() == 0,
        "zero sentinel preserved");
    failures += Expect(
        half_up.ScaleDurationFrames(-1).value() == -1,
        "negative sentinel preserved");

    const auto overflow = FramerateProfile::Create(500).value()
        .ScaleDurationFrames(std::numeric_limits<std::int32_t>::max());
    failures += Expect(!overflow, "duration destination overflow rejected");

    for (std::uint32_t target = 60; target <= 500; ++target) {
        const auto profile = FramerateProfile::Create(target).value();
        std::uint32_t previous = 0;
        for (std::uint32_t frame = 0; frame <= target; ++frame) {
            const auto mapped = profile.MapToAuthored60(frame);
            failures += Expect(mapped.has_value(), "authored mapping succeeds");
            if (!mapped) {
                continue;
            }
            failures += Expect(mapped.value() >= previous, "mapping monotonic");
            failures += Expect(
                static_cast<std::uint64_t>(mapped.value()) * target <=
                    static_cast<std::uint64_t>(frame) * 60,
                "mapping never selects future frame");
            previous = mapped.value();
        }
        failures += Expect(
            profile.MapToAuthored60(target).value() == 60,
            "one-second boundary maps to 60");
        for (std::int32_t duration = 1; duration <= 120; ++duration) {
            const auto scaled = profile.ScaleDurationFrames(duration);
            failures += Expect(scaled.has_value(), "duration scaling succeeds");
            if (!scaled) {
                continue;
            }
            const double wall_clock_error = std::fabs(
                static_cast<double>(scaled.value()) / target -
                static_cast<double>(duration) / 60.0);
            failures += Expect(
                wall_clock_error <= 0.5 / target + 1.0e-12,
                "duration error is at most half a target frame");
        }
    }

    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run the profile test and verify RED**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target FramerateProfileTests
```

Expected: compilation fails because `FramerateProfile.h` is absent.

- [ ] **Step 3: Define the exact immutable profile interface**

Create `src/Patches/Framerate/FramerateProfile.h` with:

```cpp
#pragma once

#include <cstdint>
#include <expected>

namespace gc::framerate {

enum class FramerateProfileError {
    TargetOutOfRange,
    ArithmeticOverflow,
    DestinationOverflow,
};

class FramerateProfile {
public:
    [[nodiscard]] static std::expected<
        FramerateProfile,
        FramerateProfileError>
    Create(std::uint32_t target_fps) noexcept;

    [[nodiscard]] std::uint32_t target_fps() const noexcept {
        return target_fps_;
    }
    [[nodiscard]] float target_fps_float() const noexcept {
        return target_fps_float_;
    }
    [[nodiscard]] const float* target_fps_operand() const noexcept {
        return &target_fps_float_;
    }
    [[nodiscard]] bool native_timing() const noexcept {
        return target_fps_ == 60;
    }
    [[nodiscard]] bool gameplay_validated() const noexcept {
        return gameplay_validated_;
    }
    [[nodiscard]] float frame_milliseconds() const noexcept {
        return frame_milliseconds_;
    }
    [[nodiscard]] float frame_seconds() const noexcept {
        return frame_seconds_;
    }
    [[nodiscard]] float render_smoothing_step() const noexcept {
        return render_smoothing_step_;
    }
    [[nodiscard]] float render_offset_decay_step() const noexcept {
        return render_offset_decay_step_;
    }
    [[nodiscard]] std::uint32_t two_second_frames() const noexcept {
        return two_second_frames_;
    }
    [[nodiscard]] std::uint32_t palette_frame_cap() const noexcept {
        return palette_frame_cap_;
    }
    [[nodiscard]] float ScalePerFrameDelta(float value) const noexcept {
        return value * 60.0F / target_fps_float_;
    }

    [[nodiscard]] std::expected<std::int32_t, FramerateProfileError>
    ScaleDurationFrames(std::int32_t value) const noexcept;

    [[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
    MapToAuthored60(std::uint32_t value) const noexcept;

private:
    explicit FramerateProfile(std::uint32_t target_fps) noexcept;

    std::uint32_t target_fps_{};
    float target_fps_float_{};
    bool gameplay_validated_{};
    float frame_milliseconds_{};
    float frame_seconds_{};
    float render_smoothing_step_{};
    float render_offset_decay_step_{};
    std::uint32_t two_second_frames_{};
    std::uint32_t palette_frame_cap_{};
};

} // namespace gc::framerate
```

- [ ] **Step 4: Implement checked nearest-duration and floor-index math**

Create `src/Patches/Framerate/FramerateProfile.cpp` with:

```cpp
#include "Patches/Framerate/FramerateProfile.h"

#include "Config/TargetFps.h"

#include <limits>

namespace gc::framerate {

FramerateProfile::FramerateProfile(std::uint32_t target_fps) noexcept
    : target_fps_{target_fps},
      target_fps_float_{static_cast<float>(target_fps)},
      gameplay_validated_{
          gc::config::IsGameplayValidatedTargetFps(target_fps)},
      frame_milliseconds_{1000.0F / target_fps_float_},
      frame_seconds_{1.0F / target_fps_float_},
      render_smoothing_step_{4.0F * 60.0F / target_fps_float_},
      render_offset_decay_step_{5.0F * 60.0F / target_fps_float_},
      two_second_frames_{target_fps * 2},
      palette_frame_cap_{target_fps} {
}

std::expected<FramerateProfile, FramerateProfileError>
FramerateProfile::Create(std::uint32_t target_fps) noexcept {
    if (!gc::config::IsTargetFpsInRange(target_fps)) {
        return std::unexpected(FramerateProfileError::TargetOutOfRange);
    }
    return FramerateProfile{target_fps};
}

std::expected<std::int32_t, FramerateProfileError>
FramerateProfile::ScaleDurationFrames(std::int32_t value) const noexcept {
    if (value <= 0) {
        return value;
    }

    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (static_cast<std::int64_t>(value) >
        maximum / static_cast<std::int64_t>(target_fps_)) {
        return std::unexpected(FramerateProfileError::ArithmeticOverflow);
    }

    const auto product = static_cast<std::int64_t>(value) * target_fps_;
    const auto rounded = (product + 30) / 60;
    if (rounded > std::numeric_limits<std::int32_t>::max()) {
        return std::unexpected(FramerateProfileError::DestinationOverflow);
    }
    return static_cast<std::int32_t>(rounded);
}

std::expected<std::uint32_t, FramerateProfileError>
FramerateProfile::MapToAuthored60(std::uint32_t value) const noexcept {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (static_cast<std::uint64_t>(value) > maximum / 60) {
        return std::unexpected(FramerateProfileError::ArithmeticOverflow);
    }
    const auto mapped =
        static_cast<std::uint64_t>(value) * 60 / target_fps_;
    if (mapped > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(FramerateProfileError::DestinationOverflow);
    }
    return static_cast<std::uint32_t>(mapped);
}

} // namespace gc::framerate
```

Add `Framerate/FramerateProfile.cpp` to `gc_runtime_patches` in `src/Patches/CMakeLists.txt`.

- [ ] **Step 5: Build and verify GREEN**

```powershell
cmake --build --preset msvc32-release --target FramerateProfileTests
ctest --test-dir build-msvc32-release --output-on-failure -R '^FramerateProfileTests$'
```

Expected: the focused target builds and the test passes across all 441 accepted targets.

- [ ] **Step 6: Commit the pure profile**

```powershell
git add -- src/Patches/Framerate/FramerateProfile.h src/Patches/Framerate/FramerateProfile.cpp tests/Patches/Framerate/FramerateProfileTests.cpp src/Patches/CMakeLists.txt tests/Patches/CMakeLists.txt
git commit -m "feat: add immutable framerate profile"
```

---

### Task 3: Add the Fixed-Storage Startup Cadence Monitor

**Files:**
- Create: `src/Patches/Framerate/FramerateMonitor.h`
- Create: `src/Patches/Framerate/FramerateMonitor.cpp`
- Create: `tests/Patches/Framerate/FramerateMonitorTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**
- Consumes: A validated target FPS, a positive QPC frequency, and one monotonically increasing QPC timestamp per outer frame.
- Produces: `FramerateMonitor::Create`, `FramerateMonitor::Observe`, fixed-capacity sample storage, and `FramerateObservation` decisions with measured FPS, relative error, sample count, streaks, and overflow state.

- [ ] **Step 1: Register the monitor target and write RED state-machine tests**

Append to `tests/Patches/CMakeLists.txt`:

```cmake
add_executable(FramerateMonitorTests
        Framerate/FramerateMonitorTests.cpp)
target_link_libraries(FramerateMonitorTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateMonitorTests COMMAND FramerateMonitorTests)
```

Create `tests/Patches/Framerate/FramerateMonitorTests.cpp`. Use a purpose-built QPC frequency divisible by 144, 144x0.97, and 144x1.03 so the inclusive tolerance edges are represented exactly. The complete test driver must contain these helpers and cases:

```cpp
#include "Patches/Framerate/FramerateMonitor.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>

namespace {

constexpr std::int64_t kFrequency = 1'438'704'000;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::optional<gc::framerate::FramerateObservation> FeedSeconds(
    gc::framerate::FramerateMonitor& monitor,
    std::int64_t& now,
    double fps,
    double seconds,
    std::optional<std::size_t> one_stall_after = std::nullopt) {
    const auto step = static_cast<std::int64_t>(
        std::llround(static_cast<double>(kFrequency) / fps));
    const auto frames = static_cast<std::size_t>(
        std::ceil(fps * seconds)) + 2;
    std::optional<gc::framerate::FramerateObservation> last;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        now += step;
        if (one_stall_after && frame == *one_stall_after) {
            now += kFrequency / 2;
        }
        if (auto observation = monitor.Observe(now)) {
            last = observation;
        }
    }
    return last;
}

gc::framerate::FramerateObservation ReachDecision(
    std::uint32_t target,
    double measured,
    bool add_stall = false) {
    auto monitor = gc::framerate::FramerateMonitor::Create(
        target, kFrequency).value();
    std::int64_t now = 0;
    monitor.Observe(now);
    FeedSeconds(monitor, now, measured, 5.1);
    std::optional<gc::framerate::FramerateObservation> result;
    for (int window = 0; window < 3; ++window) {
        result = FeedSeconds(
            monitor,
            now,
            measured,
            2.1,
            add_stall && window == 1
                ? std::optional<std::size_t>{10}
                : std::nullopt);
    }
    return *result;
}

} // namespace

int main() {
    using gc::framerate::FramerateDecision;
    using gc::framerate::FramerateMonitor;
    int failures = 0;

    failures += Expect(!FramerateMonitor::Create(59, kFrequency),
        "reject invalid target");
    failures += Expect(!FramerateMonitor::Create(120, 0),
        "reject invalid frequency");
    failures += Expect(
        !FramerateMonitor::Create(
            120, std::numeric_limits<std::int64_t>::max()),
        "reject QPC duration multiplication overflow");

    auto boundaries = FramerateMonitor::Create(120, 120'000).value();
    std::int64_t boundary_now = 0;
    failures += Expect(!boundaries.Observe(boundary_now),
        "first timestamp only establishes epoch");
    for (int frame = 0; frame < 599; ++frame) {
        boundary_now += 1'000;
        failures += Expect(!boundaries.Observe(boundary_now),
            "no decision before five-second warm-up");
    }
    boundary_now = 600'000;
    failures += Expect(!boundaries.Observe(boundary_now),
        "five-second boundary starts collection");
    for (int frame = 0; frame < 239; ++frame) {
        boundary_now += 1'000;
        failures += Expect(!boundaries.Observe(boundary_now),
            "no result before two-second window boundary");
    }
    boundary_now = 840'000;
    const auto first_window = boundaries.Observe(boundary_now);
    failures += Expect(
        first_window &&
            first_window->decision == FramerateDecision::WindowMatch,
        "two-second boundary publishes first matching window");

    for (const std::uint32_t rate : {60U, 120U, 144U, 165U, 240U, 360U, 500U}) {
        const auto result = ReachDecision(rate, static_cast<double>(rate));
        failures += Expect(result.decision == FramerateDecision::Validated,
            "three matching windows validate");
        failures += Expect(result.matching_streak == 3,
            "matching streak reaches three");
        failures += Expect(std::fabs(result.relative_error) < 0.0001,
            "exact cadence error near zero");
    }

    const auto mismatch = ReachDecision(144, 120.0);
    failures += Expect(mismatch.decision == FramerateDecision::FatalMismatch,
        "144 versus 120 aborts");
    failures += Expect(mismatch.mismatching_streak == 3,
        "mismatch streak reaches three");

    const auto low_edge = ReachDecision(144, 144.0 * 0.97);
    const auto high_edge = ReachDecision(144, 144.0 * 1.03);
    failures += Expect(low_edge.decision == FramerateDecision::Validated,
        "minus-three-percent edge matches");
    failures += Expect(high_edge.decision == FramerateDecision::Validated,
        "plus-three-percent edge matches");
    failures += Expect(
        ReachDecision(144, 144.0 * 0.969).decision ==
            FramerateDecision::FatalMismatch,
        "outside low tolerance aborts");
    failures += Expect(
        ReachDecision(144, 144.0 * 1.031).decision ==
            FramerateDecision::FatalMismatch,
        "outside high tolerance aborts");

    const auto stalled = ReachDecision(240, 240.0, true);
    failures += Expect(stalled.decision == FramerateDecision::Validated,
        "single long interval does not dominate median");

    const auto overflow = ReachDecision(500, 2'000.0);
    failures += Expect(
        overflow.decision == FramerateDecision::FatalMismatch &&
            overflow.storage_overflowed,
        "uncapped sample overflow is mismatch evidence");

    auto streaks = FramerateMonitor::Create(144, kFrequency).value();
    std::int64_t now = 0;
    streaks.Observe(now);
    FeedSeconds(streaks, now, 144.0, 5.1);
    FeedSeconds(streaks, now, 144.0, 2.1);
    auto reset_to_mismatch = FeedSeconds(streaks, now, 120.0, 2.1).value();
    failures += Expect(
        reset_to_mismatch.matching_streak == 0 &&
            reset_to_mismatch.mismatching_streak == 1,
        "mismatch resets matching streak");
    auto reset_to_match = FeedSeconds(streaks, now, 144.0, 2.1).value();
    failures += Expect(
        reset_to_match.matching_streak == 1 &&
            reset_to_match.mismatching_streak == 0,
        "match resets mismatch streak");

    auto disabled = FramerateMonitor::Create(120, kFrequency).value();
    now = 0;
    disabled.Observe(now);
    FeedSeconds(disabled, now, 120.0, 5.1);
    FeedSeconds(disabled, now, 120.0, 6.3);
    failures += Expect(!disabled.active(), "validated monitor disables itself");
    failures += Expect(!disabled.Observe(now + kFrequency),
        "disabled monitor publishes nothing later");

    auto invalid_clock = FramerateMonitor::Create(120, kFrequency).value();
    invalid_clock.Observe(100);
    const auto clock_result = invalid_clock.Observe(99);
    failures += Expect(
        clock_result &&
            clock_result->decision == FramerateDecision::FatalClock,
        "nonmonotonic clock fails closed");

    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Build the monitor test and verify RED**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target FramerateMonitorTests
```

Expected: compilation fails because `FramerateMonitor.h` is absent.

- [ ] **Step 3: Define the allocation-free monitor contract**

Create `src/Patches/Framerate/FramerateMonitor.h` with:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace gc::framerate {

inline constexpr double kFramerateWarmupSeconds = 5.0;
inline constexpr double kFramerateWindowSeconds = 2.0;
inline constexpr double kFramerateTolerance = 0.03;
inline constexpr std::uint32_t kFramerateRequiredStreak = 3;
// ceil(2 seconds * 500 FPS * 1.03) plus two boundary intervals.
inline constexpr std::size_t kMaximumIntervalsPerWindow = 1032;

enum class FramerateMonitorError {
    TargetOutOfRange,
    InvalidQpcFrequency,
    QpcRangeOverflow,
};

enum class FramerateDecision {
    WindowMatch,
    WindowMismatch,
    Validated,
    FatalMismatch,
    FatalClock,
};

struct FramerateObservation {
    FramerateDecision decision{};
    std::uint32_t target_fps{};
    double measured_fps{};
    double relative_error{};
    std::size_t interval_count{};
    std::uint32_t matching_streak{};
    std::uint32_t mismatching_streak{};
    bool storage_overflowed{};
};

class FramerateMonitor {
public:
    [[nodiscard]] static std::expected<
        FramerateMonitor,
        FramerateMonitorError>
    Create(std::uint32_t target_fps, std::int64_t qpc_frequency) noexcept;

    [[nodiscard]] std::optional<FramerateObservation> Observe(
        std::int64_t qpc_timestamp) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    FramerateMonitor(
        std::uint32_t target_fps,
        std::int64_t qpc_frequency) noexcept;

    [[nodiscard]] FramerateObservation FinishWindow() noexcept;
    [[nodiscard]] FramerateObservation FatalClock() noexcept;

    std::uint32_t target_fps_{};
    std::int64_t qpc_frequency_{};
    std::int64_t warmup_ticks_{};
    std::int64_t window_ticks_{};
    std::int64_t first_timestamp_{};
    std::int64_t previous_timestamp_{};
    std::int64_t window_start_{};
    std::array<std::int64_t, kMaximumIntervalsPerWindow> intervals_{};
    std::size_t interval_count_{};
    std::uint32_t matching_streak_{};
    std::uint32_t mismatching_streak_{};
    bool active_{true};
    bool started_{};
    bool warming_up_{true};
    bool storage_overflowed_{};
};

} // namespace gc::framerate
```

- [ ] **Step 4: Implement warm-up, median windows, streaks, and one-shot shutdown**

Create `src/Patches/Framerate/FramerateMonitor.cpp`. The core implementation must be:

```cpp
#include "Patches/Framerate/FramerateMonitor.h"

#include "Config/TargetFps.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gc::framerate {

FramerateMonitor::FramerateMonitor(
    std::uint32_t target_fps,
    std::int64_t qpc_frequency) noexcept
    : target_fps_{target_fps},
      qpc_frequency_{qpc_frequency},
      warmup_ticks_{qpc_frequency * 5},
      window_ticks_{qpc_frequency * 2} {
}

std::expected<FramerateMonitor, FramerateMonitorError>
FramerateMonitor::Create(
    std::uint32_t target_fps,
    std::int64_t qpc_frequency) noexcept {
    if (!gc::config::IsTargetFpsInRange(target_fps)) {
        return std::unexpected(FramerateMonitorError::TargetOutOfRange);
    }
    if (qpc_frequency <= 0) {
        return std::unexpected(FramerateMonitorError::InvalidQpcFrequency);
    }
    if (qpc_frequency > std::numeric_limits<std::int64_t>::max() / 5) {
        return std::unexpected(FramerateMonitorError::QpcRangeOverflow);
    }
    return FramerateMonitor{target_fps, qpc_frequency};
}

std::optional<FramerateObservation> FramerateMonitor::Observe(
    std::int64_t qpc_timestamp) noexcept {
    if (!active_) {
        return std::nullopt;
    }
    if (!started_) {
        started_ = true;
        first_timestamp_ = qpc_timestamp;
        previous_timestamp_ = qpc_timestamp;
        return std::nullopt;
    }
    if (qpc_timestamp <= previous_timestamp_) {
        return FatalClock();
    }

    if (warming_up_) {
        previous_timestamp_ = qpc_timestamp;
        if (qpc_timestamp - first_timestamp_ < warmup_ticks_) {
            return std::nullopt;
        }
        warming_up_ = false;
        window_start_ = qpc_timestamp;
        return std::nullopt;
    }

    const auto interval = qpc_timestamp - previous_timestamp_;
    previous_timestamp_ = qpc_timestamp;
    if (interval_count_ < intervals_.size()) {
        intervals_[interval_count_++] = interval;
    } else {
        storage_overflowed_ = true;
    }

    if (qpc_timestamp - window_start_ < window_ticks_) {
        return std::nullopt;
    }

    auto result = FinishWindow();
    window_start_ = qpc_timestamp;
    interval_count_ = 0;
    storage_overflowed_ = false;
    return result;
}

FramerateObservation FramerateMonitor::FinishWindow() noexcept {
    double measured_fps = 0.0;
    if (interval_count_ != 0) {
        std::sort(intervals_.begin(), intervals_.begin() + interval_count_);
        double median_ticks = 0.0;
        const auto middle = interval_count_ / 2;
        if ((interval_count_ & 1U) != 0) {
            median_ticks = static_cast<double>(intervals_[middle]);
        } else {
            median_ticks =
                (static_cast<double>(intervals_[middle - 1]) +
                 static_cast<double>(intervals_[middle])) /
                2.0;
        }
        if (median_ticks > 0.0) {
            measured_fps =
                static_cast<double>(qpc_frequency_) / median_ticks;
        }
    }

    const double relative_error = std::fabs(
        measured_fps - static_cast<double>(target_fps_)) /
        static_cast<double>(target_fps_);
    const bool matches =
        !storage_overflowed_ && interval_count_ != 0 &&
        relative_error <= kFramerateTolerance;

    FramerateDecision decision{};
    if (matches) {
        ++matching_streak_;
        mismatching_streak_ = 0;
        decision = matching_streak_ >= kFramerateRequiredStreak
            ? FramerateDecision::Validated
            : FramerateDecision::WindowMatch;
    } else {
        ++mismatching_streak_;
        matching_streak_ = 0;
        decision = mismatching_streak_ >= kFramerateRequiredStreak
            ? FramerateDecision::FatalMismatch
            : FramerateDecision::WindowMismatch;
    }

    if (decision == FramerateDecision::Validated ||
        decision == FramerateDecision::FatalMismatch) {
        active_ = false;
    }

    return {
        .decision = decision,
        .target_fps = target_fps_,
        .measured_fps = measured_fps,
        .relative_error = relative_error,
        .interval_count = interval_count_,
        .matching_streak = matching_streak_,
        .mismatching_streak = mismatching_streak_,
        .storage_overflowed = storage_overflowed_,
    };
}

FramerateObservation FramerateMonitor::FatalClock() noexcept {
    active_ = false;
    return {
        .decision = FramerateDecision::FatalClock,
        .target_fps = target_fps_,
        .relative_error = 1.0,
        .mismatching_streak = kFramerateRequiredStreak,
    };
}

} // namespace gc::framerate
```

Add `Framerate/FramerateMonitor.cpp` to `gc_runtime_patches`.

- [ ] **Step 5: Run focused tests and verify GREEN**

```powershell
cmake --build --preset msvc32-release --target FramerateMonitorTests
ctest --test-dir build-msvc32-release --output-on-failure -R '^FramerateMonitorTests$'
```

Expected: all exact, edge, mismatch, overflow, streak-reset, post-success, stall, and invalid-clock cases pass.

- [ ] **Step 6: Commit the monitor**

```powershell
git add -- src/Patches/Framerate/FramerateMonitor.h src/Patches/Framerate/FramerateMonitor.cpp tests/Patches/Framerate/FramerateMonitorTests.cpp src/Patches/CMakeLists.txt tests/Patches/CMakeLists.txt
git commit -m "feat: add startup framerate monitor"
```

---

### Task 4: Add One-Shot Mismatch and Initialization-Failure Diagnostics

**Files:**
- Create: `src/Patches/Framerate/FramerateDiagnostics.h`
- Create: `src/Patches/Framerate/FramerateDiagnostics.cpp`
- Create: `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**
- Consumes: `FramerateProfile`, `FramerateObservation`, an atomic publication latch, and injected platform actions.
- Produces: `ReportFramerateStartup`, `ShouldSuggestIntervalModeOne`, `ReportFramerateMismatch`, `ReportFramerateClockFailure`, `ReportFramerateRuntimeFailure`, and `ReportFramerateInitializationFailure`, with production log/modal/terminate/fail-fast actions kept outside the pure monitor.

- [ ] **Step 1: Write failing tests for the exact recovery-message policy**

Append this test target:

```cmake
add_executable(FramerateDiagnosticsTests
        Framerate/FramerateDiagnosticsTests.cpp)
target_link_libraries(FramerateDiagnosticsTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateDiagnosticsTests COMMAND FramerateDiagnosticsTests)
```

Create `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp` with these includes, assertion helper, and fake actions that append strings and exit codes to one `DiagnosticState`:

```cpp
#include "Patches/Framerate/FramerateDiagnostics.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

struct DiagnosticState {
    std::vector<std::string> infos;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::vector<std::string> messages;
    std::vector<DWORD> termination_codes;
    std::vector<std::string> fatal_sequence;
    std::uint32_t fail_fast_calls{};
};

DiagnosticState* g_state = nullptr;

void LogInfo(const char* text) { g_state->infos.emplace_back(text); }
void LogWarning(const char* text) { g_state->warnings.emplace_back(text); }
void LogError(const char* text) {
    g_state->errors.emplace_back(text);
    g_state->fatal_sequence.emplace_back("log");
}
void ShowError(const char* text) {
    g_state->messages.emplace_back(text);
    g_state->fatal_sequence.emplace_back("modal");
}
void Terminate(DWORD code) {
    g_state->termination_codes.push_back(code);
    g_state->fatal_sequence.emplace_back("terminate");
}
void FailFast() {
    ++g_state->fail_fast_calls;
    g_state->fatal_sequence.emplace_back("fail_fast");
}

gc::framerate::FrameratePlatformActions Actions() {
    return {LogInfo, LogWarning, LogError, ShowError, Terminate, FailFast};
}

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}
```

In `main()`, construct fatal observations and require:

```cpp
using namespace gc::framerate;
int failures = 0;

DiagnosticState validated_startup;
g_state = &validated_startup;
ReportFramerateStartup(
    FramerateProfile::Create(144).value(), Actions());
failures += Expect(
    validated_startup.infos.size() == 1 &&
        validated_startup.warnings.empty(),
    "validated target logs no support warning");

DiagnosticState formula_startup;
g_state = &formula_startup;
ReportFramerateStartup(
    FramerateProfile::Create(200).value(), Actions());
failures += Expect(
    formula_startup.infos.size() == 1 &&
        formula_startup.warnings.size() == 1 &&
        Contains(formula_startup.warnings[0], "not individually gameplay-validated"),
    "formula-only target logs exactly one support warning");

FramerateObservation measured120{
    .decision = FramerateDecision::FatalMismatch,
    .target_fps = 144,
    .measured_fps = 120.0,
    .relative_error = 1.0 / 6.0,
    .interval_count = 240,
    .mismatching_streak = 3,
};
FramerateObservation measured60 = measured120;
measured60.measured_fps = 60.0;
measured60.relative_error = 7.0 / 12.0;

failures += Expect(
    !ShouldSuggestIntervalModeOne(144, 120.0),
    "144 versus 120 omits IntervalMode");
failures += Expect(
    ShouldSuggestIntervalModeOne(144, 60.0),
    "144 versus 60 includes IntervalMode");
failures += Expect(
    !ShouldSuggestIntervalModeOne(60, 60.0),
    "native target never receives high-target hint");
failures += Expect(
    ShouldSuggestIntervalModeOne(144, 61.8) &&
        !ShouldSuggestIntervalModeOne(144, 61.81),
    "60-FPS hint uses inclusive three-percent tolerance");

DiagnosticState ordinary;
g_state = &ordinary;
std::atomic_bool ordinary_latch{false};
ReportFramerateMismatch(measured120, ordinary_latch, Actions());
failures += Expect(
    ordinary.errors.size() == 1 &&
        Contains(ordinary.errors[0], "target_fps=144") &&
        Contains(ordinary.errors[0], "measured_fps=120") &&
        Contains(ordinary.errors[0], "relative_error=") &&
        Contains(ordinary.errors[0], "interval_count=240") &&
        Contains(ordinary.errors[0], "failed_windows=3"),
    "fatal log contains required measurements");
failures += Expect(
    ordinary.messages.size() == 1 &&
        !Contains(ordinary.messages[0], "IntervalMode"),
    "ordinary mismatch modal omits IntervalMode");
failures += Expect(
    ordinary.termination_codes == std::vector<DWORD>{ERROR_INVALID_DATA} &&
        ordinary.fail_fast_calls == 1 &&
        ordinary.fatal_sequence == std::vector<std::string>{
            "log", "modal", "terminate", "fail_fast"},
    "fatal mismatch terminates then fail-fasts");

ReportFramerateMismatch(measured120, ordinary_latch, Actions());
failures += Expect(
    ordinary.messages.size() == 1 && ordinary.errors.size() == 1,
    "atomic latch suppresses duplicate publication");

DiagnosticState sixty;
g_state = &sixty;
std::atomic_bool sixty_latch{false};
ReportFramerateMismatch(measured60, sixty_latch, Actions());
failures += Expect(
    sixty.messages.size() == 1 &&
        Contains(sixty.messages[0], "IntervalMode = 1"),
    "approximately-60 mismatch contains conditional hint");

DiagnosticState initialization;
g_state = &initialization;
std::atomic_bool initialization_latch{false};
ReportFramerateInitializationFailure(
    "hook install failed at palette compare; rollback_complete=true",
    initialization_latch,
    Actions());
failures += Expect(
    initialization.errors.size() == 1 &&
        Contains(initialization.errors[0], "rollback_complete=true") &&
        initialization.messages.size() == 1 &&
        initialization.termination_codes ==
            std::vector<DWORD>{ERROR_DLL_INIT_FAILED} &&
        initialization.fail_fast_calls == 1,
    "initialization failure logs, prompts, terminates, and fail-fasts");

DiagnosticState runtime;
g_state = &runtime;
std::atomic_bool runtime_latch{false};
ReportFramerateRuntimeFailure(
    "IFBL wait scaling overflow",
    runtime_latch,
    Actions());
failures += Expect(
    runtime.errors.size() == 1 &&
        Contains(runtime.errors[0], "IFBL wait scaling overflow") &&
        runtime.messages.size() == 1 &&
        runtime.termination_codes ==
            std::vector<DWORD>{ERROR_INVALID_DATA} &&
        runtime.fail_fast_calls == 1,
    "runtime conversion failure is fatal and one-shot");

return failures == 0 ? 0 : 1;
```

- [ ] **Step 2: Build the diagnostics test and verify RED**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target FramerateDiagnosticsTests
```

Expected: compilation fails because the diagnostics interface is absent.

- [ ] **Step 3: Define the injected platform boundary**

Create `src/Patches/Framerate/FramerateDiagnostics.h`:

```cpp
#pragma once

#include "Patches/Framerate/FramerateMonitor.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace gc::framerate {

struct FrameratePlatformActions {
    void (*log_info)(const char*);
    void (*log_warning)(const char*);
    void (*log_error)(const char*);
    void (*show_error)(const char*);
    void (*terminate_process)(DWORD);
    void (*fail_fast)();
};

[[nodiscard]] FrameratePlatformActions ProductionFrameratePlatformActions()
    noexcept;

void ReportFramerateStartup(
    const FramerateProfile& profile,
    FrameratePlatformActions actions) noexcept;

[[nodiscard]] bool ShouldSuggestIntervalModeOne(
    std::uint32_t target_fps,
    double measured_fps) noexcept;

void ReportFramerateMismatch(
    const FramerateObservation& observation,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

void ReportFramerateClockFailure(
    std::uint32_t target_fps,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

void ReportFramerateRuntimeFailure(
    std::string_view detail,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

void ReportFramerateInitializationFailure(
    std::string_view detail,
    std::atomic_bool& publication_latch,
    FrameratePlatformActions actions) noexcept;

} // namespace gc::framerate
```

- [ ] **Step 4: Implement exact conditional guidance and fatal ordering**

In `FramerateDiagnostics.cpp`, make the hint predicate independent of the configured target tolerance:

```cpp
bool ShouldSuggestIntervalModeOne(
    std::uint32_t target_fps,
    double measured_fps) noexcept {
    if (target_fps <= 60 || !std::isfinite(measured_fps)) {
        return false;
    }
    return std::fabs(measured_fps - 60.0) / 60.0 <= 0.03;
}
```

Build the mismatch log and modal with these exact fields and recovery text:

```cpp
std::ostringstream log;
log << "FrameratePatch: external cap validation failed"
    << " target_fps=" << observation.target_fps
    << " measured_fps=" << observation.measured_fps
    << " relative_error=" << observation.relative_error
    << " interval_count=" << observation.interval_count
    << " failed_windows=" << observation.mismatching_streak
    << " storage_overflowed="
    << (observation.storage_overflowed ? "true" : "false");

std::ostringstream modal;
modal << "GCLoader measured " << observation.measured_fps
      << " FPS, but target_fps is " << observation.target_fps << ".\n\n"
      << "GCLoader does not apply a frame cap. Configure your driver or RTSS "
         "cap to exactly match target_fps, ensure the system can sustain it, "
         "then restart the game.";
if (ShouldSuggestIntervalModeOne(
        observation.target_fps, observation.measured_fps)) {
    modal << "\n\nThe game appears to be held near its built-in 60 FPS limit. "
             "Set IntervalMode = 1, keep the external cap enabled, and restart.";
}
```

`ReportFramerateStartup` emits one info line containing target, frame milliseconds/seconds, native/transformed mode, countdown, palette cap, smoothing, decay, `warmup_seconds=5`, `window_seconds=2`, `tolerance_percent=3`, and `required_streak=3`. It calls `log_warning` exactly once only when `profile.gameplay_validated()` is false; build that warning with `stream << "FrameratePatch: target_fps=" << profile.target_fps() << " is formula-driven and has not been individually gameplay-validated"`.

Build the non-mismatch diagnostics exactly as follows:

```cpp
std::ostringstream clock_log;
clock_log << "FrameratePatch: QPC cadence clock failed target_fps="
          << target_fps;
constexpr std::string_view clock_modal =
    "GCLoader could not measure the external frame cap because the "
    "high-resolution clock failed. Restart the game; if the error repeats, "
    "keep target_fps at 60 and report the loader log.";

std::ostringstream runtime_log;
runtime_log << "FrameratePatch: runtime timing conversion failed detail="
            << detail;
constexpr std::string_view runtime_modal =
    "GCLoader encountered an unsafe runtime framerate conversion and stopped "
    "the game to avoid mixed timing domains. Restart the game and report the "
    "loader log.";

std::ostringstream initialization_log;
initialization_log << "FrameratePatch: initialization failed detail="
                   << detail;
constexpr std::string_view initialization_modal =
    "GCLoader could not install the complete framerate patch set and stopped "
    "the game. The loader log contains the failed stage and rollback status.";
```

Each fatal reporter must claim the latch with `compare_exchange_strong`, then call actions in this order: error log, one modal, `terminate_process`, `fail_fast`. Catch exceptions around each injected action so the next fallback still runs. Use `ERROR_INVALID_DATA` for cadence, clock, and runtime-conversion failures and `ERROR_DLL_INIT_FAILED` for initialization failure. Production actions must call `PLOG_*`, `MessageBoxA`, `TerminateProcess(GetCurrentProcess(), code)`, and finally `RaiseFailFastException` followed by `std::abort()`.

- [ ] **Step 5: Build and verify GREEN**

```powershell
cmake --build --preset msvc32-release --target FramerateDiagnosticsTests
ctest --test-dir build-msvc32-release --output-on-failure -R '^FramerateDiagnosticsTests$'
```

Expected: target-144/measured-120 omits the hint; target-144/measured-near-60 includes it; target 60 omits it; every fatal path logs before prompting and invokes termination before fail-fast exactly once.

- [ ] **Step 6: Commit fatal diagnostics**

```powershell
git add -- src/Patches/Framerate/FramerateDiagnostics.h src/Patches/Framerate/FramerateDiagnostics.cpp tests/Patches/Framerate/FramerateDiagnosticsTests.cpp src/Patches/CMakeLists.txt tests/Patches/CMakeLists.txt
git commit -m "feat: add framerate failure diagnostics"
```

---

### Task 5: Add a Fixed-Capacity Patch and Hook Transaction

**Files:**
- Create: `src/Patches/Framerate/FrameratePatchTransaction.h`
- Create: `src/Patches/Framerate/FrameratePatchTransaction.cpp`
- Create: `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**
- Consumes: Owned byte patterns, an injected memory API, and hook install/reset operations whose contexts outlive the transaction.
- Produces: `BytePattern`, `CheckedWrite`, `HookOperation`, `FrameratePatchTransaction::Install`, reverse-order rollback, and `FramerateInstallError` with stage, operation name/index, and rollback completeness.

- [ ] **Step 1: Register the target and write synthetic-memory rollback tests**

Append:

```cmake
add_executable(FrameratePatchTransactionTests
        Framerate/FrameratePatchTransactionTests.cpp)
target_link_libraries(FrameratePatchTransactionTests PRIVATE gc_runtime_patches)
add_test(NAME FrameratePatchTransactionTests COMMAND FrameratePatchTransactionTests)
```

Create `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp` around this deterministic fake backend:

```cpp
#include "Patches/Framerate/FrameratePatchTransaction.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>
#include <vector>

using namespace gc::framerate;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

BytePattern Pattern(std::initializer_list<std::uint8_t> values) {
    BytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::transform(
        values.begin(), values.end(), pattern.bytes.begin(),
        [](std::uint8_t value) { return static_cast<std::byte>(value); });
    return pattern;
}

struct FakeMemory {
    std::array<std::byte, 128> bytes{};
    bool fail_read{};
    int fail_write_call{-1};
    int write_calls{};
};

FakeMemory* g_memory = nullptr;

bool FakeRead(
    std::uintptr_t address,
    std::span<std::byte> destination) noexcept {
    if (g_memory->fail_read ||
        address + destination.size() > g_memory->bytes.size()) {
        return false;
    }
    std::copy_n(
        g_memory->bytes.begin() + address,
        destination.size(),
        destination.begin());
    return true;
}

bool FakeWrite(
    std::uintptr_t address,
    std::span<const std::byte> source) noexcept {
    const int call = g_memory->write_calls++;
    if (call == g_memory->fail_write_call ||
        address + source.size() > g_memory->bytes.size()) {
        return false;
    }
    std::copy(source.begin(), source.end(), g_memory->bytes.begin() + address);
    return true;
}

struct FakeHook {
    int install_call{};
    int fail_on_call{-1};
    std::vector<std::size_t> installed;
    std::vector<std::size_t> reset;
};

struct FakeHookContext {
    FakeHook* state{};
    std::size_t index{};
};

bool InstallFakeHook(void* opaque) noexcept {
    auto& context = *static_cast<FakeHookContext*>(opaque);
    const int call = context.state->install_call++;
    if (call == context.state->fail_on_call) {
        return false;
    }
    context.state->installed.push_back(context.index);
    return true;
}

void ResetFakeHook(void* opaque) noexcept {
    auto& context = *static_cast<FakeHookContext*>(opaque);
    context.state->reset.push_back(context.index);
}

struct Fixture {
    FakeMemory memory{};
    std::array<std::byte, 128> original_bytes{};
    FakeHook hook_state{};
    std::array<FakeHookContext, 4> hook_contexts{};
    std::array<CheckedWrite, 3> writes{};
    std::array<HookOperation, 4> hooks{};

    Fixture() {
        writes = {
            CheckedWrite{8, Pattern({0x01, 0x02}), Pattern({0xA1, 0xA2}), "write0"},
            CheckedWrite{16, Pattern({0x03, 0x04}), Pattern({0xA3, 0xA4}), "write1"},
            CheckedWrite{24, Pattern({0x05, 0x06}), Pattern({0xA5, 0xA6}), "write2"},
        };
        for (const auto& write : writes) {
            std::copy(
                write.expected.view().begin(),
                write.expected.view().end(),
                memory.bytes.begin() + write.address);
        }

        for (std::size_t index = 0; index < hooks.size(); ++index) {
            const auto address = 40 + index * 8;
            BytePattern expected{};
            expected.size = 1;
            expected.bytes[0] = static_cast<std::byte>(0x70 + index);
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

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

Fixture MakeFixture() { return Fixture{}; }

std::vector<std::size_t> ReverseIndices(std::size_t count) {
    std::vector<std::size_t> result;
    while (count != 0) {
        result.push_back(--count);
    }
    return result;
}
```

Use three direct writes and four hooks. Load every original pattern into the fake image before calling `Install`. Test all of these loops:

```cpp
int main() {
int failures = 0;

for (int failed_write = 0; failed_write < 3; ++failed_write) {
    auto fixture = MakeFixture();
    fixture.memory.fail_write_call = failed_write;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(!result, "write failure rejected");
    failures += Expect(
        fixture.memory.bytes == fixture.original_bytes,
        "write failure restores complete image");
    failures += Expect(
        result.error().rollback_complete,
        "write failure rollback verified");
}

for (int failed_hook = 0; failed_hook < 4; ++failed_hook) {
    auto fixture = MakeFixture();
    fixture.hook_state.fail_on_call = failed_hook;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(!result, "hook failure rejected");
    failures += Expect(
        fixture.memory.bytes == fixture.original_bytes,
        "hook failure restores every direct patch");
    const std::vector<std::size_t> expected_reset =
        ReverseIndices(static_cast<std::size_t>(failed_hook + 1));
    failures += Expect(
        fixture.hook_state.reset == expected_reset,
        "hook rollback is reverse ordered and resets failed hook defensively");
    failures += Expect(
        result.error().rollback_complete,
        "hook failure rollback verified");
}

{
    auto fixture = MakeFixture();
    fixture.memory.bytes[8] = std::byte{0xFF};
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        !result &&
            result.error().stage ==
                FramerateInstallStage::PreflightMismatch &&
            fixture.memory.write_calls == 0 &&
            fixture.hook_state.installed.empty(),
        "preflight mismatch mutates nothing");
}

{
    auto fixture = MakeFixture();
    fixture.memory.fail_read = true;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        !result &&
            result.error().stage == FramerateInstallStage::PreflightRead,
        "preflight read failure is distinct");
}

{
    auto fixture = MakeFixture();
    g_memory = &fixture.memory;
    std::array<CheckedWrite, kMaximumFramerateWrites + 1> too_many{};
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(
        too_many, std::span<const HookOperation>{});
    failures += Expect(
        !result && result.error().stage == FramerateInstallStage::Capacity,
        "over-capacity plan rejected before descriptor access");
}

{
    auto fixture = MakeFixture();
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        result.has_value() && transaction.committed() &&
            fixture.hook_state.installed ==
                std::vector<std::size_t>{0, 1, 2, 3} &&
            fixture.memory.bytes != fixture.original_bytes,
        "successful transaction retains every write and hook");
}

{
    auto fixture = MakeFixture();
    fixture.hook_state.fail_on_call = 0;
    fixture.memory.fail_write_call = 3;
    g_memory = &fixture.memory;
    FrameratePatchTransaction transaction({FakeRead, FakeWrite});
    const auto result = transaction.Install(fixture.writes, fixture.hooks);
    failures += Expect(
        !result && result.error().rollback_attempted &&
            !result.error().rollback_complete,
        "failed restore is reported as incomplete rollback");
}

return failures == 0 ? 0 : 1;
}
```

The final block above returns the aggregate result from `main()`.

- [ ] **Step 2: Build the transaction test and verify RED**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target FrameratePatchTransactionTests
```

Expected: compilation fails because the transaction interface is absent.

- [ ] **Step 3: Define owned byte and hook descriptors**

Create `src/Patches/Framerate/FrameratePatchTransaction.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::framerate {

inline constexpr std::size_t kMaximumPatternBytes = 16;
inline constexpr std::size_t kMaximumFramerateWrites = 16;
inline constexpr std::size_t kMaximumFramerateHooks = 16;

struct BytePattern {
    std::array<std::byte, kMaximumPatternBytes> bytes{};
    std::uint8_t size{};

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return {bytes.data(), size};
    }

    friend bool operator==(const BytePattern&, const BytePattern&) = default;
};

struct CheckedWrite {
    std::uintptr_t address{};
    BytePattern expected{};
    BytePattern replacement{};
    const char* name{};
};

struct HookOperation {
    std::uintptr_t address{};
    BytePattern expected{};
    const char* name{};
    void* context{};
    bool (*install)(void*) noexcept{};
    void (*reset)(void*) noexcept{};
};

struct FramerateMemoryApi {
    bool (*read)(std::uintptr_t, std::span<std::byte>) noexcept;
    bool (*write)(std::uintptr_t, std::span<const std::byte>) noexcept;
};

enum class FramerateInstallStage {
    None,
    Capacity,
    InvalidDescriptor,
    PreflightRead,
    PreflightMismatch,
    DirectWrite,
    HookInstall,
};

struct FramerateInstallError {
    FramerateInstallStage stage{FramerateInstallStage::None};
    std::size_t operation_index{};
    const char* operation_name{};
    bool rollback_attempted{};
    bool rollback_complete{};
};

class FrameratePatchTransaction {
public:
    explicit FrameratePatchTransaction(FramerateMemoryApi memory) noexcept;

    [[nodiscard]] std::expected<void, FramerateInstallError> Install(
        std::span<const CheckedWrite> writes,
        std::span<const HookOperation> hooks) noexcept;

    [[nodiscard]] bool Rollback() noexcept;
    [[nodiscard]] bool committed() const noexcept { return committed_; }

private:
    [[nodiscard]] bool PatternMatches(
        std::uintptr_t address,
        const BytePattern& pattern) noexcept;
    [[nodiscard]] bool VerifyOriginalState() noexcept;
    [[nodiscard]] std::expected<void, FramerateInstallError> Fail(
        FramerateInstallStage stage,
        std::size_t index,
        const char* name) noexcept;

    FramerateMemoryApi memory_{};
    std::array<CheckedWrite, kMaximumFramerateWrites> writes_{};
    std::array<HookOperation, kMaximumFramerateHooks> hooks_{};
    std::size_t write_count_{};
    std::size_t hook_count_{};
    std::size_t applied_write_count_{};
    std::size_t installed_hook_count_{};
    bool committed_{};
};

[[nodiscard]] FramerateMemoryApi ProductionFramerateMemoryApi() noexcept;

} // namespace gc::framerate
```

- [ ] **Step 4: Implement preflight-before-mutation and reverse rollback**

The `Install` algorithm in `FrameratePatchTransaction.cpp` must execute in this exact order:

```cpp
if (writes.size() > writes_.size() || hooks.size() > hooks_.size()) {
    return std::unexpected(FramerateInstallError{
        .stage = FramerateInstallStage::Capacity,
    });
}

for (std::size_t index = 0; index < writes.size(); ++index) {
    const auto& write = writes[index];
    if (write.expected.size == 0 ||
        write.expected.size != write.replacement.size ||
        write.expected.size > kMaximumPatternBytes) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::InvalidDescriptor,
            .operation_index = index,
            .operation_name = write.name,
        });
    }
    std::array<std::byte, kMaximumPatternBytes> actual{};
    const auto destination = std::span{
        actual.data(), static_cast<std::size_t>(write.expected.size)};
    if (!memory_.read(write.address, destination)) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::PreflightRead,
            .operation_index = index,
            .operation_name = write.name,
        });
    }
    if (!std::equal(
            destination.begin(), destination.end(),
            write.expected.view().begin())) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::PreflightMismatch,
            .operation_index = index,
            .operation_name = write.name,
        });
    }
}

for (std::size_t index = 0; index < hooks.size(); ++index) {
    const auto& hook = hooks[index];
    if (hook.expected.size == 0 || hook.install == nullptr ||
        hook.reset == nullptr) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::InvalidDescriptor,
            .operation_index = index,
            .operation_name = hook.name,
        });
    }
    std::array<std::byte, kMaximumPatternBytes> actual{};
    const auto destination = std::span{
        actual.data(), static_cast<std::size_t>(hook.expected.size)};
    if (!memory_.read(hook.address, destination)) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::PreflightRead,
            .operation_index = index,
            .operation_name = hook.name,
        });
    }
    if (!std::equal(
            destination.begin(), destination.end(),
            hook.expected.view().begin())) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::PreflightMismatch,
            .operation_index = index,
            .operation_name = hook.name,
        });
    }
}

std::copy(writes.begin(), writes.end(), writes_.begin());
std::copy(hooks.begin(), hooks.end(), hooks_.begin());
write_count_ = writes.size();
hook_count_ = hooks.size();

for (std::size_t index = 0; index < write_count_; ++index) {
    applied_write_count_ = index + 1;
    if (!memory_.write(
            writes_[index].address,
            writes_[index].replacement.view())) {
        return Fail(
            FramerateInstallStage::DirectWrite,
            index,
            writes_[index].name);
    }
}

for (std::size_t index = 0; index < hook_count_; ++index) {
    installed_hook_count_ = index + 1;
    if (!hooks_[index].install(hooks_[index].context)) {
        return Fail(
            FramerateInstallStage::HookInstall,
            index,
            hooks_[index].name);
    }
}

committed_ = true;
return {};
```

`Fail` must preserve the original stage/name/index, call `Rollback`, and return an error with `rollback_attempted = true` and the returned completeness. `Rollback` resets every attempted hook in reverse order and restores every attempted write in reverse order. It then sets only `installed_hook_count_` and `applied_write_count_` to zero, calls `VerifyOriginalState` while `write_count_` and `hook_count_` still describe every configured site, and clears those configured counts only after verification. Verification rereads every configured write and hook site and compares it with the original pattern. Production read uses structured exception handling; production write uses `VirtualProtect(PAGE_EXECUTE_READWRITE)`, `memcpy`, `FlushInstructionCache`, checks restoration of the previous protection, and reports failure if any protection transition fails.

- [ ] **Step 5: Build and verify every injected failure path GREEN**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchTransactionTests
ctest --test-dir build-msvc32-release --output-on-failure -R '^FrameratePatchTransactionTests$'
```

Expected: all preflight, capacity, each-write, each-hook, reverse-order, complete-rollback, incomplete-rollback, and success cases pass.

- [ ] **Step 6: Commit the transaction**

```powershell
git add -- src/Patches/Framerate/FrameratePatchTransaction.h src/Patches/Framerate/FrameratePatchTransaction.cpp tests/Patches/Framerate/FrameratePatchTransactionTests.cpp src/Patches/CMakeLists.txt tests/Patches/CMakeLists.txt
git commit -m "feat: add transactional framerate patching"
```

---

### Task 6: Generate the Binary-Backed Patch Plan and Pure Hook Math

**Files:**
- Create: `src/Patches/Framerate/FrameratePatchPlan.h`
- Create: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Create: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**
- Consumes: `FramerateProfile`, transaction-owned patterns, a loaded executable base, and the stable address of the profile's target-rate float.
- Produces: a zero-write native plan, a 13-write transformed plan, 14 exact hook-site contracts, `ApplyCmp32Flags`, and pure helpers for countdown equality, duration scaling, authored clip indices, and audio divisors.

- [ ] **Step 1: Register and write RED tests for every validated rate and byte contract**

Append:

```cmake
add_executable(FrameratePatchPlanTests
        Framerate/FrameratePatchPlanTests.cpp)
target_link_libraries(FrameratePatchPlanTests PRIVATE gc_runtime_patches)
add_test(NAME FrameratePatchPlanTests COMMAND FrameratePatchPlanTests)
```

Create `tests/Patches/Framerate/FrameratePatchPlanTests.cpp` with these exact helpers before `main()`:

```cpp
#include "Patches/Framerate/FrameratePatchPlan.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <span>

namespace {

constexpr std::uintptr_t kFakeBase = 0x00400000;
constexpr std::uint64_t kFakeTargetOperand = 0x12345678;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

gc::framerate::BytePattern Pattern(
    std::initializer_list<std::uint8_t> values) {
    gc::framerate::BytePattern pattern{};
    pattern.size = static_cast<std::uint8_t>(values.size());
    std::transform(
        values.begin(), values.end(), pattern.bytes.begin(),
        [](std::uint8_t value) { return static_cast<std::byte>(value); });
    return pattern;
}

const gc::framerate::CheckedWrite* FindWrite(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva) {
    const auto address = kFakeBase + rva;
    const auto found = std::find_if(
        plan.view().begin(), plan.view().end(),
        [address](const auto& write) { return write.address == address; });
    return found == plan.view().end() ? nullptr : &*found;
}

bool PlanContainsRva(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva) {
    return FindWrite(plan, rva) != nullptr;
}

std::uint32_t ReadInstructionImmediate(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva,
    std::size_t offset) {
    const auto* write = FindWrite(plan, rva);
    if (write == nullptr || offset + sizeof(std::uint32_t) >
        write->replacement.size) {
        std::abort();
    }
    std::uint32_t value{};
    std::memcpy(
        &value,
        write->replacement.bytes.data() + offset,
        sizeof(value));
    return value;
}

float ReadFloatReplacement(
    const gc::framerate::FramerateDirectPatchPlan& plan,
    std::uintptr_t rva) {
    const auto bits = ReadInstructionImmediate(plan, rva, 0);
    float value{};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

const gc::framerate::FramerateHookContract& FindHook(
    std::span<const gc::framerate::FramerateHookContract> contracts,
    gc::framerate::FramerateHookId id) {
    const auto found = std::find_if(
        contracts.begin(), contracts.end(),
        [id](const auto& contract) { return contract.id == id; });
    if (found == contracts.end()) {
        std::abort();
    }
    return *found;
}

} // namespace
```

In `main()`, use these assertions:

```cpp
using namespace gc::framerate;
int failures = 0;

const auto native_profile = FramerateProfile::Create(60).value();
const auto native_plan = BuildFramerateDirectPatchPlan(
    kFakeBase, native_profile, kFakeTargetOperand).value();
failures += Expect(native_plan.count == 0, "60 has no timing writes");

for (const std::uint32_t target : {120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto profile = FramerateProfile::Create(target).value();
    const auto plan = BuildFramerateDirectPatchPlan(
        kFakeBase, profile, kFakeTargetOperand).value();
    failures += Expect(plan.count == 13, "high target has 13 direct writes");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002FC0A0) ==
            profile.frame_milliseconds(),
        "gameplay frame-ms replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002F4604) ==
            profile.frame_milliseconds(),
        "visual frame-ms replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002FC280) ==
            profile.frame_seconds(),
        "gameplay frame-seconds replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002E8F00) ==
            profile.render_smoothing_step(),
        "smoothing replacement");
    failures += Expect(
        ReadFloatReplacement(plan, 0x002E8F04) ==
            profile.render_offset_decay_step(),
        "decay replacement");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00055CCC, 2) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(16).value()),
        "repeat initial duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00055CDD, 2) ==
            static_cast<std::uint32_t>(
                profile.ScaleDurationFrames(8).value()),
        "repeat next duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x002645EE, 6) ==
            profile.two_second_frames(),
        "gameplay countdown duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00249A5E, 1) ==
            profile.two_second_frames(),
        "render EAX countdown duration");
    failures += Expect(
        ReadInstructionImmediate(plan, 0x00249A73, 1) ==
            profile.two_second_frames(),
        "render EDX countdown duration");
    for (const auto rva : {0x0022BACFU, 0x0022BAD5U, 0x00262CB6U}) {
        failures += Expect(
            ReadInstructionImmediate(plan, rva, 2) == kFakeTargetOperand,
            "local x87 operand redirects to profile target");
    }
    failures += Expect(
        !PlanContainsRva(plan, 0x0022BA60),
        "palette imm8 compare is never directly patched");
}

const auto native_hooks = FramerateHookContracts(false);
const auto transformed_hooks = FramerateHookContracts(true);
failures += Expect(native_hooks.size() == 1, "60 uses cadence hook only");
failures += Expect(
    native_hooks[0].id == FramerateHookId::OuterFrame,
    "native hook is outer cadence");
failures += Expect(transformed_hooks.size() == 14,
    "transformed mode has all 14 hooks");
failures += Expect(
    transformed_hooks.back().id == FramerateHookId::OuterFrame,
    "outer-frame hook installs last");
failures += Expect(
    FindHook(transformed_hooks, FramerateHookId::PaletteCompare).expected ==
        Pattern({0x83, 0x78, 0x0C, 0x3C}),
    "palette compare exact bytes");

const std::array<FramerateHookContract, 14> expected_hooks{{
    {FramerateHookId::MovieClipGoto, 0x000DEA30,
        Pattern({0x6A, 0xFF, 0x68, 0xC9, 0x38, 0x67, 0x00}), ""},
    {FramerateHookId::MovieClipAdvance, 0x000DF940,
        Pattern({0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x90, 0x4C, 0x01, 0x00, 0x00}), ""},
    {FramerateHookId::NewsUpdate, 0x00218A50,
        Pattern({0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xED, 0xA1, 0x67, 0x00}), ""},
    {FramerateHookId::NoticeUpdate, 0x002544D0,
        Pattern({0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x7F, 0x96, 0x67, 0x00}), ""},
    {FramerateHookId::PaletteCompare, 0x0022BA60,
        Pattern({0x83, 0x78, 0x0C, 0x3C}), ""},
    {FramerateHookId::StageClipFrame, 0x00244054,
        Pattern({0x89, 0x4D, 0xF8}), ""},
    {FramerateHookId::IfblWait, 0x002309D4,
        Pattern({0x89, 0x4A, 0x3C}), ""},
    {FramerateHookId::IfblLoop, 0x00230AB6,
        Pattern({0x89, 0x4C, 0x90, 0x1C}), ""},
    {FramerateHookId::StageBgmPreload, 0x0021001A,
        Pattern({0x83, 0xC0, 0x01}), ""},
    {FramerateHookId::TuneCountdownCompare, 0x002648F7,
        Pattern({0x83, 0xBA, 0x14, 0x1D, 0x00, 0x00, 0x78}), ""},
    {FramerateHookId::AudioSkipMargin, 0x0024018F,
        Pattern({0x8B, 0x45, 0xF4}), ""},
    {FramerateHookId::AudioSkipInterval, 0x002401BD,
        Pattern({0xF7, 0x79, 0x3C}), ""},
    {FramerateHookId::AudioResyncDiagnostic, 0x002401C4,
        Pattern({0x8B, 0x55, 0xF8}), ""},
    {FramerateHookId::OuterFrame, 0x00058B70,
        Pattern({0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x50, 0x24}), ""},
}};
for (std::size_t index = 0; index < expected_hooks.size(); ++index) {
    failures += Expect(
        transformed_hooks[index].id == expected_hooks[index].id &&
            transformed_hooks[index].rva == expected_hooks[index].rva &&
            transformed_hooks[index].expected == expected_hooks[index].expected,
        "exact hook ID/RVA/byte contract");
}

failures += Expect(
    !BuildFramerateDirectPatchPlan(
        kFakeBase,
        FramerateProfile::Create(120).value(),
        static_cast<std::uint64_t>(UINT32_MAX) + 1),
    "x87 operand above 32-bit range is rejected");
failures += Expect(
    !BuildFramerateDirectPatchPlan(
        0x00500000,
        FramerateProfile::Create(120).value(),
        kFakeTargetOperand),
    "unexpected loaded image base is rejected");
```

Add exact 120-FPS assertions for `1000/120`, `1/120`, smoothing `2.0`, decay `2.5`, repeat delays 32/16, countdown 240, and the three `kFakeTargetOperand` x87 operands. The loop above covers 144/165 rational durations and target 500; Task 2 already covers sentinels and every authored mapping, while Task 7's runtime test covers the stage sequences and CMP/JGE flags actually consumed by callbacks. End with `return failures == 0 ? 0 : 1;`.

- [ ] **Step 2: Build the patch-plan test and verify RED**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target FrameratePatchPlanTests
```

Expected: compilation fails because `FrameratePatchPlan.h` is absent.

- [ ] **Step 3: Define the direct-plan, hook-contract, and math interfaces**

Create `src/Patches/Framerate/FrameratePatchPlan.h`:

```cpp
#pragma once

#include "Patches/Framerate/FrameratePatchTransaction.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <array>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::framerate {

enum class FrameratePatchPlanError {
    ProfileConversion,
    OperandAddressOutOfRange,
    UnexpectedImageBase,
    Capacity,
};

struct FramerateDirectPatchPlan {
    std::array<CheckedWrite, 13> writes{};
    std::size_t count{};

    [[nodiscard]] std::span<const CheckedWrite> view() const noexcept {
        return {writes.data(), count};
    }
};

enum class FramerateHookId {
    MovieClipGoto,
    MovieClipAdvance,
    NewsUpdate,
    NoticeUpdate,
    PaletteCompare,
    StageClipFrame,
    IfblWait,
    IfblLoop,
    StageBgmPreload,
    TuneCountdownCompare,
    AudioSkipMargin,
    AudioSkipInterval,
    AudioResyncDiagnostic,
    OuterFrame,
};

struct FramerateHookContract {
    FramerateHookId id{};
    std::uintptr_t rva{};
    BytePattern expected{};
    const char* name{};
};

[[nodiscard]] std::expected<
    FramerateDirectPatchPlan,
    FrameratePatchPlanError>
BuildFramerateDirectPatchPlan(
    std::uintptr_t executable_base,
    const FramerateProfile& profile,
    std::uint64_t target_fps_operand) noexcept;

[[nodiscard]] std::span<const FramerateHookContract>
FramerateHookContracts(bool transformed_timing) noexcept;

[[nodiscard]] std::uint32_t ApplyCmp32Flags(
    std::uint32_t existing_eflags,
    std::uint32_t left,
    std::uint32_t right) noexcept;

[[nodiscard]] std::expected<std::uint32_t, FramerateProfileError>
ScalePositiveFrameCount(
    const FramerateProfile& profile,
    std::uint32_t raw_value) noexcept;

} // namespace gc::framerate
```

- [ ] **Step 4: Encode every exact `game471.exe` hook-site contract**

Use the following IDA-confirmed table in `FrameratePatchPlan.cpp`. High-rate contracts are listed first and the outer-frame hook is last; the native span contains only the last entry.

| Hook ID | RVA | Exact expected bytes |
|---|---:|---|
| `MovieClipGoto` | `0x000DEA30` | `6A FF 68 C9 38 67 00` |
| `MovieClipAdvance` | `0x000DF940` | `56 8B F1 8B 06 8B 90 4C 01 00 00` |
| `NewsUpdate` | `0x00218A50` | `55 8B EC 6A FF 68 ED A1 67 00` |
| `NoticeUpdate` | `0x002544D0` | `55 8B EC 6A FF 68 7F 96 67 00` |
| `PaletteCompare` | `0x0022BA60` | `83 78 0C 3C` |
| `StageClipFrame` | `0x00244054` | `89 4D F8` |
| `IfblWait` | `0x002309D4` | `89 4A 3C` |
| `IfblLoop` | `0x00230AB6` | `89 4C 90 1C` |
| `StageBgmPreload` | `0x0021001A` | `83 C0 01` |
| `TuneCountdownCompare` | `0x002648F7` | `83 BA 14 1D 00 00 78` |
| `AudioSkipMargin` | `0x0024018F` | `8B 45 F4` |
| `AudioSkipInterval` | `0x002401BD` | `F7 79 3C` |
| `AudioResyncDiagnostic` | `0x002401C4` | `8B 55 F8` |
| `OuterFrame` | `0x00058B70` | `56 8B F1 8B 06 8B 50 24` |

Represent each pattern as owned bytes in the returned contract; do not retain initializer-list spans.

- [ ] **Step 5: Generate all 13 full-instruction checked writes**

Use complete original and replacement instructions, not operand-only writes. The plan must contain these source contracts:

| RVA | Original contract | Replacement |
|---:|---|---|
| `0x002FC0A0` | f32 `1000/60` | profile frame milliseconds |
| `0x002F4604` | f32 `1000/60` | profile frame milliseconds |
| `0x002FC280` | f32 `1/60` | profile frame seconds |
| `0x002E8F00` | f32 `4.0` | profile smoothing step |
| `0x002E8F04` | f32 `5.0` | profile offset-decay step |
| `0x00055CCC` | `C7 00 10 00 00 00` | `C7 00` + scaled 16-frame duration |
| `0x00055CDD` | `C7 00 08 00 00 00` | `C7 00` + scaled 8-frame duration |
| `0x002645EE` | `C7 80 14 1D 00 00 78 00 00 00` | same prefix + `2*target_fps` |
| `0x00249A5E` | `B8 78 00 00 00` | `B8` + `2*target_fps` |
| `0x00249A73` | `BA 78 00 00 00` | `BA` + `2*target_fps` |
| `0x0022BACF` | `D8 2D AC BB 6F 00` | `D8 2D` + profile target-float address |
| `0x0022BAD5` | `D8 35 AC BB 6F 00` | `D8 35` + profile target-float address |
| `0x00262CB6` | `D8 0D AC BB 6F 00` | `D8 0D` + profile target-float address |

Build float bytes with `std::bit_cast<std::uint32_t>` and every immediate/address with an explicit little-endian append helper. Reject an executable base other than `0x00400000` and a target operand above `UINT32_MAX`. Return an empty plan when `profile.native_timing()` is true. Do not include RVA `0x0022BA60` and do not change the shared float at RVA `0x002FBBAC`.

- [ ] **Step 6: Implement pure comparison and runtime conversion helpers**

Implement all arithmetic without `target_fps / 60` truncation:

```cpp
std::uint32_t ApplyCmp32Flags(
    std::uint32_t existing,
    std::uint32_t left,
    std::uint32_t right) noexcept {
    constexpr std::uint32_t kCarry = 0x001;
    constexpr std::uint32_t kParity = 0x004;
    constexpr std::uint32_t kAuxiliary = 0x010;
    constexpr std::uint32_t kZero = 0x040;
    constexpr std::uint32_t kSign = 0x080;
    constexpr std::uint32_t kOverflow = 0x800;
    constexpr std::uint32_t kMask =
        kCarry | kParity | kAuxiliary | kZero | kSign | kOverflow;

    const std::uint32_t result = left - right;
    const bool even_parity =
        (std::popcount(result & 0xFFU) & 1U) == 0;
    std::uint32_t flags = existing & ~kMask;
    flags |= left < right ? kCarry : 0;
    flags |= even_parity ? kParity : 0;
    flags |= ((left ^ right ^ result) & 0x10U) != 0 ? kAuxiliary : 0;
    flags |= result == 0 ? kZero : 0;
    flags |= (result & 0x80000000U) != 0 ? kSign : 0;
    flags |= ((left ^ right) & (left ^ result) & 0x80000000U) != 0
        ? kOverflow
        : 0;
    return flags;
}

std::expected<std::uint32_t, FramerateProfileError>
ScalePositiveFrameCount(
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
```

The runtime callbacks will use `profile.MapToAuthored60(target_frame)` for stage clip masks, `profile.ScaleDurationFrames(interval)` for audio divisors, `profile.two_second_frames()` for countdown equality, and `ApplyCmp32Flags` for the palette comparison.

- [ ] **Step 7: Build and verify the patch plan GREEN**

```powershell
cmake --build --preset msvc32-release --target FrameratePatchPlanTests
ctest --test-dir build-msvc32-release --output-on-failure -R '^FrameratePatchPlanTests$'
```

Expected: every validated target, 60/61/500 boundaries, exact original byte contract, target-derived replacement, CMP flag case, authored mapping sequence, and invalid operand case passes.

- [ ] **Step 8: Commit the binary-backed plan**

```powershell
git add -- src/Patches/Framerate/FrameratePatchPlan.h src/Patches/Framerate/FrameratePatchPlan.cpp tests/Patches/Framerate/FrameratePatchPlanTests.cpp src/Patches/CMakeLists.txt tests/Patches/CMakeLists.txt
git commit -m "feat: build target-derived framerate patch plan"
```

---

### Task 7: Compose the Profile, Monitor, Transaction, and Domain-Specific Hooks

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Replace: `src/Patches/Framerate/FrameratePatch.cpp`
- Create: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**
- Consumes: `ConfigManager::GetTargetFps`, all Task 2-6 components, SafetyHook, QPC, and the existing countdown-freeze setting.
- Produces: `gc::framerate::FrameratePatchInit() -> bool`, process-lifetime runtime state, target-aware callbacks, cadence success/fatal publication, and fail-closed DLL attach behavior.

- [ ] **Step 1: Add RED runtime-helper and loader-contract tests**

Append:

```cmake
add_executable(FramerateRuntimeTests
        Framerate/FramerateRuntimeTests.cpp)
target_link_libraries(FramerateRuntimeTests PRIVATE gc_runtime_patches)
add_test(NAME FramerateRuntimeTests COMMAND FramerateRuntimeTests)
```

Create `tests/Patches/Framerate/FramerateRuntimeTests.cpp` to exercise the public pure helpers used by callbacks:

```cpp
#include "Patches/Framerate/FrameratePatch.h"
#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FramerateProfile.h"

#include <concepts>
#include <cstdint>
#include <iostream>

using namespace gc::framerate;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

int main() {
int failures = 0;

for (const std::uint32_t target : {120U, 144U, 165U, 240U, 360U}) {
    const auto profile = FramerateProfile::Create(target).value();
    for (std::uint32_t frame = 0; frame < target * 3; ++frame) {
        const auto mapped = profile.MapToAuthored60(frame).value();
        failures += Expect(
            mapped == static_cast<std::uint64_t>(frame) * 60 / target,
            "stage clip uses rational floor mapping");
    }
    failures += Expect(
        profile.ScaleDurationFrames(16).value() ==
            (16 * static_cast<std::int64_t>(target) + 30) / 60,
        "input delay uses nearest rational scaling");
}

const auto profile144 = FramerateProfile::Create(144).value();
failures += Expect(
    profile144.ScaleDurationFrames(25).value() == 60,
    "audio interval scales without integer ratio");
failures += Expect(
    profile144.ScaleDurationFrames(0).value() == 0 &&
        profile144.ScaleDurationFrames(-1).value() == -1,
    "runtime counter sentinels survive");

for (const std::uint32_t cap : {120U, 144U, 165U, 240U, 360U, 500U}) {
    const auto below = ApplyCmp32Flags(0x202, cap - 1, cap);
    const auto equal = ApplyCmp32Flags(0x202, cap, cap);
    const auto above = ApplyCmp32Flags(0x202, cap + 1, cap);
    failures += Expect((below & 0x40U) == 0, "palette below is not equal");
    failures += Expect((equal & 0x40U) != 0, "palette equal sets ZF");
    failures += Expect((above & 0x40U) == 0, "palette above is not equal");
    failures += Expect(
        ((below >> 7U) & 1U) != ((below >> 11U) & 1U),
        "signed JGE sees below as less");
    failures += Expect(
        ((equal >> 7U) & 1U) == ((equal >> 11U) & 1U) &&
            ((above >> 7U) & 1U) == ((above >> 11U) & 1U),
        "signed JGE sees equal and above as not less");
}
```

Add a compile-time assertion that `FrameratePatchInit()` returns `bool`:

```cpp
static_assert(std::same_as<
    decltype(gc::framerate::FrameratePatchInit()), bool>);

return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Build and verify RED**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target FramerateRuntimeTests iDmacDrv32
```

Expected: compilation fails because `FrameratePatchInit` still has the old global `void` signature and the runtime has not been generalized.

- [ ] **Step 3: Change the public initialization contract and loader gate**

Replace `FrameratePatch.h` with:

```cpp
#pragma once

namespace gc::framerate {

[[nodiscard]] bool FrameratePatchInit();

} // namespace gc::framerate
```

Replace the unconditional call in `src/Loader/DllMain.cpp` with:

```cpp
if (!gc::framerate::FrameratePatchInit()) {
    PLOG_ERROR << "FrameratePatch: fail-closed DLL attach";
    return FALSE;
}
PLOG_DEBUG << "Framerate runtime initialization complete!";
```

The production fatal reporter normally prevents return on failure; returning `FALSE` remains the final loader boundary if an injected or exceptional platform action returns.

- [ ] **Step 4: Introduce one process-lifetime runtime state and owned hook set**

In `FrameratePatch.cpp`, define:

```cpp
struct FramerateHookStorage {
    safetyhook::InlineHook movieclip_goto{};
    safetyhook::InlineHook movieclip_advance{};
    safetyhook::InlineHook news_update{};
    safetyhook::InlineHook notice_update{};
    safetyhook::MidHook palette_compare{};
    safetyhook::MidHook stage_clip_frame{};
    safetyhook::MidHook ifbl_wait{};
    safetyhook::MidHook ifbl_loop{};
    safetyhook::MidHook stage_bgm_preload{};
    safetyhook::MidHook tune_countdown_compare{};
    safetyhook::MidHook audio_skip_margin{};
    safetyhook::MidHook audio_skip_interval{};
    safetyhook::MidHook audio_resync_diagnostic{};
    safetyhook::MidHook outer_frame{};
};

struct FramerateRuntimeState {
    FramerateRuntimeState(
        FramerateProfile profile_value,
        FramerateMonitor monitor_value,
        std::int64_t frequency_value,
        FrameratePlatformActions platform_value) noexcept
        : profile{std::move(profile_value)},
          monitor{std::move(monitor_value)},
          qpc_frequency{frequency_value},
          platform{platform_value},
          transaction{ProductionFramerateMemoryApi()} {}

    FramerateProfile profile;
    FramerateMonitor monitor;
    std::int64_t qpc_frequency{};
    FrameratePlatformActions platform{};
    FrameratePatchTransaction transaction;
    FramerateHookStorage hooks;
    std::atomic_bool fatal_published{false};
    std::atomic_bool authored_60hz_tick{true};
    std::int64_t previous_qpc{};
    double authored_accumulator{};
    bool authored_clock_started{};
};

std::optional<FramerateRuntimeState> g_runtime;
```

Keep the existing diagnostic counters, but rename every `GC120FPS` label and 120-specific counter name to `FrameratePatch`/target-neutral terminology.

- [ ] **Step 5: Create type-safe SafetyHook install/reset operations for all 14 contracts**

Use pointer-to-member templates so every operation has the same transaction interface without duplicating ownership logic:

```cpp
template <
    safetyhook::MidHook FramerateHookStorage::*Member,
    safetyhook::MidHookFn Callback,
    std::uintptr_t Rva>
bool InstallMidHook(void* opaque) noexcept {
    auto& state = *static_cast<FramerateRuntimeState*>(opaque);
    auto& hook = state.hooks.*Member;
    hook = safetyhook::create_mid(
        reinterpret_cast<void*>(ExecutableBase() + Rva), Callback);
    return static_cast<bool>(hook);
}

template <
    safetyhook::InlineHook FramerateHookStorage::*Member,
    auto Callback,
    std::uintptr_t Rva>
bool InstallInlineHook(void* opaque) noexcept {
    auto& state = *static_cast<FramerateRuntimeState*>(opaque);
    auto& hook = state.hooks.*Member;
    hook = safetyhook::create_inline(
        reinterpret_cast<void*>(ExecutableBase() + Rva),
        reinterpret_cast<void*>(Callback));
    return static_cast<bool>(hook);
}

template <auto Member>
void ResetOwnedHook(void* opaque) noexcept {
    auto& state = *static_cast<FramerateRuntimeState*>(opaque);
    (state.hooks.*Member).reset();
}
```

Map every `FramerateHookId` from Task 6 to exactly one template instantiation:

| ID | Storage member | Callback | Kind |
|---|---|---|---|
| `MovieClipGoto` | `movieclip_goto` | `HookMovieClipGoto` | inline |
| `MovieClipAdvance` | `movieclip_advance` | `HookMovieClipAdvance` | inline |
| `NewsUpdate` | `news_update` | `HookNewsUpdate` | inline |
| `NoticeUpdate` | `notice_update` | `HookNoticeUpdate` | inline |
| `PaletteCompare` | `palette_compare` | `HookPaletteCompare` | mid |
| `StageClipFrame` | `stage_clip_frame` | `HookStageClipFrame` | mid |
| `IfblWait` | `ifbl_wait` | `HookIfblWait` | mid |
| `IfblLoop` | `ifbl_loop` | `HookIfblLoop` | mid |
| `StageBgmPreload` | `stage_bgm_preload` | `HookStageBgmPreload` | mid |
| `TuneCountdownCompare` | `tune_countdown_compare` | `HookTuneCountdownCompare` | mid |
| `AudioSkipMargin` | `audio_skip_margin` | `HookAudioSkipMargin` | mid |
| `AudioSkipInterval` | `audio_skip_interval` | `HookAudioSkipInterval` | mid |
| `AudioResyncDiagnostic` | `audio_resync_diagnostic` | `HookAudioResyncDiagnostic` | mid |
| `OuterFrame` | `outer_frame` | `HookOuterFrame` | mid |

Store the resulting operations in this fixed-capacity plan:

```cpp
struct FramerateHookOperationPlan {
    std::array<HookOperation, 14> operations{};
    std::size_t count{};

    [[nodiscard]] std::span<const HookOperation> view() const noexcept {
        return {operations.data(), count};
    }
};

FramerateHookOperationPlan BuildHookOperations(
    std::span<const FramerateHookContract> contracts,
    FramerateRuntimeState& state) noexcept {
    FramerateHookOperationPlan plan{};
    for (const auto& contract : contracts) {
        auto& operation = plan.operations[plan.count++];
        operation.address = ExecutableBase() + contract.rva;
        operation.expected = contract.expected;
        operation.name = contract.name;
        operation.context = &state;
        AssignHookCallbacks(contract.id, operation);
    }
    return plan;
}
```

Implement the callback assignment exactly and omit a default case:

```cpp
void AssignHookCallbacks(
    FramerateHookId id,
    HookOperation& operation) noexcept {
    switch (id) {
    case FramerateHookId::MovieClipGoto:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::movieclip_goto,
            HookMovieClipGoto,
            0x000DEA30>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::movieclip_goto>;
        break;
    case FramerateHookId::MovieClipAdvance:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::movieclip_advance,
            HookMovieClipAdvance,
            0x000DF940>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::movieclip_advance>;
        break;
    case FramerateHookId::NewsUpdate:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::news_update,
            HookNewsUpdate,
            0x00218A50>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::news_update>;
        break;
    case FramerateHookId::NoticeUpdate:
        operation.install = &InstallInlineHook<
            &FramerateHookStorage::notice_update,
            HookNoticeUpdate,
            0x002544D0>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::notice_update>;
        break;
    case FramerateHookId::PaletteCompare:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::palette_compare,
            HookPaletteCompare,
            0x0022BA60>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::palette_compare>;
        break;
    case FramerateHookId::StageClipFrame:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::stage_clip_frame,
            HookStageClipFrame,
            0x00244054>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::stage_clip_frame>;
        break;
    case FramerateHookId::IfblWait:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::ifbl_wait,
            HookIfblWait,
            0x002309D4>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::ifbl_wait>;
        break;
    case FramerateHookId::IfblLoop:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::ifbl_loop,
            HookIfblLoop,
            0x00230AB6>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::ifbl_loop>;
        break;
    case FramerateHookId::StageBgmPreload:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::stage_bgm_preload,
            HookStageBgmPreload,
            0x0021001A>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::stage_bgm_preload>;
        break;
    case FramerateHookId::TuneCountdownCompare:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::tune_countdown_compare,
            HookTuneCountdownCompare,
            0x002648F7>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::tune_countdown_compare>;
        break;
    case FramerateHookId::AudioSkipMargin:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::audio_skip_margin,
            HookAudioSkipMargin,
            0x0024018F>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::audio_skip_margin>;
        break;
    case FramerateHookId::AudioSkipInterval:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::audio_skip_interval,
            HookAudioSkipInterval,
            0x002401BD>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::audio_skip_interval>;
        break;
    case FramerateHookId::AudioResyncDiagnostic:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::audio_resync_diagnostic,
            HookAudioResyncDiagnostic,
            0x002401C4>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::audio_resync_diagnostic>;
        break;
    case FramerateHookId::OuterFrame:
        operation.install = &InstallMidHook<
            &FramerateHookStorage::outer_frame,
            HookOuterFrame,
            0x00058B70>;
        operation.reset = &ResetOwnedHook<
            &FramerateHookStorage::outer_frame>;
        break;
    }
}
```

The Task 6 ordering guarantees the outer-frame hook becomes observable only after every transformed-domain hook succeeds.

- [ ] **Step 6: Generalize every domain-specific callback**

Use the immutable profile from `g_runtime`; do not read configuration in a callback.

Palette comparison must reproduce all CMP flags and skip only the four-byte compare:

```cpp
void HookPaletteCompare(safetyhook::Context& context) {
    std::uint32_t counter{};
    if (!ReadU32Safe(context.eax + 0x0C, counter)) {
        ReportFramerateRuntimeFailure(
            "palette counter read failed",
            g_runtime->fatal_published,
            g_runtime->platform);
        return;
    }
    context.eflags = ApplyCmp32Flags(
        context.eflags,
        counter,
        g_runtime->profile.palette_frame_cap());
    context.eip += 4;
}
```

Stage, IFBL, countdown, and audio conversions must be:

```cpp
void HookStageClipFrame(safetyhook::Context& context) {
    const auto mapped = g_runtime->profile.MapToAuthored60(context.ecx);
    if (!mapped) {
        FatalRuntimeConversion("stage clip frame mapping");
        return;
    }
    context.ecx = mapped.value();
}

void HookIfblWait(safetyhook::Context& context) {
    const auto scaled = ScalePositiveFrameCount(
        g_runtime->profile, context.ecx);
    if (!scaled) {
        FatalRuntimeConversion("IFBL wait scaling");
        return;
    }
    if (WriteU32Safe(context.edx + 0x3C, scaled.value())) {
        context.eip += 3;
    } else {
        FatalRuntimeConversion("IFBL wait store");
    }
}

void HookIfblLoop(safetyhook::Context& context) {
    const auto scaled = ScalePositiveFrameCount(
        g_runtime->profile, context.ecx);
    if (!scaled) {
        FatalRuntimeConversion("IFBL loop scaling");
        return;
    }
    if (WriteU32Safe(
            context.eax + context.edx * 4 + 0x1C,
            scaled.value())) {
        context.eip += 4;
    } else {
        FatalRuntimeConversion("IFBL loop store");
    }
}

void HookTuneCountdownCompare(safetyhook::Context& context) {
    std::uint32_t countdown{};
    if (!ReadU32Safe(context.edx + 0x1D14, countdown)) {
        FatalRuntimeConversion("countdown compare read");
        return;
    }
    SetZeroFlag(
        context,
        countdown == g_runtime->profile.two_second_frames());
    context.eip += 7;
}
```

Implement the internal helper used above as:

```cpp
void FatalRuntimeConversion(std::string_view operation) noexcept {
    ReportFramerateRuntimeFailure(
        operation,
        g_runtime->fatal_published,
        g_runtime->platform);
}
```

For `HookAudioSkipInterval`, read the positive interval at `[ecx+0x3C]`, call `ScaleDurationFrames`, emulate signed `EDX:EAX / scaled_interval`, write quotient/remainder only if they fit signed 32-bit, and skip the three-byte original `idiv`. Nonpositive interval sentinels fall through to the original instruction. Overflow or an invalid divisor calls `FatalRuntimeConversion`. Keep the 48-ms skip-margin floor unchanged. Keep the resync hook diagnostic only and include `target_fps` in its bounded log labels. Replace the old first-120/every-60 observation thresholds with `first target_fps` and then every `max(1U, target_fps / 2)` observations so diagnostic volume remains approximately wall-clock stable without affecting gameplay math.

The signed division replacement must use defined arithmetic:

```cpp
void HookAudioSkipInterval(safetyhook::Context& context) {
    std::uint32_t raw_interval{};
    if (!ReadU32Safe(context.ecx + 0x3C, raw_interval)) {
        FatalRuntimeConversion("audio interval read");
        return;
    }

    const auto interval = static_cast<std::int32_t>(raw_interval);
    if (interval <= 0) {
        return;
    }
    const auto scaled = g_runtime->profile.ScaleDurationFrames(interval);
    if (!scaled || scaled.value() <= 0) {
        FatalRuntimeConversion("audio interval scaling");
        return;
    }

    const auto high = static_cast<std::int64_t>(
        static_cast<std::int32_t>(context.edx));
    const auto dividend = high * (std::int64_t{1} << 32) +
        static_cast<std::uint32_t>(context.eax);
    const auto quotient = dividend / scaled.value();
    const auto remainder = dividend % scaled.value();
    if (quotient < std::numeric_limits<std::int32_t>::min() ||
        quotient > std::numeric_limits<std::int32_t>::max()) {
        FatalRuntimeConversion("audio interval quotient overflow");
        return;
    }

    context.eax = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(quotient));
    context.edx = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(remainder));
    context.eip += 3;
}
```

Keep ordinary MovieClip, news, notice, and BGM-preload gating semantically identical to the current implementation. Preserve the goto-depth guard so nested frame actions are never skipped. Do not add a generic update, render, input, audio, or animation skip.

Use these exact authored-domain callbacks:

```cpp
thread_local int g_movieclip_goto_depth = 0;

bool IsAuthored60HzTick() noexcept {
    return g_runtime->authored_60hz_tick.load(std::memory_order_acquire);
}

char __fastcall HookMovieClipGoto(
    void* self,
    void*,
    int frame,
    int subframe) {
    struct DepthGuard {
        DepthGuard() { ++g_movieclip_goto_depth; }
        ~DepthGuard() { --g_movieclip_goto_depth; }
    } guard;
    return g_runtime->hooks.movieclip_goto.unsafe_thiscall<char>(
        self, frame, subframe);
}

char __fastcall HookMovieClipAdvance(
    void* self,
    void*,
    char forward,
    char loop) {
    if (g_movieclip_goto_depth == 0 && !IsAuthored60HzTick()) {
        return 1;
    }
    return g_runtime->hooks.movieclip_advance.unsafe_thiscall<char>(
        self, forward, loop);
}

int __fastcall HookNewsUpdate(void* self, void*) {
    if (!IsAuthored60HzTick()) {
        return 1;
    }
    return g_runtime->hooks.news_update.unsafe_thiscall<int>(self);
}

int __fastcall HookNoticeUpdate(void* self, void*) {
    if (!IsAuthored60HzTick()) {
        return 1;
    }
    return g_runtime->hooks.notice_update.unsafe_thiscall<int>(self);
}

void HookStageBgmPreload(safetyhook::Context& context) {
    if (!IsAuthored60HzTick()) {
        context.eip += 3;
    }
}
```

- [ ] **Step 7: Feed the monitor and authored clock from the outer-frame hook**

The outer hook must call `QueryPerformanceCounter` once. Feed that timestamp to the monitor first, handle only completed-window results, and avoid per-frame allocation/logging:

```cpp
void HookOuterFrame(safetyhook::Context&) {
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
        ReportFramerateClockFailure(
            g_runtime->profile.target_fps(),
            g_runtime->fatal_published,
            g_runtime->platform);
        return;
    }

    if (auto observation = g_runtime->monitor.Observe(now.QuadPart)) {
        switch (observation->decision) {
        case FramerateDecision::Validated:
            LogCadenceValidated(*observation);
            break;
        case FramerateDecision::FatalMismatch:
            ReportFramerateMismatch(
                *observation,
                g_runtime->fatal_published,
                g_runtime->platform);
            break;
        case FramerateDecision::FatalClock:
            ReportFramerateClockFailure(
                g_runtime->profile.target_fps(),
                g_runtime->fatal_published,
                g_runtime->platform);
            break;
        case FramerateDecision::WindowMatch:
        case FramerateDecision::WindowMismatch:
            break;
        }
    }

    if (g_runtime->profile.native_timing()) {
        return;
    }
    UpdateAuthored60HzTick(now.QuadPart);
}
```

`LogCadenceValidated` emits exactly one final success line with `target_fps`, `measured_fps`, `relative_error`, `interval_count`, and `matching_windows=3`. Intermediate matching/mismatching windows do not log per sample or per frame.

`UpdateAuthored60HzTick` keeps the existing `1/60` accumulator and `1/30` maximum delta clamp, using the preflighted QPC frequency:

```cpp
void UpdateAuthored60HzTick(std::int64_t now) noexcept {
    if (!g_runtime->authored_clock_started) {
        g_runtime->authored_clock_started = true;
        g_runtime->previous_qpc = now;
        g_runtime->authored_60hz_tick.store(true, std::memory_order_release);
        return;
    }

    double delta = static_cast<double>(now - g_runtime->previous_qpc) /
        static_cast<double>(g_runtime->qpc_frequency);
    g_runtime->previous_qpc = now;
    delta = std::clamp(delta, 0.0, 1.0 / 30.0);
    g_runtime->authored_accumulator += delta;

    bool tick = false;
    if (g_runtime->authored_accumulator >= 1.0 / 60.0) {
        g_runtime->authored_accumulator -= 1.0 / 60.0;
        tick = true;
    }
    g_runtime->authored_60hz_tick.store(tick, std::memory_order_release);
}
```

Its tick is consumed only by the four authored-domain callbacks.

- [ ] **Step 8: Preflight QPC, construct the immutable runtime, and install atomically**

Implement `FrameratePatchInit()` in this order:

```cpp
bool FrameratePatchInit() {
    static std::atomic_bool initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return g_runtime.has_value() && g_runtime->transaction.committed();
    }

    const auto actions = ProductionFrameratePlatformActions();
    const auto target = ConfigManager::instance().GetTargetFps();
    const auto profile_result = FramerateProfile::Create(target);

    LARGE_INTEGER frequency{};
    if (!profile_result || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0) {
        static std::atomic_bool startup_fatal{false};
        ReportFramerateInitializationFailure(
            "profile or QPC preflight failed; executable memory was not changed",
            startup_fatal,
            actions);
        return false;
    }

    auto monitor_result = FramerateMonitor::Create(
        target, frequency.QuadPart);
    if (!monitor_result) {
        static std::atomic_bool startup_fatal{false};
        ReportFramerateInitializationFailure(
            "cadence monitor preflight failed; executable memory was not changed",
            startup_fatal,
            actions);
        return false;
    }

    g_runtime.emplace(
        std::move(profile_result.value()),
        std::move(monitor_result.value()),
        frequency.QuadPart,
        actions);

    ReportFramerateStartup(g_runtime->profile, actions);
    const auto direct_plan = BuildFramerateDirectPatchPlan(
        ExecutableBase(),
        g_runtime->profile,
        reinterpret_cast<std::uintptr_t>(
            g_runtime->profile.target_fps_operand()));
    if (!direct_plan) {
        FatalInstallPlanFailure(direct_plan.error());
        return false;
    }

    const auto hook_operations = BuildHookOperations(
        FramerateHookContracts(!g_runtime->profile.native_timing()),
        *g_runtime);
    const auto installed = g_runtime->transaction.Install(
        direct_plan->view(), hook_operations.view());
    if (!installed) {
        FatalTransactionFailure(installed.error());
        return false;
    }

    gc::timer_freeze::SetCountdownTimerFreezeEnabled(
        ConfigManager::instance().GetEnableTimerFreezePatches());
    gc::timer_freeze::CountdownTimerFreezeInit();
    return true;
}
```

`ReportFramerateStartup` emits target, frame milliseconds/seconds, native/transformed mode, derived countdown/palette/smoothing values, and whether the target is explicitly validated. For a valid nonvalidated target, it emits exactly one warning. Add one target-neutral info line stating that the built-in limiter is unpatched and an external cap must equal the target; do not recommend an `IntervalMode` value.

`FatalTransactionFailure` must log stage/name/index and `rollback_attempted`/`rollback_complete` before calling `ReportFramerateInitializationFailure`. If rollback is incomplete, production termination/fail-fast remains mandatory.

- [ ] **Step 9: Build focused targets and verify GREEN**

```powershell
cmake --build --preset msvc32-release --target FramerateRuntimeTests FrameratePatchPlanTests FrameratePatchTransactionTests FramerateMonitorTests FramerateDiagnosticsTests FramerateProfileTests iDmacDrv32
ctest --test-dir build-msvc32-release --output-on-failure -R '^Framerate(Profile|Monitor|Diagnostics|PatchTransaction|PatchPlan|Runtime)Tests$'
```

Expected: all six framerate tests pass and the production Win32 DLL links.

- [ ] **Step 10: Audit forbidden broad behavior and 120-only assumptions**

```powershell
$forbidden = rg -n "GC120FPS|kFrameMs120|kFrameSeconds120|120HzMultiplier|double_positive_frame_count|enable_120fps_timer_patches|Use IntervalMode=1" src/Patches/Framerate src/Loader src/Config tools/ConfigGUI
if ($LASTEXITCODE -eq 0) {
    $forbidden | Write-Host
    throw '120-only or broad-gating implementation remains'
}
if ($LASTEXITCODE -ne 1) { throw "rg audit failed: $LASTEXITCODE" }

rg -n "target_fps / 60|target_fps_ / 60|GetTargetFps\(\).*60" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) { throw 'integer-ratio truncation remains' }
if ($LASTEXITCODE -ne 1) { throw "ratio audit failed: $LASTEXITCODE" }
```

Expected: neither audit finds a forbidden implementation.

- [ ] **Step 11: Commit the integrated runtime**

```powershell
git add -- src/Patches/Framerate/FrameratePatch.h src/Patches/Framerate/FrameratePatch.cpp tests/Patches/Framerate/FramerateRuntimeTests.cpp src/Loader/DllMain.cpp src/Patches/CMakeLists.txt tests/Patches/CMakeLists.txt
git commit -m "feat: install configurable framerate runtime"
```

---

### Task 8: Run Complete Static Verification and Review the Owned Diff

**Files:**
- Verify: every file changed by Tasks 1-7
- Verify: `build-msvc32-debug/dist/iDmacDrv32.dll`
- Verify: `build-msvc32-release/dist/iDmacDrv32.dll`

**Interfaces:**
- Consumes: The seven independently committed implementation deliverables.
- Produces: Fresh x86 Debug and RelWithDebInfo build/test evidence, mechanical policy audits, and a clean reviewed branch ready for operator runtime acceptance.

- [ ] **Step 1: Configure and build both supported x86 presets**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
if ($LASTEXITCODE -ne 0) { throw "Debug build failed: $LASTEXITCODE" }

cmake --preset msvc32-release
cmake --build --preset msvc32-release
if ($LASTEXITCODE -ne 0) { throw "RelWithDebInfo build failed: $LASTEXITCODE" }
```

Expected: ConfigGUI, every test target, and `iDmacDrv32.dll` build for Win32 in both configurations.

- [ ] **Step 2: Run the complete CTest suite in both configurations**

```powershell
ctest --preset msvc32-debug
if ($LASTEXITCODE -ne 0) { throw "Debug CTest failed: $LASTEXITCODE" }

ctest --preset msvc32-release
if ($LASTEXITCODE -ne 0) { throw "RelWithDebInfo CTest failed: $LASTEXITCODE" }
```

Expected: `100% tests passed, 0 tests failed` for both presets.

- [ ] **Step 3: Run configuration, timing-domain, and diagnostics policy audits**

```powershell
$legacy = rg -n "enable_120fps_timer_patches|GC120FPS|full-120|120HzMultiplier|kFrames2SecondsAt120|kPaletteSmoothingCap120" src tools config.toml
if ($LASTEXITCODE -eq 0) {
    $legacy | Write-Host
    throw 'legacy 120-only implementation remains'
}
if ($LASTEXITCODE -ne 1) { throw "legacy audit failed: $LASTEXITCODE" }

$limiter = rg -n "Sleep\(|sleep_for|busy.?wait|create_(inline|mid).*Present|kRva.*Present|Hook.*Present" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $limiter | Write-Host
    throw 'GCLoader limiter or presentation hook was introduced'
}
if ($LASTEXITCODE -ne 1) { throw "limiter audit failed: $LASTEXITCODE" }

$interval = rg -n "IntervalMode = 1" src tools tests
if ($LASTEXITCODE -ne 0) { throw 'conditional IntervalMode message/test is missing' }
$unexpectedInterval = $interval | Where-Object {
    $_ -notmatch 'FramerateDiagnostics\.(cpp|h)' -and
    $_ -notmatch 'FramerateDiagnosticsTests\.cpp'
}
if ($unexpectedInterval) {
    $unexpectedInterval | Write-Host
    throw 'IntervalMode guidance escaped the conditional diagnostics boundary'
}

$sharedSixtyWrite = rg -n "002FBBAC|0x2FBBAC|kRva.*Shared.*60" src/Patches/Framerate
if ($LASTEXITCODE -eq 0) {
    $sharedSixtyWrite | Write-Host
    throw 'shared 60.0 data object is referenced by a write plan'
}
if ($LASTEXITCODE -ne 1) { throw "shared-60 audit failed: $LASTEXITCODE" }
```

Expected: no legacy boolean/labels, no limiter/presentation hook, only conditional diagnostics/tests mention `IntervalMode = 1`, and no write descriptor targets the shared 60.0 RVA.

- [ ] **Step 4: Review the complete implementation history and tree**

```powershell
git diff --check 801d6df..HEAD
if ($LASTEXITCODE -ne 0) { throw 'implementation history has whitespace errors' }

git diff --stat 801d6df..HEAD
git diff --name-status 801d6df..HEAD
git log --oneline --decorate 801d6df..HEAD
git status --short
```

Expected: only the configuration, GUI, framerate, loader, tests, build ownership, sample config, and plan files named above changed; the seven implementation commits are present; the worktree is clean.

- [ ] **Step 5: Record the static-verification boundary honestly**

In the execution handoff, state all of the following:

- Both x86 builds and the complete CTest suite passed, with exact test counts from the command output.
- The loader still relies on an operator-supplied driver/RTSS fixed cap.
- No automated result proves 2D, stage 3D, input, audio, countdown, judgement, or sustained high-rate gameplay behavior.
- The rates remain implementation-supported but not newly gameplay-accepted until Task 9 is completed by an operator.

Do not deploy over `H:\gc\iDmacDrv32.dll` and do not start or terminate `game471.exe` as part of this static task.

---

### Task 9: Perform Operator Runtime Acceptance Without Changing Cap Ownership

**Files:**
- Runtime input: `H:\gc\game471.exe`
- Runtime configuration: `H:\gc\config.toml`
- Runtime log: `H:\gc\loader-log.txt`
- Candidate artifact: `build-msvc32-release/dist/iDmacDrv32.dll`

**Interfaces:**
- Consumes: A statically verified candidate DLL, an operator-controlled driver or RTSS cap, and a stopped game process before deployment.
- Produces: Per-rate observed acceptance evidence; it does not change GCLoader into a limiter or automatically label an untested rate as validated.

- [ ] **Step 1: Deploy the candidate only while the game is stopped**

```powershell
$game = Get-Process game471 -ErrorAction SilentlyContinue
if ($game) { throw 'game471.exe is running; stop it normally before deployment' }

Copy-Item -LiteralPath 'build-msvc32-release\dist\iDmacDrv32.dll' -Destination 'H:\gc\iDmacDrv32.dll' -Force
$hashes = Get-FileHash -Algorithm SHA256 -LiteralPath @(
    'build-msvc32-release\dist\iDmacDrv32.dll',
    'H:\gc\iDmacDrv32.dll')
if ($hashes[0].Hash -ne $hashes[1].Hash) {
    throw 'deployed DLL hash does not match the candidate'
}
$hashes
```

Expected: no running game process and matching SHA-256 hashes. Do not terminate the game or overwrite its DLL while it is running.

- [ ] **Step 2: Verify the native-60 regression**

Set `target_fps = 60`, configure the external cap to 60, start the game normally, and wait for cadence validation. Confirm from `loader-log.txt` that native mode installed only the cadence hook and measured within tolerance. Observe menus, booster input, countdowns, one gameplay song, stage 3D, and audio for native behavior.

Acceptance: no timing-site transformations are logged; cadence validates; all observed behavior remains native.

- [ ] **Step 3: Run the full high-rate matrix at 120, 144, 165, 240, and 360**

For each target, set the same fixed value in `config.toml` and the external driver/RTSS cap, restart the process, and retain the target-specific startup/cadence log. Do not change the configured target while the process is running.

For every rate, observe all of these acceptance points:

| Area | Required observation |
|---|---|
| Startup | Derived target values are correct, no support warning appears, and cadence validates after three matching windows. |
| 2D/UI | Attract, menu, news, notice, and ordinary MovieClip animation retain normal wall-clock speed. |
| Stage 3D | Models/effects remain visible and clip masks advance without disappearing or skipping content. |
| Booster input | Edges remain responsive without missed, duplicated, or stuck inputs. |
| Repeat input | Wall-clock repeat delay remains approximately native. |
| Countdown/duration | Two-second countdowns and overall song duration remain correct. |
| Gameplay sync | Notes, effects, scrolling, and music remain synchronized. |
| Judgement | Millisecond judgement does not tighten or widen with target rate. |
| Audio | No new resync storm, crackling, chopping, or drift appears. |

Acceptance: a rate is described as gameplay-validated only after every row passes at that rate. If the machine cannot sustain a rate, record that as unaccepted rather than weakening the cadence validator.

- [ ] **Step 4: Verify both mismatch-message branches**

Run target 144 with an external 120 cap. Wait for three failed windows and confirm the log/modal report approximately 120 FPS, terminate after dismissal, and contain no `IntervalMode` text.

Then run target 144 while presentation remains approximately 60 FPS. Confirm the same sustained-mismatch abort includes the narrowly conditional `IntervalMode = 1` hint.

Acceptance: the 120 mismatch omits the hint; the approximately-60 mismatch includes it; both log target, measurement, relative error, interval count, failed-window count, and overflow state before the modal.

- [ ] **Step 5: Verify startup-stall robustness and a formula-only rate**

At a matching validated target, introduce one isolated startup stall without changing the sustained cap. Confirm the median-based detector still reaches three matching windows.

Then set `target_fps = 200` with an external 200 cap. Confirm exactly one nonvalidated-value warning, formula-derived timing values, and successful cadence validation. Exercise one menu and one song, but describe the result as an operator observation rather than adding 200 to the explicitly validated set.

- [ ] **Step 6: Close the runtime gate with evidence, not inference**

Capture for each run: configured target, external cap, measured median, validation outcome, warning presence, and the nine behavioral observations above. If any validated target fails, preserve its log, identify the failing domain, and return to the relevant task rather than claiming completion.

No code or documentation commit is required when all observations pass. If implementation changes are needed, use the systematic-debugging and test-driven-development workflows, rerun Tasks 8 and 9, and commit the focused fix separately.

---
