# Switch Gameplay Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a required Arcade/Switch gameplay-input style that applies Switch direction-to-button and one-cardinal diagonal semantics only inside Groove Coaster gameplay judgment, for both keyboard and gamepad mappings.

**Architecture:** Keep raw iDmac/FIO input and menu behavior unchanged, then detour the two gameplay query wrappers and mid-hook the post-native diagonal result. Put fixed logical-input rules in a platform-free `SwitchInputPolicy` unit; keep executable addresses, guarded stack access, SafetyHook ownership, counters, logging, signature preflight, and all-or-nothing activation in `SwitchInputPatch`.

**Tech Stack:** C++23, Win32 x86 DLL, SafetyHook v0.6.9, SDL3, reflect-cpp v0.19.0 TOML, ImGui, plog, CMake/Ninja, CTest, existing `build-msvc32-latest` MSVC x86 build.

## Global Constraints

- The supported analysis target is `H:\gc\game471.exe.i64` / `game471.exe` with image base `0x00400000`.
- Add exactly one required top-level setting: `gameplay_input_style = 'Arcade'` or `gameplay_input_style = 'Switch'`.
- Keep `Arcade` as the updated-config default.
- A config missing `gameplay_input_style` must fail parsing under the existing strict upgrade contract; unsupported enum strings must also fail.
- The existing `input_mode` continues to select `Keyboard` or `Gamepad`; `gameplay_input_style` changes post-mapping gameplay semantics for either backend.
- Apply Switch semantics during gameplay only; menus and test mode must retain the native distinction between directions and booster buttons.
- Do not change `InputManager`, `iDmacDrvRegisterRead`, raw iDmac/FIO board words, bindings, polling, chattering windows, judgment timing, or repeat timing.
- Preserve native true results, real booster buttons, exact diagonals, native cardinal matching, unrelated directions, and Arcade behavior.
- Left button logical ID `4` aliases direction IDs `0, 1, 2, 3`; right button logical ID `9` aliases `5, 6, 7, 8`.
- Switch diagonal additions are exactly `1 <- {2, 4}`, `3 <- {2, 6}`, `7 <- {8, 4}`, and `9 <- {8, 6}`.
- Hook `RVA 0x00259640` for gameplay pressed-edge queries, `RVA 0x00259570` for gameplay held-state queries, and `RVA 0x001D32A0` for post-native diagonal matching.
- Validate the exact 16-byte query prefix `55 8B EC 83 EC 18 89 4D EC C6 45 FF 00 8B 4D EC` at both inline-hook entries.
- Validate the exact 9-byte diagonal prefix `0F B6 55 8B 83 FA 01 75 2B` before creating the mid-hook.
- Preflight all three signatures before creating any hook; publish active Switch state only after all three hook objects are valid.
- On any signature or hook-creation failure, destroy every hook created by that attempt, leave active behavior as Arcade, log the exact RVA and failure stage, and continue startup.
- No exception may escape a hook callback; failed guarded diagonal-local reads or writes leave the native match unchanged.
- Log `requested_style` and `active_style` at startup. Count virtual button edges, virtual button holds, and added diagonal matches; directly log only the first occurrence of each behavior.
- Unit tests must not patch a live process.
- Build at least `iDmacDrv32`, `ConfigGUI`, `SwitchInputPolicyTests`, and `ConfigFeatureTests` under the existing x86 MSVC environment, then run the full CTest suite.
- Source, tests, commits, and this plan belong in `H:\gc\artifacts\GCLoader`. `H:\gc\config.toml`, `H:\gc\loader-log.txt`, and deployed binaries are operator/runtime state and must not be committed.
- Preserve the pre-existing untracked `.superpowers/` directory and do not stage it.

---

## Scope Check

This is one coherent feature. Configuration, policy, all three gameplay hooks, and transactional activation must ship together because any subset would create a different and internally inconsistent input style.

The design spec already contains the binary route proof. A fresh daemon-backed check of the same IDB confirmed the plan-critical details omitted from the prose: both query wrappers are `char __thiscall(void* self, int input_device_id, int logical_input_id, int gameplay_frame)`; the mid-hook at `0x005D32A0` sees the native match byte at `EBP-0x75`, normalized chart target at `EBP-0x7C`, and current booster direction at `EBP-0x68`. Re-run IDA only if the executable or these signatures differ during implementation.

## File Structure

- Modify `config.h`: define `GameplayInputStyle`, add the required top-level reflect-cpp field, and expose `ConfigManager::GetGameplayInputStyle()`.
- Modify `GUI_main.cpp`: add the non-experimental Arcade/Switch combo next to Input Mode.
- Modify `tests/ConfigFeatureTests.cpp`: cover both enum values, strict missing/invalid failures, Arcade default, and TOML round-trip.
- Create `SwitchInputPolicy.h`: platform-free public types and query-policy interface.
- Create `SwitchInputPolicy.cpp`: fixed button-alias tables, native-first alias querying, and exact diagonal-component rules.
- Create `tests/SwitchInputPolicyTests.cpp`: exhaustive pure-policy coverage, including native short-circuiting and independent direction edges.
- Create `SwitchInputPatch.h`: verified RVAs/signatures, transactional activation helpers, guarded-stack abstraction, and the runtime init declaration.
- Create `SwitchInputPatch.cpp`: SafetyHook callbacks/objects, signature preflight, stack access, counters, first-hit logging, rollback, and startup activation.
- Create `tests/SwitchInputPatchTests.cpp`: synthetic signature, hook-state, and diagonal-local boundary tests without live patching.
- Modify `dllmain.cpp`: initialize the patch only inside the existing game-process branch.
- Modify `CMakeLists.txt`: compile the policy and patch into `iDmacDrv32` and register both new CTest executables.

### Task 1: Required Gameplay Style Config and GUI

**Files:**
- Modify: `config.h:14-17,75-86,162-168`
- Modify: `GUI_main.cpp:318-328`
- Modify: `tests/ConfigFeatureTests.cpp:14-53,87-149,151-253`

**Interfaces:**
- Consumes: existing `InputConfig` reflect-cpp model, strict TOML parsing, and ImGui dirty/save flow.
- Produces:
  - `enum class GameplayInputStyle { Arcade, Switch }`
  - required TOML field `InputConfig::gameplay_input_style`
  - `GameplayInputStyle ConfigManager::GetGameplayInputStyle() const`
  - GUI combo label `Gameplay Input Style` with `Arcade` and `Switch` choices

- [ ] **Step 1: Write failing config and serialization tests**

In `tests/ConfigFeatureTests.cpp`, add the required field to the default fixture immediately after `input_mode`:

```cpp
constexpr const char* kRequiredConfigPrefix = R"toml(
axis_threshold = 16384
gamepad_index = 0
input_mode = 'Keyboard'
gameplay_input_style = 'Arcade'

[gamepad]
p1_axis_horizontal = 'leftx'
p1_axis_vertical = 'lefty'
p1_button1 = 'south'
p1_dpad_down = 'dpad_down'
p1_dpad_left = 'dpad_left'
p1_dpad_right = 'dpad_right'
p1_dpad_up = 'dpad_up'
p2_axis_horizontal = 'rightx'
p2_axis_vertical = 'righty'
p2_button1 = 'east'
p2_button_down = 'invalid'
p2_button_left = 'invalid'
p2_button_right = 'invalid'
p2_button_up = 'invalid'

[keyboard]
p1_button1 = 'space'
p1_down = 's'
p1_left = 'a'
p1_right = 'd'
p1_start = '1'
p1_up = 'w'
p2_button1 = 'k'
p2_down = 'down'
p2_left = 'left'
p2_right = 'right'
p2_service = 'f2'
p2_start = '2'
p2_up = 'up'
service1 = 'f1'
service2 = 'i'
service3 = 'p'
test = 't'
)toml";
```

Add these helpers before the anonymous namespace closes:

```cpp
int expect_style(
    GameplayInputStyle actual,
    GameplayInputStyle expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " enum value "
              << static_cast<int>(expected) << ", got "
              << static_cast<int>(actual) << "\n";
    return 1;
}

std::string replace_once(
    std::string input,
    std::string_view needle,
    std::string_view replacement) {
    const auto position = input.find(needle);
    if (position == std::string::npos) {
        std::cerr << "Test fixture did not contain '" << needle << "'\n";
        std::exit(1);
    }

    input.replace(position, needle.size(), replacement);
    return input;
}
```

Add `#include <string_view>` with the standard-library includes.

After parsing `upgraded_defaults`, assert the updated default:

```cpp
failures += expect_style(
    upgraded_defaults.gameplay_input_style(),
    GameplayInputStyle::Arcade,
    "upgraded default gameplay_input_style");
```

Replace the current custom parse with a Switch fixture and assert it:

```cpp
const auto switch_prefix = replace_once(
    kRequiredConfigPrefix,
    "gameplay_input_style = 'Arcade'",
    "gameplay_input_style = 'Switch'");
const auto custom = parse_config(switch_prefix + kEnabledExperimentalConfig);
failures += expect_style(
    custom.gameplay_input_style(),
    GameplayInputStyle::Switch,
    "custom gameplay_input_style");
```

Add strict missing/invalid and round-trip checks after the existing missing-field cases:

```cpp
const auto valid_arcade_config =
    std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;
failures += expect_parse_failure(
    replace_once(
        valid_arcade_config,
        "gameplay_input_style = 'Arcade'\n",
        ""),
    "missing gameplay_input_style");
failures += expect_parse_failure(
    replace_once(
        valid_arcade_config,
        "gameplay_input_style = 'Arcade'",
        "gameplay_input_style = 'Touch'"),
    "unsupported gameplay_input_style");

const auto serialized_switch = rfl::toml::write(custom);
const auto reparsed_switch = parse_config(serialized_switch);
failures += expect_style(
    reparsed_switch.gameplay_input_style(),
    GameplayInputStyle::Switch,
    "serialized gameplay_input_style");
```

- [ ] **Step 2: Run the focused test and verify the red state**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests'
```

Expected: compilation fails because `GameplayInputStyle` and `InputConfig::gameplay_input_style` do not exist.

- [ ] **Step 3: Add the enum, required field, and getter**

In `config.h`, add the enum after `InputMode`:

```cpp
enum class GameplayInputStyle {
    Arcade,
    Switch
};
```

Add the field immediately after `input_mode` in `InputConfig`:

```cpp
rfl::Rename<"gameplay_input_style", GameplayInputStyle> gameplay_input_style =
    GameplayInputStyle::Arcade;
```

Add this public getter next to `GetInputMode()`:

```cpp
GameplayInputStyle GetGameplayInputStyle() const {
    return config.gameplay_input_style.value();
}
```

Do not wrap the field in `rfl::DefaultIfMissing`. The initializer controls newly constructed GUI state; reflect-cpp must still reject an old TOML document that omits the field.

- [ ] **Step 4: Add the non-experimental GUI combo**

In `GUI_main.cpp`, insert this block immediately after the existing `Input Mode` combo and before gamepad-device controls:

```cpp
const char* gameplay_input_styles[] = {"Arcade", "Switch"};
int current_gameplay_input_style =
    static_cast<int>(g_config.gameplay_input_style());
if (ImGui::Combo(
        "Gameplay Input Style",
        &current_gameplay_input_style,
        gameplay_input_styles,
        IM_ARRAYSIZE(gameplay_input_styles))) {
    g_config.gameplay_input_style =
        static_cast<GameplayInputStyle>(current_gameplay_input_style);
    g_config_dirty = true;
}
```

Do not place this control under the `Experimental` separator.

- [ ] **Step 5: Build and run the focused config test**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests ConfigGUI && ctest --test-dir build-msvc32-latest --output-on-failure -R ConfigFeatureTests'
```

Expected: both targets build; `ConfigFeatureTests` passes and reports one passed test.

- [ ] **Step 6: Commit the config surface**

```powershell
git add -- config.h GUI_main.cpp tests/ConfigFeatureTests.cpp
git commit -m "Add gameplay input style config"
```

### Task 2: Pure Switch Input Policy

**Files:**
- Create: `SwitchInputPolicy.h`
- Create: `SwitchInputPolicy.cpp`
- Create: `tests/SwitchInputPolicyTests.cpp`
- Modify: `CMakeLists.txt:155-178`

**Interfaces:**
- Consumes: logical input IDs and a caller-supplied native query callback.
- Produces:
  - `std::span<const LogicalInputId> DirectionAliasesForButton(LogicalInputId) noexcept`
  - `bool IsSwitchDiagonalComponent(LogicalInputId target, LogicalInputId current) noexcept`
  - `AliasQueryResult QueryButtonWithDirectionAliases(LogicalInputId, void*, LogicalInputQuery) noexcept`
- The policy unit contains no Windows, SDL, SafetyHook, logging, config, or process state.

- [ ] **Step 1: Add the failing policy test target**

Create `tests/SwitchInputPolicyTests.cpp`:

```cpp
#include "SwitchInputPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>

namespace {

using gc::switch_input::LogicalInputId;

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_false(bool actual, const char* name) {
    if (!actual) {
        return 0;
    }
    std::cerr << "Expected false for " << name << "\n";
    return 1;
}

int expect_int(int actual, int expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_ids(
    std::span<const LogicalInputId> actual,
    std::initializer_list<LogicalInputId> expected,
    const char* name) {
    if (actual.size() != expected.size()) {
        std::cerr << "Expected " << name << " size " << expected.size()
                  << ", got " << actual.size() << "\n";
        return 1;
    }

    std::size_t index = 0;
    for (const auto value : expected) {
        if (actual[index] != value) {
            std::cerr << "Expected " << name << "[" << index << "] to be "
                      << value << ", got " << actual[index] << "\n";
            return 1;
        }
        ++index;
    }
    return 0;
}

bool expected_diagonal_component(
    LogicalInputId target,
    LogicalInputId current) {
    switch (target) {
    case 1:
        return current == 2 || current == 4;
    case 3:
        return current == 2 || current == 6;
    case 7:
        return current == 8 || current == 4;
    case 9:
        return current == 8 || current == 6;
    default:
        return false;
    }
}

struct QueryProbe {
    std::array<std::uint8_t, 16> values{};
    std::array<LogicalInputId, 16> calls{};
    std::size_t call_count{0};
};

std::uint8_t probe_query(void* context, LogicalInputId logical_input) noexcept {
    auto& probe = *static_cast<QueryProbe*>(context);
    if (probe.call_count < probe.calls.size()) {
        probe.calls[probe.call_count] = logical_input;
    }
    ++probe.call_count;

    if (logical_input < 0 ||
        static_cast<std::size_t>(logical_input) >= probe.values.size()) {
        return 0;
    }
    return probe.values[static_cast<std::size_t>(logical_input)];
}

} // namespace

int main() {
    using namespace gc::switch_input;
    int failures = 0;

    failures += expect_ids(DirectionAliasesForButton(4), {0, 1, 2, 3}, "left button aliases");
    failures += expect_ids(DirectionAliasesForButton(9), {5, 6, 7, 8}, "right button aliases");
    for (LogicalInputId requested = -1; requested <= 14; ++requested) {
        if (requested != 4 && requested != 9) {
            failures += expect_ids(
                DirectionAliasesForButton(requested),
                {},
                "non-button aliases");
        }
    }

    for (LogicalInputId target = 1; target <= 9; ++target) {
        for (LogicalInputId current = 1; current <= 9; ++current) {
            const bool expected = expected_diagonal_component(target, current);
            const bool actual = IsSwitchDiagonalComponent(target, current);
            failures += expected
                ? expect_true(actual, "accepted diagonal component")
                : expect_false(actual, "rejected diagonal component");
        }
    }
    failures += expect_false(IsSwitchDiagonalComponent(0, 2), "invalid target zero");
    failures += expect_false(IsSwitchDiagonalComponent(1, 10), "invalid current ten");

    QueryProbe native_button{};
    native_button.values[4] = 7;
    native_button.values[0] = 1;
    const auto native_result =
        QueryButtonWithDirectionAliases(4, &native_button, probe_query);
    failures += expect_int(native_result.value, 7, "native button result");
    failures += expect_int(
        native_result.accepted_direction,
        kNoDirectionAlias,
        "native button accepted direction");
    failures += expect_int(
        static_cast<int>(native_button.call_count),
        1,
        "native button query count");
    failures += expect_int(native_button.calls[0], 4, "native button first query");

    QueryProbe ordinary_direction{};
    ordinary_direction.values[2] = 3;
    const auto ordinary_result =
        QueryButtonWithDirectionAliases(2, &ordinary_direction, probe_query);
    failures += expect_int(ordinary_result.value, 3, "ordinary direction result");
    failures += expect_int(
        static_cast<int>(ordinary_direction.call_count),
        1,
        "ordinary direction query count");
    failures += expect_int(ordinary_direction.calls[0], 2, "ordinary direction query");

    QueryProbe first_edge{};
    first_edge.values[0] = 1;
    const auto first_edge_result =
        QueryButtonWithDirectionAliases(4, &first_edge, probe_query);
    failures += expect_int(first_edge_result.value, 1, "first direction edge result");
    failures += expect_int(first_edge_result.accepted_direction, 0, "first direction edge");
    failures += expect_int(
        static_cast<int>(first_edge.call_count),
        2,
        "first direction edge query count");

    QueryProbe second_edge{};
    second_edge.values[1] = 1;
    const auto second_edge_result =
        QueryButtonWithDirectionAliases(4, &second_edge, probe_query);
    failures += expect_int(second_edge_result.value, 1, "second direction edge result");
    failures += expect_int(second_edge_result.accepted_direction, 1, "second direction edge");
    failures += expect_int(
        static_cast<int>(second_edge.call_count),
        3,
        "second direction edge query count");
    failures += expect_int(second_edge.calls[0], 4, "second edge native query");
    failures += expect_int(second_edge.calls[1], 0, "second edge first alias query");
    failures += expect_int(second_edge.calls[2], 1, "second edge independent alias query");

    QueryProbe right_edge{};
    right_edge.values[8] = 1;
    const auto right_edge_result =
        QueryButtonWithDirectionAliases(9, &right_edge, probe_query);
    failures += expect_int(right_edge_result.accepted_direction, 8, "right direction edge");
    failures += expect_int(
        static_cast<int>(right_edge.call_count),
        5,
        "right direction edge query count");

    QueryProbe no_match{};
    const auto no_match_result =
        QueryButtonWithDirectionAliases(4, &no_match, probe_query);
    failures += expect_int(no_match_result.value, 0, "no alias result");
    failures += expect_int(
        no_match_result.accepted_direction,
        kNoDirectionAlias,
        "no alias accepted direction");

    return failures == 0 ? 0 : 1;
}
```

Add this target after `ConfigFeatureTests` in `CMakeLists.txt`:

```cmake
add_executable(SwitchInputPolicyTests
        SwitchInputPolicy.cpp
        tests/SwitchInputPolicyTests.cpp
)
target_include_directories(SwitchInputPolicyTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME SwitchInputPolicyTests COMMAND SwitchInputPolicyTests)
```

- [ ] **Step 2: Run the target and verify the red state**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SwitchInputPolicyTests'
```

Expected: CMake/build fails because `SwitchInputPolicy.h` and `SwitchInputPolicy.cpp` do not exist.

- [ ] **Step 3: Define the pure policy interface**

Create `SwitchInputPolicy.h`:

```cpp
#pragma once

#include <cstdint>
#include <span>

namespace gc::switch_input {

using LogicalInputId = std::int32_t;

inline constexpr LogicalInputId kNoDirectionAlias = -1;

using LogicalInputQuery =
    std::uint8_t (*)(void* context, LogicalInputId logical_input) noexcept;

struct AliasQueryResult {
    std::uint8_t value{0};
    LogicalInputId accepted_direction{kNoDirectionAlias};
};

std::span<const LogicalInputId> DirectionAliasesForButton(
    LogicalInputId requested_input) noexcept;

bool IsSwitchDiagonalComponent(
    LogicalInputId target_direction,
    LogicalInputId current_direction) noexcept;

AliasQueryResult QueryButtonWithDirectionAliases(
    LogicalInputId requested_input,
    void* context,
    LogicalInputQuery query) noexcept;

} // namespace gc::switch_input
```

- [ ] **Step 4: Implement the fixed mappings and native-first query**

Create `SwitchInputPolicy.cpp`:

```cpp
#include "SwitchInputPolicy.h"

#include <array>

namespace gc::switch_input {
namespace {

constexpr std::array<LogicalInputId, 4> kLeftButtonDirections{0, 1, 2, 3};
constexpr std::array<LogicalInputId, 4> kRightButtonDirections{5, 6, 7, 8};

} // namespace

std::span<const LogicalInputId> DirectionAliasesForButton(
    LogicalInputId requested_input) noexcept {
    switch (requested_input) {
    case 4:
        return kLeftButtonDirections;
    case 9:
        return kRightButtonDirections;
    default:
        return {};
    }
}

bool IsSwitchDiagonalComponent(
    LogicalInputId target_direction,
    LogicalInputId current_direction) noexcept {
    switch (target_direction) {
    case 1:
        return current_direction == 2 || current_direction == 4;
    case 3:
        return current_direction == 2 || current_direction == 6;
    case 7:
        return current_direction == 8 || current_direction == 4;
    case 9:
        return current_direction == 8 || current_direction == 6;
    default:
        return false;
    }
}

AliasQueryResult QueryButtonWithDirectionAliases(
    LogicalInputId requested_input,
    void* context,
    LogicalInputQuery query) noexcept {
    if (query == nullptr) {
        return {};
    }

    const auto native_value = query(context, requested_input);
    if (native_value != 0) {
        return {native_value, kNoDirectionAlias};
    }

    for (const auto direction : DirectionAliasesForButton(requested_input)) {
        const auto direction_value = query(context, direction);
        if (direction_value != 0) {
            return {direction_value, direction};
        }
    }

    return {native_value, kNoDirectionAlias};
}

} // namespace gc::switch_input
```

- [ ] **Step 5: Build and run the exhaustive policy test**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SwitchInputPolicyTests && ctest --test-dir build-msvc32-latest --output-on-failure -R SwitchInputPolicyTests'
```

Expected: `SwitchInputPolicyTests` builds and passes. The second-edge probe must show calls `4, 0, 1`, proving directions are queried independently rather than collapsed into one aggregate bit.

- [ ] **Step 6: Commit the pure policy**

```powershell
git add -- SwitchInputPolicy.h SwitchInputPolicy.cpp tests/SwitchInputPolicyTests.cpp CMakeLists.txt
git commit -m "Add Switch gameplay input policy"
```

### Task 3: Hook Boundary Contracts and Synthetic Tests

**Files:**
- Create: `SwitchInputPatch.h`
- Create: `tests/SwitchInputPatchTests.cpp`
- Modify: `CMakeLists.txt:155-196`

**Interfaces:**
- Consumes: `IsSwitchDiagonalComponent()` from Task 2.
- Produces:
  - exact hook RVAs and expected byte arrays
  - `bool ValidateSwitchInputSignatures(const SwitchInputSignatureSpans&, SwitchHookSite*) noexcept`
  - `SwitchPatchState ResolveSwitchPatchState(HookCreationResults) noexcept`
  - `bool TryApplySwitchDiagonalMatch(const StackAccessor&) noexcept`
  - `void SwitchInputPatchInit()` for Task 4 and `dllmain.cpp`

- [ ] **Step 1: Add failing synthetic boundary tests**

Create `tests/SwitchInputPatchTests.cpp`:

```cpp
#include "SwitchInputPatch.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_false(bool actual, const char* name) {
    if (!actual) {
        return 0;
    }
    std::cerr << "Expected false for " << name << "\n";
    return 1;
}

int expect_int(int actual, int expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

struct FakeStack {
    std::uint8_t native_match{0};
    std::int32_t target_direction{0};
    std::int32_t current_direction{0};
    bool fail_reads{false};
    bool fail_writes{false};
    int read_count{0};
    int write_count{0};
};

bool fake_read(
    void* context,
    std::ptrdiff_t offset,
    void* output,
    std::size_t size) noexcept {
    auto& stack = *static_cast<FakeStack*>(context);
    ++stack.read_count;
    if (stack.fail_reads || output == nullptr) {
        return false;
    }

    if (offset == gc::switch_input::kDiagonalNativeMatchOffset &&
        size == sizeof(stack.native_match)) {
        std::memcpy(output, &stack.native_match, size);
        return true;
    }
    if (offset == gc::switch_input::kDiagonalTargetDirectionOffset &&
        size == sizeof(stack.target_direction)) {
        std::memcpy(output, &stack.target_direction, size);
        return true;
    }
    if (offset == gc::switch_input::kDiagonalCurrentDirectionOffset &&
        size == sizeof(stack.current_direction)) {
        std::memcpy(output, &stack.current_direction, size);
        return true;
    }
    return false;
}

bool fake_write(
    void* context,
    std::ptrdiff_t offset,
    const void* input,
    std::size_t size) noexcept {
    auto& stack = *static_cast<FakeStack*>(context);
    ++stack.write_count;
    if (stack.fail_writes || input == nullptr) {
        return false;
    }

    if (offset == gc::switch_input::kDiagonalNativeMatchOffset &&
        size == sizeof(stack.native_match)) {
        std::memcpy(&stack.native_match, input, size);
        return true;
    }
    return false;
}

gc::switch_input::StackAccessor accessor(FakeStack& stack) {
    return {&stack, fake_read, fake_write};
}

} // namespace

int main() {
    using namespace gc::switch_input;
    int failures = 0;

    auto pressed = kGameplayQueryEntrySignature;
    auto held = kGameplayQueryEntrySignature;
    auto diagonal = kDiagonalMatchSignature;
    SwitchHookSite mismatch = SwitchHookSite::None;
    failures += expect_true(
        ValidateSwitchInputSignatures(
            {pressed, held, diagonal},
            &mismatch),
        "all signatures");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::None),
        "no signature mismatch site");

    auto bad_pressed = pressed;
    bad_pressed[0] ^= 0xFF;
    failures += expect_false(
        ValidateSwitchInputSignatures(
            {bad_pressed, held, diagonal},
            &mismatch),
        "pressed signature mismatch");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::PressedEdge),
        "pressed mismatch site");

    auto bad_held = held;
    bad_held[5] ^= 0xFF;
    failures += expect_false(
        ValidateSwitchInputSignatures(
            {pressed, bad_held, diagonal},
            &mismatch),
        "held signature mismatch");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::HeldState),
        "held mismatch site");

    auto bad_diagonal = diagonal;
    bad_diagonal[8] ^= 0xFF;
    failures += expect_false(
        ValidateSwitchInputSignatures(
            {pressed, held, bad_diagonal},
            &mismatch),
        "diagonal signature mismatch");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::DiagonalMatch),
        "diagonal mismatch site");

    failures += expect_true(
        ResolveSwitchPatchState({true, true, true}) ==
            SwitchPatchState::Switch,
        "complete hook set activates Switch");
    failures += expect_true(
        ResolveSwitchPatchState({false, true, true}) ==
            SwitchPatchState::Arcade,
        "pressed creation failure rolls back");
    failures += expect_true(
        ResolveSwitchPatchState({true, false, true}) ==
            SwitchPatchState::Arcade,
        "held creation failure rolls back");
    failures += expect_true(
        ResolveSwitchPatchState({true, true, false}) ==
            SwitchPatchState::Arcade,
        "diagonal creation failure rolls back");

    FakeStack native_success{1, 1, 2};
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(native_success)),
        "native success unchanged");
    failures += expect_int(native_success.native_match, 1, "native success value");
    failures += expect_int(native_success.write_count, 0, "native success writes");

    FakeStack promoted{0, 1, 2};
    failures += expect_true(
        TryApplySwitchDiagonalMatch(accessor(promoted)),
        "adjacent cardinal promoted");
    failures += expect_int(promoted.native_match, 1, "promoted local value");
    failures += expect_int(promoted.write_count, 1, "promoted write count");

    FakeStack unrelated{0, 1, 6};
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(unrelated)),
        "unrelated cardinal unchanged");
    failures += expect_int(unrelated.native_match, 0, "unrelated local value");
    failures += expect_int(unrelated.write_count, 0, "unrelated write count");

    FakeStack invalid_read{0, 1, 2};
    invalid_read.fail_reads = true;
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(invalid_read)),
        "invalid local read");
    failures += expect_int(invalid_read.native_match, 0, "invalid-read local value");
    failures += expect_int(invalid_read.write_count, 0, "invalid-read writes");

    FakeStack invalid_write{0, 9, 8};
    invalid_write.fail_writes = true;
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(invalid_write)),
        "invalid local write");
    failures += expect_int(invalid_write.native_match, 0, "invalid-write local value");
    failures += expect_int(invalid_write.write_count, 1, "invalid-write attempts");

    return failures == 0 ? 0 : 1;
}
```

Add the target after `SwitchInputPolicyTests` in `CMakeLists.txt`:

```cmake
add_executable(SwitchInputPatchTests
        SwitchInputPolicy.cpp
        tests/SwitchInputPatchTests.cpp
)
target_include_directories(SwitchInputPatchTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME SwitchInputPatchTests COMMAND SwitchInputPatchTests)
```

- [ ] **Step 2: Run the target and verify the red state**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SwitchInputPatchTests'
```

Expected: compilation fails because `SwitchInputPatch.h` does not exist.

- [ ] **Step 3: Define the tested patch boundary**

Create `SwitchInputPatch.h`:

```cpp
#pragma once

#include "SwitchInputPolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gc::switch_input {

inline constexpr std::uintptr_t kGameplayPressedQueryRva = 0x00259640;
inline constexpr std::uintptr_t kGameplayHeldQueryRva = 0x00259570;
inline constexpr std::uintptr_t kDiagonalMatchRva = 0x001D32A0;

inline constexpr std::array<std::uint8_t, 16> kGameplayQueryEntrySignature{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x89, 0x4D,
    0xEC, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xEC,
};

inline constexpr std::array<std::uint8_t, 9> kDiagonalMatchSignature{
    0x0F, 0xB6, 0x55, 0x8B, 0x83, 0xFA, 0x01, 0x75, 0x2B,
};

inline constexpr std::ptrdiff_t kDiagonalNativeMatchOffset = -0x75;
inline constexpr std::ptrdiff_t kDiagonalTargetDirectionOffset = -0x7C;
inline constexpr std::ptrdiff_t kDiagonalCurrentDirectionOffset = -0x68;

enum class SwitchHookSite {
    None,
    PressedEdge,
    HeldState,
    DiagonalMatch,
};

constexpr std::uintptr_t RvaForHookSite(SwitchHookSite site) noexcept {
    switch (site) {
    case SwitchHookSite::PressedEdge:
        return kGameplayPressedQueryRva;
    case SwitchHookSite::HeldState:
        return kGameplayHeldQueryRva;
    case SwitchHookSite::DiagonalMatch:
        return kDiagonalMatchRva;
    case SwitchHookSite::None:
        return 0;
    }
    return 0;
}

constexpr const char* HookSiteName(SwitchHookSite site) noexcept {
    switch (site) {
    case SwitchHookSite::PressedEdge:
        return "pressed_edge";
    case SwitchHookSite::HeldState:
        return "held_state";
    case SwitchHookSite::DiagonalMatch:
        return "diagonal_match";
    case SwitchHookSite::None:
        return "none";
    }
    return "unknown";
}

struct SwitchInputSignatureSpans {
    std::span<const std::uint8_t> pressed_edge;
    std::span<const std::uint8_t> held_state;
    std::span<const std::uint8_t> diagonal_match;
};

inline bool HasExpectedPrefix(
    std::span<const std::uint8_t> actual,
    std::span<const std::uint8_t> expected) noexcept {
    return actual.size() >= expected.size() &&
           std::equal(expected.begin(), expected.end(), actual.begin());
}

inline bool ValidateSwitchInputSignatures(
    const SwitchInputSignatureSpans& signatures,
    SwitchHookSite* mismatch) noexcept {
    if (mismatch != nullptr) {
        *mismatch = SwitchHookSite::None;
    }

    if (!HasExpectedPrefix(
            signatures.pressed_edge,
            kGameplayQueryEntrySignature)) {
        if (mismatch != nullptr) {
            *mismatch = SwitchHookSite::PressedEdge;
        }
        return false;
    }
    if (!HasExpectedPrefix(
            signatures.held_state,
            kGameplayQueryEntrySignature)) {
        if (mismatch != nullptr) {
            *mismatch = SwitchHookSite::HeldState;
        }
        return false;
    }
    if (!HasExpectedPrefix(
            signatures.diagonal_match,
            kDiagonalMatchSignature)) {
        if (mismatch != nullptr) {
            *mismatch = SwitchHookSite::DiagonalMatch;
        }
        return false;
    }
    return true;
}

enum class SwitchPatchState {
    Arcade,
    Switch,
};

struct HookCreationResults {
    bool pressed_edge{false};
    bool held_state{false};
    bool diagonal_match{false};
};

constexpr SwitchPatchState ResolveSwitchPatchState(
    HookCreationResults hooks) noexcept {
    return hooks.pressed_edge && hooks.held_state && hooks.diagonal_match
        ? SwitchPatchState::Switch
        : SwitchPatchState::Arcade;
}

using StackRead = bool (*)(
    void* context,
    std::ptrdiff_t offset,
    void* output,
    std::size_t size) noexcept;

using StackWrite = bool (*)(
    void* context,
    std::ptrdiff_t offset,
    const void* input,
    std::size_t size) noexcept;

struct StackAccessor {
    void* context{nullptr};
    StackRead read{nullptr};
    StackWrite write{nullptr};
};

inline bool TryApplySwitchDiagonalMatch(
    const StackAccessor& stack) noexcept {
    if (stack.read == nullptr || stack.write == nullptr) {
        return false;
    }

    std::uint8_t native_match = 0;
    if (!stack.read(
            stack.context,
            kDiagonalNativeMatchOffset,
            &native_match,
            sizeof(native_match)) ||
        native_match != 0) {
        return false;
    }

    LogicalInputId target_direction = 0;
    LogicalInputId current_direction = 0;
    if (!stack.read(
            stack.context,
            kDiagonalTargetDirectionOffset,
            &target_direction,
            sizeof(target_direction)) ||
        !stack.read(
            stack.context,
            kDiagonalCurrentDirectionOffset,
            &current_direction,
            sizeof(current_direction)) ||
        !IsSwitchDiagonalComponent(target_direction, current_direction)) {
        return false;
    }

    constexpr std::uint8_t kMatched = 1;
    return stack.write(
        stack.context,
        kDiagonalNativeMatchOffset,
        &kMatched,
        sizeof(kMatched));
}

void SwitchInputPatchInit();

} // namespace gc::switch_input
```

- [ ] **Step 4: Build and run the synthetic boundary tests**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SwitchInputPatchTests && ctest --test-dir build-msvc32-latest --output-on-failure -R SwitchInputPatchTests'
```

Expected: `SwitchInputPatchTests` passes. Each one-site signature mutation resolves to its exact site, every incomplete hook set resolves to Arcade, native true is never rewritten, and synthetic read/write failures leave the local false.

- [ ] **Step 5: Commit the tested patch contract**

```powershell
git add -- SwitchInputPatch.h tests/SwitchInputPatchTests.cpp CMakeLists.txt
git commit -m "Add Switch input hook boundary contracts"
```

### Task 4: Transactional Gameplay Hooks and Game-Process Initialization

**Files:**
- Create: `SwitchInputPatch.cpp`
- Modify: `dllmain.cpp:11-16,86-94`
- Modify: `CMakeLists.txt:105-116`

**Interfaces:**
- Consumes:
  - `ConfigManager::GetGameplayInputStyle()` from Task 1
  - `QueryButtonWithDirectionAliases()` from Task 2
  - RVAs, signatures, activation resolver, and stack helper from Task 3
  - SafetyHook `InlineHook::unsafe_thiscall`, `create_inline`, and `create_mid`
- Produces:
  - gameplay-only inline hooks with ABI `std::uint8_t __fastcall(void*, void*, int, int, int) noexcept`
  - diagonal mid-hook using `EBP-0x75`, `EBP-0x7C`, and `EBP-0x68` through guarded access
  - atomic all-or-nothing `SwitchPatchState` activation and reverse-order rollback
  - three atomic acceptance counters with first-hit-only logs
  - `gc::switch_input::SwitchInputPatchInit()` called only for game-role DLL attach

- [ ] **Step 1: Re-run pure and boundary tests before runtime wiring**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SwitchInputPolicyTests SwitchInputPatchTests && ctest --test-dir build-msvc32-latest --output-on-failure -R "SwitchInputPolicyTests|SwitchInputPatchTests"'
```

Expected: both tests pass before process-specific code is added.

- [ ] **Step 2: Implement the complete runtime patch**

Create `SwitchInputPatch.cpp`:

```cpp
#include "SwitchInputPatch.h"

#include "config.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>

#include <safetyhook.hpp>
#include "plog/Log.h"

namespace gc::switch_input {
namespace {

safetyhook::InlineHook g_pressed_edge_hook{};
safetyhook::InlineHook g_held_state_hook{};
safetyhook::MidHook g_diagonal_match_hook{};

std::atomic<SwitchPatchState> g_active_state{SwitchPatchState::Arcade};
std::atomic_uint64_t g_virtual_button_edges{0};
std::atomic_uint64_t g_virtual_button_holds{0};
std::atomic_uint64_t g_cardinal_diagonal_matches{0};

std::uintptr_t executable_base() noexcept {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

void* rva_pointer(std::uintptr_t base, std::uintptr_t rva) noexcept {
    return reinterpret_cast<void*>(base + rva);
}

const char* requested_style_name(GameplayInputStyle style) noexcept {
    switch (style) {
    case GameplayInputStyle::Arcade:
        return "Arcade";
    case GameplayInputStyle::Switch:
        return "Switch";
    }
    return "Unknown";
}

const char* active_style_name(SwitchPatchState state) noexcept {
    return state == SwitchPatchState::Switch ? "Switch" : "Arcade";
}

void log_install_failure(
    SwitchHookSite site,
    const char* stage) noexcept {
    try {
        PLOG_ERROR << "SwitchInputPatch: install failure stage=" << stage
                   << " site=" << HookSiteName(site)
                   << " rva=0x" << std::hex << RvaForHookSite(site)
                   << std::dec;
    } catch (...) {
    }
}

void log_requested_and_active(GameplayInputStyle requested) noexcept {
    try {
        PLOG_INFO << "SwitchInputPatch: requested_style="
                  << requested_style_name(requested)
                  << " active_style="
                  << active_style_name(
                         g_active_state.load(std::memory_order_acquire));
    } catch (...) {
    }
}

bool read_bytes_safe(
    std::uintptr_t address,
    void* output,
    std::size_t size) noexcept {
    if (address == 0 || output == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool preflight_signatures(std::uintptr_t base) noexcept {
    std::array<std::uint8_t, kGameplayQueryEntrySignature.size()> pressed{};
    std::array<std::uint8_t, kGameplayQueryEntrySignature.size()> held{};
    std::array<std::uint8_t, kDiagonalMatchSignature.size()> diagonal{};

    if (!read_bytes_safe(
            base + kGameplayPressedQueryRva,
            pressed.data(),
            pressed.size())) {
        log_install_failure(SwitchHookSite::PressedEdge, "read_signature");
        return false;
    }
    if (!read_bytes_safe(
            base + kGameplayHeldQueryRva,
            held.data(),
            held.size())) {
        log_install_failure(SwitchHookSite::HeldState, "read_signature");
        return false;
    }
    if (!read_bytes_safe(
            base + kDiagonalMatchRva,
            diagonal.data(),
            diagonal.size())) {
        log_install_failure(SwitchHookSite::DiagonalMatch, "read_signature");
        return false;
    }

    SwitchHookSite mismatch = SwitchHookSite::None;
    if (!ValidateSwitchInputSignatures(
            {pressed, held, diagonal},
            &mismatch)) {
        log_install_failure(mismatch, "validate_signature");
        return false;
    }

    try {
        PLOG_INFO << "SwitchInputPatch: signature preflight passed"
                  << " pressed_rva=0x" << std::hex
                  << kGameplayPressedQueryRva
                  << " held_rva=0x" << kGameplayHeldQueryRva
                  << " diagonal_rva=0x" << kDiagonalMatchRva
                  << std::dec;
    } catch (...) {
    }
    return true;
}

void reset_hooks() noexcept {
    g_active_state.store(SwitchPatchState::Arcade, std::memory_order_release);
    try {
        g_diagonal_match_hook.reset();
    } catch (...) {
    }
    try {
        g_held_state_hook.reset();
    } catch (...) {
    }
    try {
        g_pressed_edge_hook.reset();
    } catch (...) {
    }
}

struct OriginalQueryContext {
    safetyhook::InlineHook* hook;
    void* self;
    int input_device_id;
    int gameplay_frame;
};

std::uint8_t query_original(
    void* opaque_context,
    LogicalInputId logical_input) noexcept {
    auto* context = static_cast<OriginalQueryContext*>(opaque_context);
    if (context == nullptr ||
        context->hook == nullptr ||
        !*context->hook) {
        return 0;
    }

    try {
        return context->hook->unsafe_thiscall<std::uint8_t>(
            context->self,
            context->input_device_id,
            logical_input,
            context->gameplay_frame);
    } catch (...) {
        return 0;
    }
}

void record_first_acceptance(
    std::atomic_uint64_t& counter,
    const char* behavior,
    LogicalInputId requested_input,
    LogicalInputId accepted_direction) noexcept {
    const auto count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count != 1) {
        return;
    }

    try {
        PLOG_INFO << "SwitchInputPatch: first " << behavior
                  << " requested_input=" << requested_input
                  << " accepted_direction=" << accepted_direction
                  << " count=" << count;
    } catch (...) {
    }
}

std::uint8_t query_gameplay_with_aliases(
    safetyhook::InlineHook& hook,
    std::atomic_uint64_t& counter,
    const char* behavior,
    void* self,
    int input_device_id,
    LogicalInputId requested_input,
    int gameplay_frame) noexcept {
    OriginalQueryContext context{
        &hook,
        self,
        input_device_id,
        gameplay_frame,
    };

    if (g_active_state.load(std::memory_order_acquire) !=
        SwitchPatchState::Switch) {
        return query_original(&context, requested_input);
    }

    const auto result = QueryButtonWithDirectionAliases(
        requested_input,
        &context,
        query_original);
    if (result.accepted_direction != kNoDirectionAlias) {
        record_first_acceptance(
            counter,
            behavior,
            requested_input,
            result.accepted_direction);
    }
    return result.value;
}

std::uint8_t __fastcall hook_pressed_edge(
    void* self,
    void*,
    int input_device_id,
    int logical_input,
    int gameplay_frame) noexcept {
    return query_gameplay_with_aliases(
        g_pressed_edge_hook,
        g_virtual_button_edges,
        "virtual_button_edge",
        self,
        input_device_id,
        logical_input,
        gameplay_frame);
}

std::uint8_t __fastcall hook_held_state(
    void* self,
    void*,
    int input_device_id,
    int logical_input,
    int gameplay_frame) noexcept {
    return query_gameplay_with_aliases(
        g_held_state_hook,
        g_virtual_button_holds,
        "virtual_button_hold",
        self,
        input_device_id,
        logical_input,
        gameplay_frame);
}

std::uintptr_t stack_address(
    void* frame_pointer,
    std::ptrdiff_t offset) noexcept {
    return reinterpret_cast<std::uintptr_t>(frame_pointer) +
           static_cast<std::uintptr_t>(offset);
}

bool guarded_stack_read(
    void* context,
    std::ptrdiff_t offset,
    void* output,
    std::size_t size) noexcept {
    if (context == nullptr || output == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(
            output,
            reinterpret_cast<const void*>(stack_address(context, offset)),
            size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guarded_stack_write(
    void* context,
    std::ptrdiff_t offset,
    const void* input,
    std::size_t size) noexcept {
    if (context == nullptr || input == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(
            reinterpret_cast<void*>(stack_address(context, offset)),
            input,
            size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void hook_diagonal_match(safetyhook::Context& context) noexcept {
    if (g_active_state.load(std::memory_order_acquire) !=
        SwitchPatchState::Switch) {
        return;
    }

    try {
        const StackAccessor stack{
            reinterpret_cast<void*>(context.ebp),
            guarded_stack_read,
            guarded_stack_write,
        };
        if (TryApplySwitchDiagonalMatch(stack)) {
            record_first_acceptance(
                g_cardinal_diagonal_matches,
                "cardinal_diagonal_match",
                kNoDirectionAlias,
                kNoDirectionAlias);
        }
    } catch (...) {
    }
}

bool install_hooks_transactionally(std::uintptr_t base) noexcept {
    HookCreationResults created{};
    SwitchHookSite current_site = SwitchHookSite::PressedEdge;
    const char* current_stage = "create_inline";

    try {
        g_pressed_edge_hook = safetyhook::create_inline(
            rva_pointer(base, kGameplayPressedQueryRva),
            reinterpret_cast<void*>(hook_pressed_edge));
        created.pressed_edge = static_cast<bool>(g_pressed_edge_hook);
        if (!created.pressed_edge) {
            log_install_failure(current_site, current_stage);
            reset_hooks();
            return false;
        }

        current_site = SwitchHookSite::HeldState;
        g_held_state_hook = safetyhook::create_inline(
            rva_pointer(base, kGameplayHeldQueryRva),
            reinterpret_cast<void*>(hook_held_state));
        created.held_state = static_cast<bool>(g_held_state_hook);
        if (!created.held_state) {
            log_install_failure(current_site, current_stage);
            reset_hooks();
            return false;
        }

        current_site = SwitchHookSite::DiagonalMatch;
        current_stage = "create_mid";
        g_diagonal_match_hook = safetyhook::create_mid(
            rva_pointer(base, kDiagonalMatchRva),
            hook_diagonal_match);
        created.diagonal_match = static_cast<bool>(g_diagonal_match_hook);
        if (!created.diagonal_match) {
            log_install_failure(current_site, current_stage);
            reset_hooks();
            return false;
        }
    } catch (...) {
        log_install_failure(current_site, current_stage);
        reset_hooks();
        return false;
    }

    const auto resolved_state = ResolveSwitchPatchState(created);
    if (resolved_state != SwitchPatchState::Switch) {
        log_install_failure(current_site, "resolve_complete_hook_set");
        reset_hooks();
        return false;
    }

    g_active_state.store(SwitchPatchState::Switch, std::memory_order_release);
    try {
        PLOG_INFO << "SwitchInputPatch: all hooks active"
                  << " pressed_rva=0x" << std::hex
                  << kGameplayPressedQueryRva
                  << " held_rva=0x" << kGameplayHeldQueryRva
                  << " diagonal_rva=0x" << kDiagonalMatchRva
                  << std::dec;
    } catch (...) {
    }
    return true;
}

} // namespace

void SwitchInputPatchInit() {
    static std::atomic_bool initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    const auto requested =
        ConfigManager::instance().GetGameplayInputStyle();
    g_active_state.store(SwitchPatchState::Arcade, std::memory_order_release);

    if (requested == GameplayInputStyle::Arcade) {
        log_requested_and_active(requested);
        return;
    }

    const auto base = executable_base();
    if (base == 0) {
        log_install_failure(SwitchHookSite::None, "resolve_main_executable");
        log_requested_and_active(requested);
        return;
    }

    if (!preflight_signatures(base)) {
        log_requested_and_active(requested);
        return;
    }

    install_hooks_transactionally(base);
    log_requested_and_active(requested);
}

} // namespace gc::switch_input
```

The inline callbacks use the x86 fastcall shim only to receive `ECX`/the synthetic `EDX` slot; every trampoline call uses SafetyHook's `unsafe_thiscall` with the original three stack arguments. The mid-hook deliberately does not advance `EIP`: it updates `[EBP-0x75]` and lets the verified `movzx/cmp/jnz` sequence execute normally.

- [ ] **Step 3: Add the runtime units to the DLL**

In the `SOURCES` list in `CMakeLists.txt`, add:

```cmake
        SwitchInputPolicy.cpp
        SwitchInputPatch.cpp
```

Keep these files out of `GUI_SOURCES`; the GUI only needs the enum in `config.h`.

- [ ] **Step 4: Initialize only in the game-process branch**

Add this include to `dllmain.cpp`:

```cpp
#include "SwitchInputPatch.h"
```

After `FrameratePatchInit()` inside `ShouldRunGameOnlyInitialization(role)`, add:

```cpp
gc::switch_input::SwitchInputPatchInit();
PLOG_DEBUG << "Switch gameplay input patch init complete!" << std::endl;
```

Do not call it from the service-role branch and do not move it into `InputManager` or `RfidEmuInit()`.

- [ ] **Step 5: Build the DLL and all focused targets**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI SwitchInputPolicyTests SwitchInputPatchTests ConfigFeatureTests'
```

Expected: all five targets build. In particular, `iDmacDrv32.dll` links SafetyHook with both new runtime sources, while the policy and patch tests remain live-process-free.

- [ ] **Step 6: Run focused tests**

Run:

```powershell
ctest --test-dir build-msvc32-latest --output-on-failure -R "SwitchInputPolicyTests|SwitchInputPatchTests|ConfigFeatureTests"
```

Expected: all three selected tests pass.

- [ ] **Step 7: Commit the runtime patch**

```powershell
git add -- SwitchInputPatch.cpp dllmain.cpp CMakeLists.txt
git commit -m "Add transactional Switch gameplay input hooks"
```

### Task 5: Full Verification, Runtime Config Upgrade, and Acceptance

**Files:**
- Runtime input: `H:\gc\config.toml`
- Runtime output: `H:\gc\loader-log.txt`
- Build output: `H:\gc\artifacts\GCLoader\build-msvc32-latest\iDmacDrv32.dll`
- Build output: `H:\gc\artifacts\GCLoader\build-msvc32-latest\ConfigGUI.exe`

**Interfaces:**
- Consumes: all committed implementation tasks.
- Produces: full static evidence plus separate Arcade, Switch-keyboard, Switch-gamepad, and transactional-failure runtime evidence.

- [ ] **Step 1: Build every runtime and test target**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI CountdownTimerFreezeTests ConfigFeatureTests NesysServicePatchTests TestModeStorageRedirectTests SwitchInputPolicyTests SwitchInputPatchTests'
```

Expected: every listed target builds successfully under the x86 compiler initialized by `vcvars32.bat`.

- [ ] **Step 2: Run the full CTest suite**

Run:

```powershell
ctest --test-dir build-msvc32-latest --output-on-failure
```

Expected: CTest reports all configured tests passed, including `SwitchInputPolicyTests`, `SwitchInputPatchTests`, and `ConfigFeatureTests`.

- [ ] **Step 3: Confirm repository scope before deployment**

Run:

```powershell
git status --short
git diff --check
```

Expected: no whitespace errors; only intentional source/plan state is present. `.superpowers/` remains untracked and unstaged. Do not add `H:\gc\config.toml`, logs, DLLs, EXEs, PDBs, or deployment backups to Git.

- [ ] **Step 4: Upgrade runtime config to the safe default**

In `H:\gc\config.toml`, add the required top-level field immediately after `input_mode`:

```toml
input_mode = 'Keyboard'
gameplay_input_style = 'Arcade'
```

Keep every existing keyboard, gamepad, and experimental value unchanged. Launch `ConfigGUI.exe` once, verify the combo shows `Arcade`, switch it to `Switch`, save, reopen, and verify the selection round-trips. Set it back to `Arcade` before the Arcade acceptance run.

- [ ] **Step 5: Deploy the verified DLL**

Close Groove Coaster and `NesysService.exe` so the DLL is not locked, then run:

```powershell
Copy-Item -LiteralPath 'H:\gc\artifacts\GCLoader\build-msvc32-latest\iDmacDrv32.dll' -Destination 'H:\gc\iDmacDrv32.dll' -Force
```

Expected: the copy succeeds and `H:\gc\iDmacDrv32.dll` has the build output's size and a current write timestamp.

- [ ] **Step 6: Run Arcade acceptance with both backends**

With `gameplay_input_style = 'Arcade'`, run once with `input_mode = 'Keyboard'` and once with `input_mode = 'Gamepad'`.

Expected log line in each run:

```text
SwitchInputPatch: requested_style=Arcade active_style=Arcade
```

Expected absence: no `signature preflight passed`, no `all hooks active`, and no first-acceptance counter logs.

In each backend, verify native booster buttons, cardinal directions, exact diagonals, sustained notes, and menu/test-mode controls behave exactly as before. A direction alone must not hit a booster-button note, and one cardinal alone must not satisfy a diagonal chart target.

- [ ] **Step 7: Run Switch keyboard acceptance**

Set:

```toml
input_mode = 'Keyboard'
gameplay_input_style = 'Switch'
```

Launch and verify these startup lines:

```text
SwitchInputPatch: signature preflight passed
SwitchInputPatch: all hooks active
SwitchInputPatch: requested_style=Switch active_style=Switch
```

Verify all of the following in gameplay:

1. Each of the four direction edges on either booster hits that booster's button note.
2. Hold one direction, then newly press a second direction on the same booster; the second edge produces another button hit.
3. A held direction sustains the same-booster button judgment.
4. Target `1` accepts current `2` or `4`; target `3` accepts `2` or `6`; target `7` accepts `8` or `4`; target `9` accepts `8` or `6`.
5. Opposite/unrelated cardinals, neutral, and another diagonal do not gain matches.
6. Exact physical diagonals, native cardinal matches, and real booster buttons still work.
7. Initial and continuation judgments use the same one-component diagonal rule.
8. Menu directions do not become booster-button confirmation and test mode remains native.

Expected first-use log families, each appearing at most once per process:

```text
SwitchInputPatch: first virtual_button_edge
SwitchInputPatch: first virtual_button_hold
SwitchInputPatch: first cardinal_diagonal_match
```

- [ ] **Step 8: Run Switch gamepad acceptance**

Set:

```toml
input_mode = 'Gamepad'
gameplay_input_style = 'Switch'
```

Repeat the eight checks from Step 7 using the configured gamepad directions/buttons. Expected: identical gameplay semantics and the same three first-use log families, proving the patch operates after backend mapping rather than inside keyboard/gamepad polling.

- [ ] **Step 9: Exercise runtime signature-failure rollback safely**

Use a Win32 debugger with the RelWithDebInfo PDB and set a source breakpoint in `dllmain.cpp` immediately before `SwitchInputPatchInit()`. With `gameplay_input_style = 'Switch'`:

1. At the breakpoint, change only the in-memory byte at `game471.exe+0x001D32A0` from `0x0F` to `0x90`.
2. Step over `SwitchInputPatchInit()`.
3. Before resuming game execution, restore that byte from `0x90` to `0x0F`.
4. Continue startup and play a short native input check.

Expected log evidence:

```text
SwitchInputPatch: install failure stage=validate_signature site=diagonal_match rva=0x1d32a0
SwitchInputPatch: requested_style=Switch active_style=Arcade
```

Expected absence: no `all hooks active` and no first-acceptance logs. Native gameplay and menus remain usable after restoring the byte. This validates failure fallback without modifying `game471.exe` on disk.

- [ ] **Step 10: Restore operator state and record the final boundary**

Return `H:\gc\config.toml` to the safe updated default:

```toml
gameplay_input_style = 'Arcade'
```

Run:

```powershell
git status --short
```

Expected: runtime config/log/DLL state does not appear in the repository status; `.superpowers/` is still untouched. No final repository commit is needed because this task changes only operator/runtime state.

## Self-Review

- Spec coverage: Task 1 covers the required enum, strict config upgrade, Arcade default, GUI placement, both parse values, invalid/missing failures, and serialization. Task 2 covers every button alias, every diagonal target/current pair, native short-circuiting, and independent direction edges. Tasks 3-4 cover exact signatures/RVAs, both inline ABIs, guarded diagonal locals, all-or-nothing creation, reverse rollback, callbacks, counters, first-hit logs, and game-only initialization. Task 5 covers required builds, full CTest, both backends, Arcade/Switch behavior, menus, full note lifecycle, and deliberate failure fallback.
- Non-goal coverage: no task changes `InputManager`, `iDmacDrvRegisterRead`, bindings, raw board words, menus, test mode, timing, chart data, UI/assets, or unknown executable support.
- Placeholder scan: every created interface, callback ABI, RVA, byte sequence, stack offset, command, config edit, expected failure, and acceptance check is explicit.
- Type consistency: `LogicalInputId`, `LogicalInputQuery`, `AliasQueryResult`, `SwitchHookSite`, `HookCreationResults`, `SwitchPatchState`, `StackAccessor`, `GameplayInputStyle`, and `SwitchInputPatchInit()` keep the same names and signatures in all producer and consumer tasks.
