# Test-Mode Timing Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an always-on, signature-guarded `TIMING SETTINGS` child form to Groove Coaster test mode so operators can stage, atomically save, and apply `GameTimeOffset` and `JudgTimeOffset` without restarting the game.

**Architecture:** Build a separate `gc_test_mode_timing` feature with a pure staged-value model, a byte-preserving Win32 `system.cfg` store, a checked x86 game-ABI adapter, and a SafetyHook-backed native carrier form. Reuse a `CTestModeForm_SoundTest` instance only for its native allocation/layout/lifecycle, copy its 13-entry vtable, preserve the deleting destructor and unknown slots, and override the activation, render, Confirm, Back, and Left/Right slots.

**Tech Stack:** C++23, Win32 x86 APIs, SafetyHook, plog, CMake 3.31+, Ninja, CTest, MSVC x86 initialized by `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`, IDA Pro/Hex-Rays through daemon-backed `ida-cli` for static verification.

## Global Constraints

- Work and commit only in `H:\gc\artifacts\GCLoader`; `H:\gc` is runtime/deploy evidence and must not be modified by agent-owned implementation or verification.
- Support only `game471.exe` SHA-256 `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`, preferred image base `0x00400000`.
- Keep the feature always enabled in the game process and absent from the injected NESYS process; do not add a TOML key or ConfigGUI control.
- Add `TIMING SETTINGS` at parent child index 10 and move the implicit null `EXIT` row to index 11; entries 0 through 9 must retain their indices and behavior.
- Expose exactly four carrier rows: `MUSIC OFFSET`, `JUDGE OFFSET`, `SAVE AND BACK`, and `CANCEL`.
- Adjust in 1 ms steps, use native input repeat, render an explicit sign including `+0 ms`, and clamp staged values to -50 through +50 ms.
- Do not live-preview unsaved values. Save both values together; Cancel and Test/Back must leave disk and live state unchanged.
- Treat `data\system.cfg` as opaque bytes. Replace only the two active signed-decimal tokens and preserve Shift-JIS bytes, tabs, ordering, comments, BOM state, and line endings.
- Persist through a same-directory temporary file, full write, flush, close, and `ReplaceFileW(..., REPLACEFILE_WRITE_THROUGH, ...)`; never truncate the live file in place.
- Apply changed staged values only after persistence succeeds: after an atomic replacement, or after the store verifies that both disk tokens already equal the staged values. Then write both globals, call the timing manager accessor, call GameTime setter, and call JudgTime setter.
- Keep native encrypted test-mode storage (`gc_test_mode_storage` and the 364-byte `SystemSetting` record) completely unchanged.
- Every executable-image read/write/hook must be preflighted. A preflight mismatch mutates nothing; a partial feature installation rolls back its own hooks and row-count write in reverse order.
- Preserve Sound Test vtable slot 3, the native scalar deleting destructor. Preserve all other unoverridden slots byte-for-byte.
- Every x86 callback is `noexcept`, catches internal C++ failures, and never unwinds through game code.
- Automated build/static checks are agent-owned evidence only. Final in-game acceptance belongs to the user.

---

## Pre-Resolved Binary Contract

The following constants were revalidated through the existing daemon-backed `H:\gc\game471.exe.i64` database while writing this plan. They are final implementation constants, not implementation-time discovery work.

### Patch and hook sites

| Contract | RVA | Expected bytes | Replacement/behavior |
|---|---:|---|---|
| Main row count | `0x173ED5` | `6A 0B` | `6A 0C` |
| Main constructor hook | `0x173EA0` | `55 8B EC 6A FF 68 A7 9A 67 00 64 A1 00 00 00 00` | SafetyHook inline |
| Main render hook | `0x173C60` | `55 8B EC 81 EC 9C 00 00 00 A1 94 93 77 00 33 C5` | SafetyHook inline |

### Native ABI functions

| Function | RVA | First 16 bytes |
|---|---:|---|
| Sound Test constructor | `0x16AE80` | `55 8B EC 6A FF 68 97 71 67 00 64 A1 00 00 00 00` |
| Game allocator | `0x23BD20` | `55 8B EC 8B 45 08 50 E8 94 FE FF FF 83 C4 04 5D` |
| Game deallocator | `0x23BD00` | `55 8B EC 8B 45 08 50 E8 44 FE FF FF 83 C4 04 5D` |
| Register child | `0x0C2C90` | `55 8B EC 51 89 4D FC 8B 45 FC 8B 48 2C 8B 55 08` |
| Base form update | `0x0C2E40` | `55 8B EC 83 EC 0C 89 4D F4 C7 45 F8 00 00 00 00` |
| Set grid cell text | `0x0C1200` | `55 8B EC 51 89 4D FC 8B 45 FC 8B 4D 08 3B 48 28` |
| Set selection | `0x0C1C00` | `55 8B EC 51 89 4D FC 8B 45 FC 83 78 28 00 75 02` |
| Draw title | `0x176940` | `55 8B EC 83 7D 14 04 75 07 C7 45 14 00 00 00 00` |
| Set title position | `0x176900` | `55 8B EC 8B 45 0C 50 8B 4D 08 51 8B 0D 64 25 7F` |
| Draw help | `0x176920` | `55 8B EC 8B 45 14 50 8B 4D 10 51 8B 55 0C 52` |
| Timing manager accessor | `0x001040` | `55 8B EC 6A FF 68 8E D6 67 00 64 A1 00 00 00 00` |
| JudgTime setter | `0x259310` | `55 8B EC 51 89 4D FC 8B 4D FC E8 B1 7D DA FF 0F` |
| GameTime setter | `0x259350` | `55 8B EC 51 89 4D FC 8B 4D FC E8 71 7D DA FF 0F` |

### Data and object layout

- `JudgTimeOffset`: RVA `0x3D9878`.
- `GameTimeOffset`: RVA `0x3D987C`.
- Sound Test vtable: RVA `0x2FB864`, exactly 13 function entries.
- Sound Test allocation size: `0x1D4` bytes.
- Base form fields: grid window `+0x28`, child pointer array `+0x2C`, logical row count `+0x30`, active child `+0x34`, flags `+0x38`.
- Grid window fields: logical row count `+0x28`, column count `+0x2C`, selection `+0x4C`.
- Main-form auxiliary fields: status window `+0x3C`, help-language record `+0x40`, title-language record `+0x44`.

The native Sound Test vtable targets, expressed as RVAs, are:

```cpp
inline constexpr std::array<std::uint32_t, 13> kSoundVtableTargetRvas{
    0x06AB20, 0x06AB20, 0x00C9B0, 0x04D070, 0x0C2680,
    0x16B0C0, 0x16B440, 0x16B290, 0x16B230, 0x16AD60,
    0x16AC20, 0x16A9A0, 0x0C2F20,
};
```

Slot meanings used by this feature are:

| Slot | Native role | Carrier behavior |
|---:|---|---|
| 2 | activation callback | snapshot live offsets, reset selection/status |
| 3 | scalar deleting destructor | preserve native `sub_44D070` |
| 4 | active/inactive state | preserve native `sub_4C2680` |
| 5 | per-frame update | replace with native base update `sub_4C2E40` |
| 6 | render | render timing title/grid/help |
| 7 | Confirm | save, cancel, or ignore by selected row |
| 8 | Test/Back | cancel and return |
| 9 | native increment/Right | add 1 ms on an offset row |
| 10 | native decrement/Left | subtract 1 ms on an offset row |
| 11 | child return | preserve; carrier has no reachable child |
| 12 | input dispatch | preserve native `sub_4C2F20` and native repeat |

---

## File Structure Map

Create this feature package without moving or refactoring unrelated code:

```text
src/Patches/TestModeTiming/
  CMakeLists.txt                       static-library ownership
  TimingSettingsModel.h               pure staged state and commands
  TimingSettingsModel.cpp             clamping and signed display formatting
  SystemConfigTimingStore.h           parser, atomic-file API, typed failures
  SystemConfigTimingStore.cpp         byte rewrite and Win32 replacement
  TimingSettingsGameAbi.h             RVAs, layouts, function types, transaction
  TimingSettingsGameAbi.cpp           preflight, checked write, rollback, live apply
  TimingSettingsPatch.h               game-process initialization entrypoint
  TimingSettingsPatch.cpp             hooks, carrier callbacks, rendering/orchestration

tests/Patches/TestModeTiming/
  CMakeLists.txt
  TimingSettingsModelTests.cpp
  SystemConfigTimingStoreTests.cpp
  TimingSettingsPatchTests.cpp
```

Modify only these existing composition files:

- `src/Patches/CMakeLists.txt`: add the feature subdirectory.
- `tests/Patches/CMakeLists.txt`: add the matching test subdirectory.
- `src/CMakeLists.txt`: link `gc_test_mode_timing` into `iDmacDrv32`.
- `src/Loader/DllMain.cpp`: initialize the feature in the game-only branch.

---

### Task 1: Staged timing model and signed display formatting

**Files:**
- Create: `src/Patches/TestModeTiming/CMakeLists.txt`
- Create: `src/Patches/TestModeTiming/TimingSettingsModel.h`
- Create: `src/Patches/TestModeTiming/TimingSettingsModel.cpp`
- Create: `tests/Patches/TestModeTiming/CMakeLists.txt`
- Create: `tests/Patches/TestModeTiming/TimingSettingsModelTests.cpp`
- Modify: `src/Patches/CMakeLists.txt:1`
- Modify: `tests/Patches/CMakeLists.txt:1`

**Interfaces:**
- Consumes: no feature-local interface.
- Produces: `TimingOffsets`, `TimingRow`, `SaveStatus`, `TimingCommand`, `TimingSettingsModel`, and `FormatOffsetMs(int)` in namespace `gc::test_mode_timing`.

- [ ] **Step 1: Add the failing model tests and CMake ownership**

Create the parent/subdirectory wiring and test target:

```cmake
# src/Patches/TestModeTiming/CMakeLists.txt
add_library(gc_test_mode_timing STATIC
        TimingSettingsModel.cpp
)
target_include_directories(gc_test_mode_timing PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)
```

```cmake
# tests/Patches/TestModeTiming/CMakeLists.txt
add_executable(TimingSettingsModelTests TimingSettingsModelTests.cpp)
target_link_libraries(TimingSettingsModelTests PRIVATE gc_test_mode_timing)
add_test(NAME TimingSettingsModelTests COMMAND TimingSettingsModelTests)
```

Add `add_subdirectory(TestModeTiming)` to both parent `Patches/CMakeLists.txt` files. Then create a dependency-free executable test using the repository's `Expect` style. Its `main()` must assert all of these exact cases:

```cpp
TimingSettingsModel model;
model.Activate({.game_ms = 75, .judge_ms = -80});
failures += Expect(model.original() == TimingOffsets{75, -80},
                   "activation preserves original live values");
failures += Expect(model.staged() == TimingOffsets{50, -50},
                   "activation clamps staged values");
failures += Expect(model.row() == TimingRow::MusicOffset,
                   "activation selects music");
failures += Expect(model.dirty(), "out-of-range activation is dirty");
failures += Expect(std::string_view{FormatOffsetMs(0).data()} == "+0 ms",
                   "zero has explicit sign");

model.Activate({.game_ms = 0, .judge_ms = -16});
model.SetRow(TimingRow::MusicOffset);
model.AdjustSelected(1);
failures += Expect(model.staged().game_ms == 1 &&
                       model.staged().judge_ms == -16,
                   "music adjustment is isolated");
model.AdjustSelected(-100);
failures += Expect(model.staged().game_ms == -50,
                   "music lower bound saturates");
model.AdjustSelected(200);
failures += Expect(model.staged().game_ms == 50,
                   "music upper bound saturates");

model.SetRow(TimingRow::JudgeOffset);
model.AdjustSelected(1);
failures += Expect(model.staged().judge_ms == -15,
                   "judge changes by one millisecond");
model.SetRow(TimingRow::SaveAndBack);
const auto before_action_adjust = model.staged();
model.AdjustSelected(1);
failures += Expect(model.staged() == before_action_adjust,
                   "action rows ignore adjustment");
failures += Expect(model.Confirm() == TimingCommand::Save,
                   "save row requests save");
model.SetRow(TimingRow::Cancel);
failures += Expect(model.Confirm() == TimingCommand::Cancel,
                   "cancel row requests cancel");
model.SetRow(TimingRow::MusicOffset);
failures += Expect(model.Confirm() == TimingCommand::None,
                   "offset row ignores confirm");
failures += Expect(model.Back() == TimingCommand::Cancel,
                   "back always requests cancel");

model.MarkSaveFailed();
failures += Expect(model.status() == SaveStatus::Failed,
                   "save failure is visible");
model.AdjustSelected(-1);
failures += Expect(model.status() == SaveStatus::Idle,
                   "adjustment clears save failure");
```

- [ ] **Step 2: Run the test target and verify it fails**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target TimingSettingsModelTests'
```

Expected: compilation fails because the model interface and implementation do not exist yet.

- [ ] **Step 3: Implement the minimal pure model**

Define the exact public interface:

```cpp
#pragma once

#include <array>

namespace gc::test_mode_timing {

inline constexpr int kMinimumOffsetMs = -50;
inline constexpr int kMaximumOffsetMs = 50;

struct TimingOffsets {
    int game_ms{};
    int judge_ms{};
    friend bool operator==(const TimingOffsets&, const TimingOffsets&) = default;
};

enum class TimingRow { MusicOffset, JudgeOffset, SaveAndBack, Cancel };
enum class SaveStatus { Idle, Failed, Succeeded };
enum class TimingCommand { None, Save, Cancel };

class TimingSettingsModel {
public:
    void Activate(TimingOffsets live) noexcept;
    void SetRow(TimingRow row) noexcept;
    void AdjustSelected(int delta_ms) noexcept;
    [[nodiscard]] TimingCommand Confirm() const noexcept;
    [[nodiscard]] TimingCommand Back() const noexcept;
    void MarkSaveFailed() noexcept;
    void MarkSaveSucceeded() noexcept;

    [[nodiscard]] TimingOffsets original() const noexcept { return original_; }
    [[nodiscard]] TimingOffsets staged() const noexcept { return staged_; }
    [[nodiscard]] TimingRow row() const noexcept { return row_; }
    [[nodiscard]] SaveStatus status() const noexcept { return status_; }
    [[nodiscard]] bool dirty() const noexcept { return staged_ != original_; }

private:
    TimingOffsets original_{};
    TimingOffsets staged_{};
    TimingRow row_{TimingRow::MusicOffset};
    SaveStatus status_{SaveStatus::Idle};
};

[[nodiscard]] std::array<char, 8> FormatOffsetMs(int value) noexcept;

} // namespace gc::test_mode_timing
```

Implement activation, adjustment, commands, and formatting exactly as follows:

```cpp
void TimingSettingsModel::Activate(TimingOffsets live) noexcept {
    original_ = live;
    staged_ = {
        std::clamp(live.game_ms, kMinimumOffsetMs, kMaximumOffsetMs),
        std::clamp(live.judge_ms, kMinimumOffsetMs, kMaximumOffsetMs),
    };
    row_ = TimingRow::MusicOffset;
    status_ = SaveStatus::Idle;
}

void TimingSettingsModel::SetRow(TimingRow row) noexcept {
    row_ = row;
}

void TimingSettingsModel::AdjustSelected(int delta_ms) noexcept {
    int* value = nullptr;
    if (row_ == TimingRow::MusicOffset) value = &staged_.game_ms;
    if (row_ == TimingRow::JudgeOffset) value = &staged_.judge_ms;
    if (value == nullptr) return;
    const int before = *value;
    const auto candidate = static_cast<long long>(*value) + delta_ms;
    *value = static_cast<int>(std::clamp(
        candidate,
        static_cast<long long>(kMinimumOffsetMs),
        static_cast<long long>(kMaximumOffsetMs)));
    if (*value != before) status_ = SaveStatus::Idle;
}

TimingCommand TimingSettingsModel::Confirm() const noexcept {
    if (row_ == TimingRow::SaveAndBack) return TimingCommand::Save;
    if (row_ == TimingRow::Cancel) return TimingCommand::Cancel;
    return TimingCommand::None;
}

TimingCommand TimingSettingsModel::Back() const noexcept {
    return TimingCommand::Cancel;
}

void TimingSettingsModel::MarkSaveFailed() noexcept {
    status_ = SaveStatus::Failed;
}

void TimingSettingsModel::MarkSaveSucceeded() noexcept {
    status_ = SaveStatus::Succeeded;
}

std::array<char, 8> FormatOffsetMs(int value) noexcept {
    std::array<char, 8> result{};
    std::snprintf(result.data(), result.size(), "%+d ms", value);
    return result;
}
```

Extend the test sequence to adjust a value away from and back to its original value and assert `dirty() == false`. Also assert `FormatOffsetMs(-16)` is `-16 ms`, `FormatOffsetMs(50)` is `+50 ms`, and `MarkSaveSucceeded()` sets `SaveStatus::Succeeded`. Activation and a real value adjustment must reset either terminal status to `Idle`; attempting adjustment on an action row or farther past an already-clamped boundary must not clear a failed status because no staged value changed.

- [ ] **Step 4: Build and run the focused test**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TimingSettingsModelTests && ctest --preset msvc32-debug -R "^TimingSettingsModelTests$"'
```

Expected: one focused test passes.

- [ ] **Step 5: Commit the model slice**

```powershell
git add -- src/Patches/CMakeLists.txt src/Patches/TestModeTiming tests/Patches/CMakeLists.txt tests/Patches/TestModeTiming
git commit -m "feat: add test-mode timing settings model"
```

---

### Task 2: Byte-preserving `system.cfg` assignment rewrite

**Files:**
- Create: `src/Patches/TestModeTiming/SystemConfigTimingStore.h`
- Create: `src/Patches/TestModeTiming/SystemConfigTimingStore.cpp`
- Create: `tests/Patches/TestModeTiming/SystemConfigTimingStoreTests.cpp`
- Modify: `src/Patches/TestModeTiming/CMakeLists.txt`
- Modify: `tests/Patches/TestModeTiming/CMakeLists.txt`

**Interfaces:**
- Consumes: `TimingOffsets` from Task 1.
- Produces: `ConfigKey`, `ConfigEditStage`, `ConfigEditError`, `RewrittenConfig`, and `RewriteTimingAssignments(std::span<const std::uint8_t>, TimingOffsets)`.

- [ ] **Step 1: Add failing pure rewrite tests**

Add `SystemConfigTimingStoreTests` to the two local CMake files. Begin the test executable with a byte fixture that deliberately includes Shift-JIS bytes, CRLF, a block-comment lookalike, a line-comment lookalike, and active assignments:

```cpp
const std::vector<std::uint8_t> input{
    '/', '*', '\r', '\n',
    'G','a','m','e','T','i','m','e','O','f','f','s','e','t',' ','=',' ','9','9','\r','\n',
    '*', '/', '\r', '\n',
    '/', '/', 'J','u','d','g','T','i','m','e','O','f','f','s','e','t','=','9','9','\r','\n',
    0x83, 0x51, 0x83, 0x5B, '\r', '\n',
    'J','u','d','g','T','i','m','e','O','f','f','s','e','t','\t','=',' ','-','1','6','\r','\n',
    'G','a','m','e','T','i','m','e','O','f','f','s','e','t','\t','=',' ','0','\r','\n',
};

const auto rewritten = RewriteTimingAssignments(
    input, {.game_ms = -5, .judge_ms = 12});
failures += Expect(rewritten.has_value(), "valid assignments rewrite");
failures += Expect(rewritten->changed, "different values report changed");
failures += Expect(Contains(rewritten->bytes, "GameTimeOffset\t= -5\r\n"),
                   "game token changes sign and width");
failures += Expect(Contains(rewritten->bytes, "JudgTimeOffset\t= 12\r\n"),
                   "judge token changes sign and width");
failures += Expect(ContainsBytes(rewritten->bytes,
                                {0x83, 0x51, 0x83, 0x5B}),
                   "Shift-JIS bytes are retained");
failures += Expect(Contains(rewritten->bytes, "GameTimeOffset = 99\r\n"),
                   "block-comment assignment is ignored");
failures += Expect(Contains(rewritten->bytes, "//JudgTimeOffset=99\r\n"),
                   "line-comment assignment is ignored");

const auto unchanged = RewriteTimingAssignments(
    input, {.game_ms = 0, .judge_ms = -16});
failures += Expect(unchanged.has_value() && !unchanged->changed &&
                       unchanged->bytes == input,
                   "unchanged rewrite is byte identical");
```

Add independent fixtures asserting exact typed failures for missing GameTime, missing JudgTime, duplicate GameTime, duplicate JudgTime, sign without digits, integer overflow, numeric suffixes such as `12x`, and a key without `=`. Add passing fixtures proving LF-only lines, horizontal whitespace, trailing `//`/`/* ... */` comments, and similarly named keys are preserved. A file whose active tokens are `+0` and `-016` must be semantically unchanged when saving 0 and -16, preserving those token bytes and creating no later temporary file.

Define `Contains` and `ContainsBytes` in the test file as bounded `std::search` helpers over byte spans; they are test-only inspection helpers and must not enter the production API.

- [ ] **Step 2: Build and verify the rewrite tests fail**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SystemConfigTimingStoreTests'
```

Expected: compilation or linkage fails because the rewrite interface is absent.

- [ ] **Step 3: Define the rewrite interface**

Add this public API to `SystemConfigTimingStore.h`:

```cpp
enum class ConfigKey { GameTimeOffset, JudgTimeOffset };
enum class ConfigEditStage { Missing, Duplicate, Malformed };

struct ConfigEditError {
    ConfigEditStage stage{};
    ConfigKey key{};
    friend bool operator==(const ConfigEditError&, const ConfigEditError&) = default;
};

struct RewrittenConfig {
    std::vector<std::uint8_t> bytes;
    bool changed{};
};

[[nodiscard]] std::expected<RewrittenConfig, ConfigEditError>
RewriteTimingAssignments(
    std::span<const std::uint8_t> input,
    TimingOffsets offsets);
```

- [ ] **Step 4: Implement exact active-line token discovery**

Implement a private `FindAssignmentToken` that walks the byte vector line by line while carrying an `in_block_comment` Boolean across lines. For each line it must:

1. Skip ASCII space and tab.
2. Enter/leave `/* ... */` regions without treating their bytes as code.
3. Stop at `//` outside a block comment.
4. Only attempt a key match at the first active token on the line.
5. Require the exact key to be followed by space, tab, or `=`; otherwise treat it as a similarly named key and ignore that line.
6. After the exact key, allow space/tab, require `=`, allow space/tab, accept one optional `+`/`-`, and require at least one ASCII digit.
7. Parse the signed token into `int` with explicit overflow detection; `+` must be accepted even though `std::from_chars` implementations need not accept it.
8. After the digit run, allow space/tab and then only end-of-line, `//`, or `/*`.
9. Return `Duplicate` when a second active assignment for the same key is found.

Use these exact replacement rules:

```cpp
struct AssignmentToken {
    std::size_t begin{};
    std::size_t end{};
    int value{};
};

auto game = FindAssignmentToken(input, "GameTimeOffset",
                                 ConfigKey::GameTimeOffset);
if (!game) return std::unexpected(game.error());
auto judge = FindAssignmentToken(input, "JudgTimeOffset",
                                 ConfigKey::JudgTimeOffset);
if (!judge) return std::unexpected(judge.error());

RewrittenConfig result{{input.begin(), input.end()}, false};
struct Replacement { AssignmentToken token; int value; };
std::array replacements{
    Replacement{*game, offsets.game_ms},
    Replacement{*judge, offsets.judge_ms},
};
std::sort(replacements.begin(), replacements.end(),
          [](const auto& lhs, const auto& rhs) {
              return lhs.token.begin > rhs.token.begin;
          });
for (const auto& replacement : replacements) {
    if (replacement.token.value == replacement.value) continue;
    const auto text = std::to_string(replacement.value);
    const auto first = result.bytes.begin() + replacement.token.begin;
    const auto last = result.bytes.begin() + replacement.token.end;
    result.bytes.erase(first, last);
    result.bytes.insert(result.bytes.begin() + replacement.token.begin,
                        text.begin(), text.end());
    result.changed = true;
}
return result;
```

Do not convert the complete byte vector to Unicode, Shift-JIS, or a normalized newline format.

- [ ] **Step 5: Run the model and rewrite tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TimingSettingsModelTests SystemConfigTimingStoreTests && ctest --preset msvc32-debug -R "TimingSettingsModelTests|SystemConfigTimingStoreTests"'
```

Expected: both tests pass.

- [ ] **Step 6: Commit the byte rewrite slice**

```powershell
git add -- src/Patches/TestModeTiming tests/Patches/TestModeTiming
git commit -m "feat: rewrite timing config bytes safely"
```

---

### Task 3: Atomic Win32 file replacement and typed failure stages

**Files:**
- Modify: `src/Patches/TestModeTiming/SystemConfigTimingStore.h`
- Modify: `src/Patches/TestModeTiming/SystemConfigTimingStore.cpp`
- Modify: `tests/Patches/TestModeTiming/SystemConfigTimingStoreTests.cpp`

**Interfaces:**
- Consumes: `RewriteTimingAssignments` from Task 2.
- Produces: `SystemConfigStage`, `SystemConfigError`, `SaveOutcome`, `Win32FileApi`, `ProductionWin32FileApi()`, and `SystemConfigTimingStore::Save(TimingOffsets)`.

- [ ] **Step 1: Add table-driven atomic-file failure tests**

Extend `SystemConfigTimingStoreTests.cpp` with a fake handle table and `WINAPI`-compatible callback functions. The fake must independently fail `CreateFileW` for read, `GetFileSizeEx`, `ReadFile`, source close, temporary create, `WriteFile`, `FlushFileBuffers`, temporary close, and `ReplaceFileW`. Pair a pre-replace or replace failure with a failing `DeleteFileW` to exercise the secondary cleanup error.

For each injected stage, assert:

```cpp
const auto result = store.Save({.game_ms = 4, .judge_ms = -3});
failures += Expect(!result, "injected file stage fails save");
failures += Expect(result.error().stage == expected_stage,
                   "failure reports exact stage");
failures += Expect(fake.target_bytes == original_bytes,
                   "failure preserves target bytes");
failures += Expect(fake.replace_calls == expected_replace_calls,
                   "replace call count is bounded");
```

Add cases proving:

- A changed save creates a same-directory name shaped as
  `system.cfg.gcloader.<pid>.<attempt>.tmp`, writes all bytes, flushes, closes,
  calls `ReplaceFileW` with `REPLACEFILE_WRITE_THROUGH`, and reports
  `SaveOutcome::Changed`.
- `ERROR_FILE_EXISTS` retries the temporary name with the next attempt.
- A successful partial `WriteFile` advances by the reported byte count and
  loops until complete; a successful zero-byte write fails as `TempWrite`.
- An unchanged save reports `SaveOutcome::Unchanged` and makes zero temporary,
  write, flush, replace, or delete calls.
- A replacement failure attempts cleanup and preserves both the primary and
  cleanup error codes.

- [ ] **Step 2: Run the store test and verify the new assertions fail**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SystemConfigTimingStoreTests && ctest --preset msvc32-debug -R "^SystemConfigTimingStoreTests$"'
```

Expected: failures show the atomic store types and operations are missing.

- [ ] **Step 3: Add the injectable Win32 file API and typed result**

Extend the header with exact Win32-compatible callbacks:

```cpp
enum class SystemConfigStage {
    PathResolution,
    TargetOpen,
    TargetSize,
    TargetRead,
    TargetClose,
    Assignment,
    TempCreate,
    TempWrite,
    TempFlush,
    TempClose,
    Replace,
    Cleanup,
    Internal,
};

struct SystemConfigError {
    SystemConfigStage stage{};
    DWORD win32_error{};
    DWORD cleanup_error{};
    std::optional<ConfigEditError> edit_error{};
};

enum class SaveOutcome { Unchanged, Changed };

struct Win32FileApi {
    decltype(&::CreateFileW) create_file{&::CreateFileW};
    decltype(&::GetFileSizeEx) get_file_size{&::GetFileSizeEx};
    decltype(&::ReadFile) read_file{&::ReadFile};
    decltype(&::WriteFile) write_file{&::WriteFile};
    decltype(&::FlushFileBuffers) flush_file{&::FlushFileBuffers};
    decltype(&::CloseHandle) close_handle{&::CloseHandle};
    decltype(&::ReplaceFileW) replace_file{&::ReplaceFileW};
    decltype(&::DeleteFileW) delete_file{&::DeleteFileW};
    decltype(&::GetLastError) get_last_error{&::GetLastError};
    decltype(&::GetCurrentProcessId) get_process_id{&::GetCurrentProcessId};
};

[[nodiscard]] Win32FileApi ProductionWin32FileApi() noexcept;

class SystemConfigTimingStore {
public:
    explicit SystemConfigTimingStore(
        std::filesystem::path path,
        Win32FileApi api = ProductionWin32FileApi());

    [[nodiscard]] std::expected<SaveOutcome, SystemConfigError>
    Save(TimingOffsets offsets) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
    Win32FileApi api_;
};
```

- [ ] **Step 4: Implement read-all and same-directory replace**

Implement `Save` as a `try`/`catch (...)` boundary. Catch `std::bad_alloc` as `SystemConfigStage::Internal` with `ERROR_OUTOFMEMORY`; catch every other C++ exception as `Internal` with `ERROR_GEN_FAILURE`. The body must execute this exact sequence:

```cpp
auto input = ReadAllBytes(path_, api_);
if (!input) return std::unexpected(input.error());

auto rewritten = RewriteTimingAssignments(*input, offsets);
if (!rewritten) {
    return std::unexpected(SystemConfigError{
        .stage = SystemConfigStage::Assignment,
        .edit_error = rewritten.error(),
    });
}
if (!rewritten->changed) return SaveOutcome::Unchanged;

for (unsigned attempt = 0; attempt != 16; ++attempt) {
    const auto temp = BuildTemporaryPath(
        path_, api_.get_process_id(), attempt);
    HANDLE handle = api_.create_file(
        temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = api_.get_last_error();
        if (error == ERROR_FILE_EXISTS) continue;
        return std::unexpected(SystemConfigError{
            .stage = SystemConfigStage::TempCreate,
            .win32_error = error,
        });
    }

    auto staged = WriteFlushClose(handle, rewritten->bytes, api_);
    if (!staged) {
        const DWORD cleanup = api_.delete_file(temp.c_str())
            ? ERROR_SUCCESS : api_.get_last_error();
        auto error = staged.error();
        error.cleanup_error = cleanup;
        return std::unexpected(error);
    }

    if (!api_.replace_file(path_.c_str(), temp.c_str(), nullptr,
                           REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
        const DWORD primary = api_.get_last_error();
        const DWORD cleanup = api_.delete_file(temp.c_str())
            ? ERROR_SUCCESS : api_.get_last_error();
        return std::unexpected(SystemConfigError{
            .stage = SystemConfigStage::Replace,
            .win32_error = primary,
            .cleanup_error = cleanup,
        });
    }
    return SaveOutcome::Changed;
}
return std::unexpected(SystemConfigError{
    .stage = SystemConfigStage::TempCreate,
    .win32_error = ERROR_FILE_EXISTS,
});
```

`ReadAllBytes` must open with `GENERIC_READ` and `FILE_SHARE_READ`, reject negative sizes or sizes above `DWORD_MAX`, and require a complete `ReadFile` (report a successful short read as `TargetRead`/`ERROR_HANDLE_EOF`). It must close the source on every post-open path, retain the primary size/read error if close also fails, and report a close failure after a successful read as `TargetClose`. `WriteFlushClose` must loop until every byte is written, reject a successful zero-byte write, flush, and close in that order. Capture the primary error before any cleanup call can overwrite `GetLastError`.

- [ ] **Step 5: Run focused store tests including real temporary-directory success**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target SystemConfigTimingStoreTests && ctest --preset msvc32-debug -R "^SystemConfigTimingStoreTests$"'
```

Expected: all parser, injected-failure, cleanup, and real temporary-directory cases pass.

- [ ] **Step 6: Commit the atomic persistence slice**

```powershell
git add -- src/Patches/TestModeTiming/CMakeLists.txt src/Patches/TestModeTiming/SystemConfigTimingStore.* tests/Patches/TestModeTiming/CMakeLists.txt tests/Patches/TestModeTiming/SystemConfigTimingStoreTests.cpp
git commit -m "feat: save timing config atomically"
```

---

### Task 4: Checked game ABI and feature-local install transaction

**Files:**
- Create: `src/Patches/TestModeTiming/TimingSettingsGameAbi.h`
- Create: `src/Patches/TestModeTiming/TimingSettingsGameAbi.cpp`
- Create: `tests/Patches/TestModeTiming/TimingSettingsPatchTests.cpp`
- Modify: `src/Patches/TestModeTiming/CMakeLists.txt`
- Modify: `tests/Patches/TestModeTiming/CMakeLists.txt`

**Interfaces:**
- Consumes: `TimingOffsets` from Task 1.
- Produces: binary RVA/layout constants, `TimingBytePattern`, `TimingByteContract`, `TimingCheckedWrite`, `TimingHookOperation`, `TimingMemoryApi`, `TimingInstallError`, `TimingPatchTransaction`, `TimingGameAbi`, `BuildTimingAbiContracts`, `BuildTimingCheckedWrites`, and `ApplyLiveTiming`.

- [ ] **Step 1: Add failing exact-contract and rollback tests**

Add `TimingSettingsPatchTests` to CMake. In the test, build a fake image large enough for translated test addresses and assert:

```cpp
const auto contracts = BuildTimingAbiContracts(kFakeBase);
failures += Expect(contracts.size() == 15,
                   "all native ABI entry contracts are present");
failures += Expect(FindContract(contracts, 0x173EA0).expected == Pattern({
    0x55,0x8B,0xEC,0x6A,0xFF,0x68,0xA7,0x9A,
    0x67,0x00,0x64,0xA1,0x00,0x00,0x00,0x00}),
    "main constructor signature is exact");
failures += Expect(FindContract(contracts, 0x173C60).expected == Pattern({
    0x55,0x8B,0xEC,0x81,0xEC,0x9C,0x00,0x00,
    0x00,0xA1,0x94,0x93,0x77,0x00,0x33,0xC5}),
    "main render signature is exact");

const auto writes = BuildTimingCheckedWrites(kFakeBase);
failures += Expect(writes.size() == 1 &&
                       writes[0].rva == 0x173ED5 &&
                       writes[0].expected == Pattern({0x6A, 0x0B}) &&
                       writes[0].replacement == Pattern({0x6A, 0x0C}),
                   "row-count write is exact");

const auto vtable = ExpectedSoundVtable(kFakeBase);
failures += Expect(vtable.size() == 13 &&
                       vtable[3] == kFakeBase + 0x04D070 &&
                       vtable[12] == kFakeBase + 0x0C2F20,
                   "native destructor and dispatcher targets are exact");
```

Reuse the fake read/write/hook style from `FrameratePatchTransactionTests.cpp` and assert: every preflight read/mismatch mutates nothing; hook 0 and hook 1 failures leave the row bytes untouched and reset the failed/installed hooks in reverse order; row-write failure restores `6A 0B` defensively and resets both installed hooks in reverse order; explicit rollback after success restores `6A 0B` before resetting render then constructor; a restore failure reports `rollback_complete == false`.

Add an `ApplyLiveTiming` fake that records these exact events:

```text
write GameTimeOffset
write JudgTimeOffset
get timing manager
set GameTime
set JudgTime
```

- [ ] **Step 2: Build and verify the ABI test fails**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TimingSettingsPatchTests'
```

Expected: compilation fails because the ABI and transaction types are missing.

- [ ] **Step 3: Define exact constants, patterns, and x86 function types**

The header must include these fixed constants and calling conventions:

```cpp
inline constexpr std::uintptr_t kPreferredImageBase = 0x00400000;
inline constexpr std::uint32_t kMainConstructorRva = 0x173EA0;
inline constexpr std::uint32_t kMainRenderRva = 0x173C60;
inline constexpr std::uint32_t kMainRowCountRva = 0x173ED5;
inline constexpr std::uint32_t kSoundConstructorRva = 0x16AE80;
inline constexpr std::uint32_t kGameDeallocatorRva = 0x23BD00;
inline constexpr std::uint32_t kGameAllocatorRva = 0x23BD20;
inline constexpr std::uint32_t kRegisterChildRva = 0x0C2C90;
inline constexpr std::uint32_t kBaseUpdateRva = 0x0C2E40;
inline constexpr std::uint32_t kSetCellTextRva = 0x0C1200;
inline constexpr std::uint32_t kSetSelectionRva = 0x0C1C00;
inline constexpr std::uint32_t kDrawTitleRva = 0x176940;
inline constexpr std::uint32_t kSetTitlePositionRva = 0x176900;
inline constexpr std::uint32_t kDrawHelpRva = 0x176920;
inline constexpr std::uint32_t kTimingManagerRva = 0x001040;
inline constexpr std::uint32_t kJudgTimeSetterRva = 0x259310;
inline constexpr std::uint32_t kGameTimeSetterRva = 0x259350;
inline constexpr std::uint32_t kSoundVtableRva = 0x2FB864;
inline constexpr std::uint32_t kJudgTimeOffsetRva = 0x3D9878;
inline constexpr std::uint32_t kGameTimeOffsetRva = 0x3D987C;
inline constexpr std::size_t kSoundFormSize = 0x1D4;
inline constexpr std::size_t kSoundVtableSlots = 13;
inline constexpr std::size_t kFormGridOffset = 0x28;
inline constexpr std::size_t kFormChildrenOffset = 0x2C;
inline constexpr std::size_t kFormRowCountOffset = 0x30;
inline constexpr std::size_t kFormActiveChildOffset = 0x34;
inline constexpr std::size_t kFormFlagsOffset = 0x38;
inline constexpr std::size_t kGridRowCountOffset = 0x28;
inline constexpr std::size_t kGridColumnCountOffset = 0x2C;
inline constexpr std::size_t kGridSelectionOffset = 0x4C;
inline constexpr std::size_t kMainStatusWindowOffset = 0x3C;
inline constexpr std::size_t kMainHelpRecordOffset = 0x40;
inline constexpr std::size_t kMainTitleRecordOffset = 0x44;

using GameAllocateFn = void* (__cdecl*)(std::size_t);
using GameDeallocateFn = int (__cdecl*)(void*);
using SoundConstructorFn = void* (__thiscall*)(void*, void*);
using ScalarDeletingDestructorFn = void* (__thiscall*)(void*, unsigned char);
using RegisterChildFn = void* (__thiscall*)(void*, int, void*);
using BaseUpdateFn = int (__thiscall*)(void*, int, int);
using SetCellTextFn = void* (__thiscall*)(void*, int, int, const unsigned char*);
using SetSelectionFn = int (__thiscall*)(void*, int);
using DrawTitleFn = int (__cdecl*)(const unsigned char*, const unsigned char*,
                                   const unsigned char*, int);
using SetTitlePositionFn = int (__cdecl*)(int, int);
using DrawHelpFn = int (__cdecl*)(const unsigned char*, const unsigned char*,
                                  int, int);
using TimingManagerFn = void* (__cdecl*)();
using TimingSetterFn = int (__thiscall*)(void*, int);
```

Use a maximum 16-byte pattern and encode both hook sites plus every row in the pre-resolved native ABI table above, for 15 entry contracts total. Validate the Sound Test vtable by reading 13 pointers and comparing each to `base + kSoundVtableTargetRvas[index]`, not by comparing unrelocated absolute bytes.

- [ ] **Step 4: Implement the feature-local preflight and transaction**

Follow the proven `FrameratePatchTransaction` algorithm without linking to or including the framerate feature:

1. Validate descriptor sizes and pointers.
2. Preflight all 15 ABI entry contracts, the vtable target list, and the row-count write before any mutation.
3. Install hook operations in constructor-then-render order.
4. Apply the checked row-count write last through an injected `TimingMemoryApi`.
5. On a hook failure, reset the failed hook defensively and reset earlier hooks in reverse order. On a row-write failure, restore the original row bytes defensively, then reset render and constructor.
6. Retain the original row bytes and committed state for explicit rollback.

`ProductionTimingMemoryApi` must use SEH-guarded reads and `VirtualProtect`/`FlushInstructionCache` writes. Return typed stages `InvalidDescriptor`, `PreflightRead`, `PreflightMismatch`, `DirectWrite`, `HookInstall`, and `Rollback` with operation name/index and rollback completeness.

Implement `ApplyLiveTiming` behind an injected action table so the test can verify ordering. The production action table writes both globals at `base + 0x3D987C` and `base + 0x3D9878`, calls `base + 0x001040`, then invokes `base + 0x259350` followed by `base + 0x259310`. Wrap production access in SEH and return `false` on a structured exception.

- [ ] **Step 5: Run all three focused tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TimingSettingsModelTests SystemConfigTimingStoreTests TimingSettingsPatchTests && ctest --preset msvc32-debug -R "TimingSettingsModelTests|SystemConfigTimingStoreTests|TimingSettingsPatchTests"'
```

Expected: three focused tests pass, including every transaction rollback point.

- [ ] **Step 6: Commit the guarded ABI slice**

```powershell
git add -- src/Patches/TestModeTiming/TimingSettingsGameAbi.* src/Patches/TestModeTiming/CMakeLists.txt tests/Patches/TestModeTiming
git commit -m "feat: add guarded timing menu game ABI"
```

---

### Task 5: Native carrier form, parent routing, and rendering

**Files:**
- Create: `src/Patches/TestModeTiming/TimingSettingsPatch.h`
- Create: `src/Patches/TestModeTiming/TimingSettingsPatch.cpp`
- Modify: `src/Patches/TestModeTiming/CMakeLists.txt`
- Modify: `tests/Patches/TestModeTiming/TimingSettingsPatchTests.cpp`

**Interfaces:**
- Consumes: `TimingSettingsModel`, `TimingGameAbi`, and `TimingPatchTransaction`.
- Produces: `MainRenderRoute`, `RouteMainSelection(int)`, `BuildCarrierVtable`, `RenderTimingSettings`, and the two owned SafetyHook callbacks; declares `[[nodiscard]] bool TimingSettingsPatchInit() noexcept` for Task 6.

- [ ] **Step 1: Add failing carrier-vtable, routing, and render tests**

Add these pure assertions before installing a real hook:

```cpp
for (int index = 0; index != 10; ++index) {
    const auto route = RouteMainSelection(index);
    failures += Expect(route.native_selection == index &&
                           !route.draw_timing_help,
                       "native main entries remain identity mapped");
}
failures += Expect(RouteMainSelection(10) == MainRenderRoute{10, true},
                   "timing uses native Exit case then custom help");
failures += Expect(RouteMainSelection(11) == MainRenderRoute{10, false},
                   "moved Exit uses original Exit help case");

const auto native = ExpectedSoundVtable(kFakeBase);
const CarrierCallbacks callbacks{
    .activate = 0x11111111,
    .render = 0x22222222,
    .confirm = 0x33333333,
    .back = 0x44444444,
    .increment = 0x55555555,
    .decrement = 0x66666666,
};
const auto carrier = BuildCarrierVtable(native, callbacks, kFakeBase);
failures += Expect(carrier[2] == callbacks.activate &&
                       carrier[5] == kFakeBase + 0x0C2E40 &&
                       carrier[6] == callbacks.render &&
                       carrier[7] == callbacks.confirm &&
                       carrier[8] == callbacks.back &&
                       carrier[9] == callbacks.increment &&
                       carrier[10] == callbacks.decrement,
                   "only approved behavioral slots change");
for (const auto slot : {0U, 1U, 3U, 4U, 11U, 12U}) {
    failures += Expect(carrier[slot] == native[slot],
                       "native lifecycle and unknown slots are preserved");
}
```

Use a fake `TimingRenderActions` recorder and assert that rendering writes exactly four rows/two columns, produces `+0 ms` and `-16 ms`, draws `TIMING SETTINGS`, selects the help string for each row, and replaces row help with `SAVE FAILED - CHECK loader-log.txt` when the model status is failed.

Use an x86 fake `0x1D4` carrier buffer plus an 80-byte fake grid window to assert `PrepareCarrierLayout` writes form row count 4, active child -1, window row count 4, window selection 0, and never writes outside those characterized fields.

- [ ] **Step 2: Run the patch test and verify the carrier assertions fail**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TimingSettingsPatchTests && ctest --preset msvc32-debug -R "^TimingSettingsPatchTests$"'
```

Expected: the new routing, vtable, layout, and rendering assertions fail to compile or link.

- [ ] **Step 3: Implement pure routing, vtable copying, and layout preparation**

Use these exact definitions:

```cpp
struct MainRenderRoute {
    int native_selection{};
    bool draw_timing_help{};
    friend bool operator==(const MainRenderRoute&, const MainRenderRoute&) = default;
};

constexpr MainRenderRoute RouteMainSelection(int selection) noexcept {
    if (selection == 10) return {10, true};
    if (selection == 11) return {10, false};
    return {selection, false};
}

std::array<std::uintptr_t, 13> BuildCarrierVtable(
    std::span<const std::uintptr_t, 13> native,
    CarrierCallbacks callbacks,
    std::uintptr_t image_base) noexcept {
    std::array<std::uintptr_t, 13> result{};
    std::copy(native.begin(), native.end(), result.begin());
    result[2] = callbacks.activate;
    result[5] = image_base + 0x0C2E40;
    result[6] = callbacks.render;
    result[7] = callbacks.confirm;
    result[8] = callbacks.back;
    result[9] = callbacks.increment;
    result[10] = callbacks.decrement;
    return result;
}
```

`PrepareCarrierLayout` must use byte-offset helpers and `static_assert(sizeof(void*) == 4)`. It writes only carrier `+0x30 = 4`, carrier `+0x34 = -1`, grid `+0x28 = 4`, and grid `+0x4C = 0`; it leaves the nine-row allocations and the native child at logical index 8 owned exactly as the native Sound Test constructor created them.

- [ ] **Step 4: Implement the carrier callbacks and renderer**

Keep one process-lifetime runtime object containing the resolved ABI, model, store pointer, carrier pointer, copied vtable, and two `safetyhook::InlineHook` objects.

Declare x86 callbacks with `__fastcall` so ECX/EDX bridge native `__thiscall`:

```cpp
void* __fastcall CarrierActivate(void* self, void*) noexcept;
void* __fastcall CarrierRender(void* self, void*, int frame, int input) noexcept;
int __fastcall CarrierConfirm(void* self, void*, int frame,
                              int input, int selection) noexcept;
int __fastcall CarrierBack(void* self, void*, int frame, int input) noexcept;
int __fastcall CarrierIncrement(void* self, void*, int frame,
                                int input, int selection) noexcept;
int __fastcall CarrierDecrement(void* self, void*, int frame,
                                int input, int selection) noexcept;
```

`CarrierActivate` verifies `self == runtime.carrier`, reads both live globals, calls `model.Activate`, sets the native grid selection to zero, and returns `self`. At the start of render, read and range-check the grid's `+0x4C` selection and synchronize `model.SetRow`; Confirm and increment/decrement must likewise range-check their native selection argument and set the model row before dispatch. Increment/decrement apply `+1`/`-1` only for rows 0/1; action rows return zero unchanged. Back returns 1 after discarding staged state. Confirm returns zero for offset rows and delegates Save/Cancel behavior to Task 6's commit path.

Render through the native functions already resolved in `TimingGameAbi`. Define the process-lifetime title and byte-string adapter next to the renderer so every call site is concrete:

```cpp
inline constexpr char kTimingTitle[] = "TIMING SETTINGS";
inline constexpr char kTimingMainHelp[] = "EDIT MUSIC/JUDGE OFFSETS";

const unsigned char* GameText(const char* text) noexcept {
    return reinterpret_cast<const unsigned char*>(text);
}

draw_title(GameText(kTimingTitle), GameText(kTimingTitle),
           GameText(kTimingTitle), 4);
set_title_position(4, 2);
set_cell(grid, 0, 0, GameText("MUSIC OFFSET"));
set_cell(grid, 0, 1, GameText(FormatOffsetMs(model.staged().game_ms).data()));
set_cell(grid, 1, 0, GameText("JUDGE OFFSET"));
set_cell(grid, 1, 1, GameText(FormatOffsetMs(model.staged().judge_ms).data()));
set_cell(grid, 2, 0, GameText("SAVE AND BACK"));
set_cell(grid, 2, 1, GameText(""));
set_cell(grid, 3, 0, GameText("CANCEL"));
set_cell(grid, 3, 1, GameText(""));
draw_help(GameText(help), GameText(help), 4, 0);
```

Use row help strings `LEFT/RIGHT: MUSIC OFFSET`, `LEFT/RIGHT: JUDGE OFFSET`, `SAVE VALUES AND RETURN`, and `DISCARD CHANGES AND RETURN`. Override all four with the save-failure message while failed status is active. Catch all C++ exceptions at every callback boundary, log one bounded error, and return zero except Back, which may safely return 1.

- [ ] **Step 5: Implement main constructor and render hooks**

Use these exact x86 bridge signatures:

```cpp
void* __fastcall MainConstructorHook(
    void* self, void*, void* parent) noexcept;
void* __fastcall MainRenderHook(
    void* self, void*, int frame, int input) noexcept;
```

The constructor hook must call the original first with `unsafe_thiscall<void*>`, allocate `0x1D4` bytes through the native allocator, call the Sound Test constructor with the original parent argument, prepare/copy the carrier vtable, register it at child index 10, and set parent grid rows 10/11 to `TIMING SETTINGS`/`EXIT`.

Track carrier ownership explicitly until registration. If allocation succeeds but construction returns null, release the raw block through the checked game deallocator. If construction succeeds but any later pre-registration operation fails, invoke the original slot-3 scalar deleting destructor with delete flag 1. Transfer ownership only after `RegisterChild` succeeds; from that point the parent owns the carrier. If carrier creation fails, restore the main form and grid logical row count to 11, leave row 10 as `EXIT`, set `carrier_ready = false`, and log the runtime failure. Do not dereference or register a null carrier, and add fake allocator/destructor tests proving each pre-registration failure releases exactly once while successful registration is left to the parent.

The render hook must use an RAII selection guard:

```cpp
const int actual = ReadGridSelection(main_form);
const auto route = RouteMainSelection(actual);
SelectionGuard guard{MainGrid(main_form), route.native_selection, actual};
auto* result = runtime.main_render_hook.unsafe_thiscall<void*>(
    main_form, frame, input);
guard.Restore();
if (route.draw_timing_help && runtime.carrier_ready) {
    runtime.abi.draw_help(GameText(kTimingMainHelp),
                          GameText(kTimingMainHelp), 4, 0);
}
return result;
```

When carrier creation degraded the menu to 11 rows, selection 10 remains native Exit and must not draw timing help.

- [ ] **Step 6: Run all focused tests**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TimingSettingsModelTests SystemConfigTimingStoreTests TimingSettingsPatchTests && ctest --preset msvc32-debug -R "TimingSettingsModelTests|SystemConfigTimingStoreTests|TimingSettingsPatchTests"'
```

Expected: all carrier routing, lifecycle preservation, render, and existing focused tests pass.

- [ ] **Step 7: Commit the native carrier slice**

```powershell
git add -- src/Patches/TestModeTiming tests/Patches/TestModeTiming
git commit -m "feat: add native test-mode timing form"
```

---

### Task 6: Save orchestration, hook installation, and loader composition

**Files:**
- Modify: `src/Patches/TestModeTiming/TimingSettingsPatch.h`
- Modify: `src/Patches/TestModeTiming/TimingSettingsPatch.cpp`
- Modify: `src/Patches/TestModeTiming/CMakeLists.txt`
- Modify: `tests/Patches/TestModeTiming/TimingSettingsPatchTests.cpp`
- Modify: `src/Patches/CMakeLists.txt:1`
- Modify: `tests/Patches/CMakeLists.txt:1`
- Modify: `src/CMakeLists.txt:22-55`
- Modify: `src/Loader/DllMain.cpp:8-14,79-106`

**Interfaces:**
- Consumes: all Task 1-5 interfaces.
- Produces: final `[[nodiscard]] bool TimingSettingsPatchInit() noexcept;` game-process entrypoint and complete `gc_test_mode_timing` linkage.

- [ ] **Step 1: Add failing commit-order and initialization tests**

Extend `TimingSettingsPatchTests` with injected commit actions. Assert these exact sequences:

```text
clean model save: mark succeeded -> return 1 (zero store/apply calls)
dirty model, changed file: save -> write globals -> manager -> GameTime -> JudgTime -> mark succeeded -> return 1
dirty model, already-matching file: save -> write globals -> manager -> GameTime -> JudgTime -> mark succeeded -> return 1
save failure: save -> mark failed -> return 0
live apply invariant failure: save -> apply failure -> mark failed/log fatal -> return 0
cancel confirm: no save/apply -> return 1
back: no save/apply -> return 1
```

Also test initialization with fake memory/hooks: two hook operations are created only after every ABI contract matches; a constructor-hook creation failure leaves the row bytes untouched; a render-hook creation failure resets the failed render hook and constructor hook while leaving the row bytes untouched; a row-write failure restores `6A 0B` defensively and resets render then constructor; successful init retains two hook owners and the `6A 0C` row byte.

- [ ] **Step 2: Run the patch test and verify orchestration assertions fail**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target TimingSettingsPatchTests && ctest --preset msvc32-debug -R "^TimingSettingsPatchTests$"'
```

Expected: new save/install sequence assertions fail.

- [ ] **Step 3: Implement explicit Save/Cancel orchestration**

Use an injected `TimingCommitActions` seam in tests and production adapters for the real store/ABI. The Confirm path must be:

```cpp
const auto command = runtime.model.Confirm();
if (command == TimingCommand::None) return 0;
if (command == TimingCommand::Cancel) return 1;

const auto before = runtime.model.original();
const auto staged = runtime.model.staged();
if (!runtime.model.dirty()) {
    runtime.model.MarkSaveSucceeded();
    return 1;
}

const auto saved = runtime.store.Save(staged);
if (!saved) {
    runtime.model.MarkSaveFailed();
    LogSaveFailure(runtime.store.path(), saved.error());
    return 0;
}
if (!ApplyLiveTiming(runtime.abi, staged)) {
    runtime.model.MarkSaveFailed();
    PLOG_FATAL << "TestModeTiming: persisted values but live ABI apply failed";
    return 0;
}
PLOG_INFO << "TestModeTiming: saved GameTimeOffset "
          << before.game_ms << " -> " << staged.game_ms
          << ", JudgTimeOffset " << before.judge_ms
          << " -> " << staged.judge_ms
          << ", config="
          << (*saved == SaveOutcome::Changed ? "replaced" : "already matched");
runtime.model.MarkSaveSucceeded();
return 1;
```

Clean-model Save must not call the store or live setters. A dirty model must call the live setters after either successful store outcome because the disk can already contain the staged values while the current live globals differ. Cancel and Back must not call the store.

- [ ] **Step 4: Implement transactional SafetyHook initialization**

`TimingSettingsPatchInit() noexcept` must catch all internal C++ failures, log the initialization stage, and return `false`. It must:

1. Return the stored result on repeated calls.
2. Resolve `GetModuleHandleW(nullptr)` and require the supported preferred base.
3. Resolve `data\system.cfg` once through `GetFullPathNameW` and construct the store with the absolute path.
4. Build/validate all ABI contracts and the 13 vtable targets.
5. Build the one checked write and two hook operations.
6. Run the feature transaction, which installs constructor then render through `safetyhook::create_inline` and applies the checked row-count write last.
7. Commit the feature-local transaction and publish runtime state only on full success.
8. Log exact install stage/site and rollback completeness on failure.

Add `TimingSettingsPatch.cpp` to `gc_test_mode_timing`, add `${plog_SOURCE_DIR}/include` to that target's private include directories, and link `safetyhook::safetyhook` privately. Keep `TimingSettingsPatch.h` limited to the initialization declaration so SafetyHook does not leak through the public interface.

- [ ] **Step 5: Compose the feature into the game-only loader path**

Add:

```cpp
#include "Patches/TestModeTiming/TimingSettingsPatch.h"
```

At the start of the game-only branch, before WASAPI/RFID/framerate/Switch initialization, add:

```cpp
if (!gc::test_mode_timing::TimingSettingsPatchInit()) {
    PLOG_ERROR << "TestModeTiming: fail-closed DLL attach";
    return FALSE;
}
PLOG_DEBUG << "Test-mode timing settings initialization complete!";
```

Add `gc_test_mode_timing` to `iDmacDrv32`'s private link libraries. Do not add it to the NESYS-process path, ConfigGUI, `gc_test_mode_storage`, or `config.toml`.

- [ ] **Step 6: Build and run the focused feature suite**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build --preset msvc32-debug --target iDmacDrv32 TimingSettingsModelTests SystemConfigTimingStoreTests TimingSettingsPatchTests && ctest --preset msvc32-debug -R "TimingSettingsModelTests|SystemConfigTimingStoreTests|TimingSettingsPatchTests"'
```

Expected: the DLL builds and all three focused tests pass.

- [ ] **Step 7: Commit the composed feature**

```powershell
git add -- src/CMakeLists.txt src/Loader/DllMain.cpp src/Patches/CMakeLists.txt src/Patches/TestModeTiming tests/Patches/CMakeLists.txt tests/Patches/TestModeTiming
git commit -m "feat: enable test-mode timing settings"
```

---

### Task 7: Static binary verification and complete regression gate

**Files:**
- Modify only if verification exposes a defect: the owning Task 1-6 source/test files.
- Do not modify: `H:\gc\game471.exe`, `H:\gc\data\system.cfg`, `H:\gc\iDmacDrv32.dll`, or any other runtime/deploy file.

**Interfaces:**
- Consumes: completed feature and all repository tests.
- Produces: verified debug/release artifacts and a user-owned runtime acceptance handoff; no new production interface.

- [ ] **Step 1: Verify the executable hash**

Run:

```powershell
(Get-FileHash -Algorithm SHA256 'H:\gc\game471.exe').Hash
```

Expected exactly:

```text
FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522
```

- [ ] **Step 2: Revalidate every encoded signature through the existing IDA daemon**

Run from `H:\IDACLI`:

```powershell
$env:IDA_CLI_DAEMON_DIR='C:\Users\10614\.ida-cli\daemons'
@'
from ida_cli.agent_bridge import AgentSession

expected = {
    0x573ED5: bytes.fromhex('6A 0B'),
    0x573EA0: bytes.fromhex('55 8B EC 6A FF 68 A7 9A 67 00 64 A1 00 00 00 00'),
    0x573C60: bytes.fromhex('55 8B EC 81 EC 9C 00 00 00 A1 94 93 77 00 33 C5'),
    0x56AE80: bytes.fromhex('55 8B EC 6A FF 68 97 71 67 00 64 A1 00 00 00 00'),
    0x63BD20: bytes.fromhex('55 8B EC 8B 45 08 50 E8 94 FE FF FF 83 C4 04 5D'),
    0x63BD00: bytes.fromhex('55 8B EC 8B 45 08 50 E8 44 FE FF FF 83 C4 04 5D'),
    0x4C2C90: bytes.fromhex('55 8B EC 51 89 4D FC 8B 45 FC 8B 48 2C 8B 55 08'),
    0x4C2E40: bytes.fromhex('55 8B EC 83 EC 0C 89 4D F4 C7 45 F8 00 00 00 00'),
    0x4C1200: bytes.fromhex('55 8B EC 51 89 4D FC 8B 45 FC 8B 4D 08 3B 48 28'),
    0x4C1C00: bytes.fromhex('55 8B EC 51 89 4D FC 8B 45 FC 83 78 28 00 75 02'),
    0x576940: bytes.fromhex('55 8B EC 83 7D 14 04 75 07 C7 45 14 00 00 00 00'),
    0x576900: bytes.fromhex('55 8B EC 8B 45 0C 50 8B 4D 08 51 8B 0D 64 25 7F'),
    0x576920: bytes.fromhex('55 8B EC 8B 45 14 50 8B 4D 10 51 8B 55 0C 52'),
    0x401040: bytes.fromhex('55 8B EC 6A FF 68 8E D6 67 00 64 A1 00 00 00 00'),
    0x659310: bytes.fromhex('55 8B EC 51 89 4D FC 8B 4D FC E8 B1 7D DA FF 0F'),
    0x659350: bytes.fromhex('55 8B EC 51 89 4D FC 8B 4D FC E8 71 7D DA FF 0F'),
}
expected_vtable = [
    0x46AB20, 0x46AB20, 0x40C9B0, 0x44D070, 0x4C2680,
    0x56B0C0, 0x56B440, 0x56B290, 0x56B230, 0x56AD60,
    0x56AC20, 0x56A9A0, 0x4C2F20,
]
with AgentSession.start(r'H:\gc\game471.exe.i64', daemon=True,
                        require_ida=True, request_timeout_s=120) as ida:
    ida.probe_backend(require_ida=True)
    checks = [(ea, len(data)) for ea, data in expected.items()]
    kernel_code = (
        "import ida_bytes\n"
        f"checks = {checks!r}\n"
        "__result__ = {\n"
        "  'bytes': {hex(ea): ida_bytes.get_bytes(ea, size).hex() "
        "for ea, size in checks},\n"
        "  'vtable': [ida_bytes.get_dword(0x6FB864 + 4 * index) "
        "for index in range(13)],\n"
        "}"
    )
    result = ida.result(kernel_code,
                        request_id='timing.verify.signatures')
    for ea, data in expected.items():
        actual = bytes.fromhex(result['bytes'][hex(ea)])
        assert actual == data, (hex(ea), actual.hex(), data.hex())
    assert result['vtable'] == expected_vtable, result['vtable']
print('timing ABI signatures verified')
'@ | python -
```

Expected: `timing ABI signatures verified`.

- [ ] **Step 3: Run clean debug and release builds**

Run:

```powershell
& $env:ComSpec /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --preset msvc32-debug && cmake --build --preset msvc32-debug --target iDmacDrv32 TimingSettingsModelTests SystemConfigTimingStoreTests TimingSettingsPatchTests && cmake --preset msvc32-release && cmake --build --preset msvc32-release --target iDmacDrv32 TimingSettingsModelTests SystemConfigTimingStoreTests TimingSettingsPatchTests'
```

Expected: both x86 configurations build without warnings promoted to errors.

- [ ] **Step 4: Run focused tests in both configurations**

Run:

```powershell
ctest --preset msvc32-debug -R "TimingSettingsModelTests|SystemConfigTimingStoreTests|TimingSettingsPatchTests"
ctest --preset msvc32-release -R "TimingSettingsModelTests|SystemConfigTimingStoreTests|TimingSettingsPatchTests"
```

Expected: 3/3 focused tests pass in each configuration.

- [ ] **Step 5: Run the complete CTest suite**

Run:

```powershell
ctest --preset msvc32-debug
ctest --preset msvc32-release
```

Expected: 55/55 tests pass in each configuration (the current 52 plus the three new timing tests).

- [ ] **Step 6: Inspect the artifact and repository boundary**

Run:

```powershell
rg -a -n "TIMING SETTINGS|MUSIC OFFSET|JUDGE OFFSET|SAVE FAILED - CHECK loader-log.txt" build-msvc32-release/dist/iDmacDrv32.dll
git diff --check
git status --short
```

Expected: all four bounded strings are present, `git diff --check` is silent, and status contains no runtime/deploy file and no uncommitted implementation file.

- [ ] **Step 7: Hand off user-owned runtime acceptance without deploying**

Provide the built DLL path `H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll` and the approved checklist from the design spec. Explicitly ask the user to validate main-menu/Exit navigation, `+0` and `-16` initial display, 1 ms press/hold behavior, both clamps, Cancel, Test/Back, successful Save without restart, subsequent gameplay timing, restart persistence, byte preservation, and read-only save failure. Do not describe the feature as runtime accepted until the user reports that result.

If verification required a code fix, rerun the owning focused test first, amend or add a narrowly scoped fix commit, then repeat Tasks 7.3 through 7.6.
