# Native Auto Play Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one opt-in, fail-closed GCLoader feature that enables Groove Coaster's native auto-play path, completes HIDDEN/AD-LIB descriptors, suppresses native score/card persistence, and draws a mandatory in-game warning marker.

**Architecture:** Compile one required TOML Boolean into the immutable launch settings, then install a feature-owned transaction only in the game process. The transaction preflights three direct byte sites plus one SafetyHook seam and one native debug-text target, installs the dormant marker hook first, writes no-save and auto-play sites in safety order, and atomically activates the marker only after every write succeeds. A small allocation-free marker producer and structured transaction actions keep native rendering and rollback independently testable without claiming game behavior from synthetic memory.

**Tech Stack:** Windows x86 C++23, MSVC, CMake/Ninja presets, reflect-cpp/TOML, ImGui ConfigGUI, SafetyHook v0.7.0, Win32 guarded executable-memory actions, standalone CTest executables, and the saved IDA-CLI Python scripts.

**Spec:** `docs/superpowers/specs/2026-09-03-native-auto-play-safety-design.md` at approved evidence commit `5924ee541b37fe4c55445a262bd75aabe6443304`.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`. `H:\gc` is runtime and reverse-engineering evidence; do not deploy a DLL, edit `H:\gc\data\expconfig.cfg`, modify either executable, or launch the game as part of implementation.
- Start from source baseline `c787b50d42084c892e794b9301e86765bf923cf2`. Re-read an overlapping file before editing if the branch advances, and preserve any user-owned worktree changes present when execution begins.
- The complete feature is gated only by required `[experimental].enable_auto_play`, whose distributed and default-constructed value is `false`. Accuracy, HIDDEN/AD-LIB handling, input policy, saving, marker text, placement, color, and opacity are not separately configurable.
- A disabled invocation performs no game-module resolution, executable-memory read or write, native-text target publication, or hook operation. An enabled failure aborts game-process DLL attach; partial operation is forbidden.
- Apply the feature only after validated configuration and input-runtime configuration are available in the game branch of `DllMain`, and before `SongUnlockPatchInit` or any later optional gameplay patch. The NESYS branch never calls or receives the auto-play initializer.
- Do not inject gameplay input or block keyboard, controller, or FastIO below the native judgement layer. The native auto-play getter owns judgement/free-tap suppression while menus, pause, service, and exit retain their existing paths.
- Patch only the three direct sites in the frozen contract below. Do not patch grade state `+0xA7`, score formulas, the result exporter directly, server code, the NESYS process, or any persistence protocol.
- Install exactly one SafetyHook mid-hook at the frozen marker seam. Use the native debug-text producer; do not add a D3D font, overlay window, renderer abstraction, or dependency on framerate/windowed-widescreen code.
- Keep the successful render callback allocation-free and free of configuration reads and logging. Preserve SafetyHook context, use `noexcept` boundaries, and contain native-call structured exceptions in a leaf `__try`/`__except` wrapper.
- Preflight all five native contracts before mutation. Install the inactive marker hook first, then write `DoNotSaveCardData`, `+0xA6`, and `+0xA5` in that order. On failure, reset the hook and restore only direct sites written by this invocation, in reverse order.
- A direct site accepts only its exact clean or exact already-patched form. The marker seam and native-text prologue accept only their exact native forms; never signature-scan, chain through, or overwrite an unknown detour.
- Tests use small, independently transcribed fixtures carrying the IDA artifact names and SHA-256 values. They must not scrape production source, duplicate an executable, patch a live process, or claim gameplay, visual, audio, input, or persistence acceptance.
- Run the saved `.codex-tmp\ida_*.py` files by path. Do not embed IDAPython in PowerShell, hold an `AgentSession`, or stop, restart, replace, or otherwise alter the IDA daemon or any other process.
- Run builds from an x86 MSVC Developer PowerShell with `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK`. Debug/Release builds, CTest, IDA artifacts, and DLL inspection are static evidence only.

---

## Scope Check

The spec describes one inseparable safety feature rather than independent products: the two native auto-play getters, no-save policy, marker, configuration, and rollback transaction must either commit together or not run. Separate plans would create unsafe intermediate states, so this plan keeps them in one task sequence while preserving reviewable file boundaries.

## Current-Code Baseline

- `ConfigDocument` is the only mutable TOML-shaped model and is parsed with `rfl::NoExtraFields`; `ConfigCompiler` constructs an immutable, value-owned `ValidatedConfig` launch snapshot.
- `PrepareProcessConfiguration` structurally parses the complete shared document for both process roles, but the NESYS result publishes only logging and NESYS settings. Auto play must remain a game-only consumer and must not be added to `NesysProcessConfiguration`.
- `DllMain.cpp` configures the game input runtime at lines 660-682, then currently starts optional gameplay work with `SongUnlockPatchInit` at lines 684-689. The auto-play gate belongs between those blocks.
- `ProductionGameBinaryPatchActions()` already supplies guarded read/write operations with detailed Win32 memory stages. Auto play reuses those actions but needs its own ten-byte pattern type and transaction because the shared compatibility pattern holds at most five bytes and has no hook rollback.
- `gc_runtime_patches` already links `gc_system_path`, `plog`, and `safetyhook::safetyhook`; add the feature files to that target without creating another generic patch framework.
- The current focused configuration tests are `ConfigContract` and `ConfigStartup`. The current suite deliberately avoids copied full fixtures and source-text assertions.
- The working tree was clean when this plan was written. No runtime deployment or active-process operation is required by any task.

## File and Responsibility Map

| Area | Final responsibility | Files |
|---|---|---|
| Strict document | Required editable Boolean and disabled default | `src/Config/ConfigDocument.h`, `config.toml` |
| Immutable launch settings | Compiler-owned Boolean copied out of the raw document | `src/Config/ConfigCompiler.h`, `src/Config/ConfigCompiler.cpp` |
| ConfigGUI | One checkbox, dirty-state update, and checked-state warning | `tools/ConfigGUI/Main.cpp` |
| Marker producer | Fixed four native-text calls and guarded native `__cdecl` ABI | `src/Patches/AutoPlay/AutoPlayMarker.h`, `src/Patches/AutoPlay/AutoPlayMarker.cpp` |
| Install transaction | Five-site preflight, state classification, hook-first install, ordered writes, ownership-aware rollback, atomic publication | `src/Patches/AutoPlay/AutoPlayPatchTransaction.h`, `src/Patches/AutoPlay/AutoPlayPatchTransaction.cpp` |
| Diagnostics | Stable stage/site names, exact-byte diagnostics, setup modal, and one-shot runtime marker fatal | `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.h`, `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.cpp` |
| Production adapter | Module resolution, shared memory actions, SafetyHook ownership, native-text sink, idempotent public entry | `src/Patches/AutoPlay/AutoPlayPatch.h`, `src/Patches/AutoPlay/AutoPlayPatch.cpp` |
| Composition root | Game-only fail-closed call in required order | `src/Loader/DllMain.cpp` |
| Build registration | Compile production files and focused test executable | `src/Patches/CMakeLists.txt`, `tests/CMakeLists.txt` |
| Configuration proof | Strictness, false/true round trip, immutable ownership, and no collateral serialized changes | `tests/Config/ConfigContractTests.cpp`, `tests/Config/ConfigStartupTests.cpp` |
| Transaction proof | Independent native fixtures, eight state combinations, preflight rejection, ordered mutation, rollback, idempotence, and marker calls | `tests/Patches/AutoPlay/AutoPlayPatchTests.cpp` |

No framerate, input, audio, NESYS, RFID, renderer, widescreen, game-compatibility, or server source file is part of this implementation.

## Frozen Native Contract

All addresses are RVAs from the loaded main executable. The installer accepts a relocated module base and checks both addition and final-byte overflow.

| Site | RVA | Clean/native bytes | Patched bytes |
|---|---:|---|---|
| `do_not_save_card_data` | `0x00269951` | `0F 95 C1` | `B1 01 90` |
| `complete_is_mute` | `0x0003CAFA` | `8A 80 A6 00 00 00` | `B0 01 90 90 90 90` |
| `native_auto_play` | `0x0003CADA` | `8A 80 A5 00 00 00` | `B0 01 90 90 90 90` |
| `marker_seam` | `0x00058BE9` | `8D 44 24 08 50 E8 8D 03 00 00` | SafetyHook-owned |
| `native_debug_text` | `0x00069650` | `55 8B EC 6A FF` | read-only callable target |

The native debug-text target is:

```cpp
using NativeDebugTextFunction = int(__cdecl*)(
    float x,
    float y,
    std::uint32_t argb,
    const char* format,
    ...);
```

The leaf adapter calls it as `function(x, y, argb, "%s", text)`. This matches the saved IDA type/call evidence: two floats, packed color, format pointer, then a native vararg list.

---

### Task 1: Add the strict immutable setting and ConfigGUI warning

**Files:**

- Modify: `src/Config/ConfigDocument.h:31-62`
- Modify: `src/Config/ConfigCompiler.h:48-101`
- Modify: `src/Config/ConfigCompiler.cpp:337-362,906-962`
- Modify: `config.toml:67-81`
- Modify: `tools/ConfigGUI/Main.cpp:1727-1795`
- Modify: `tests/Config/ConfigContractTests.cpp:42-117,626-735`
- Modify: `tests/Config/ConfigStartupTests.cpp:221-261`

**Interfaces:**

- Consumes: strict `ConfigDocument`, `ConfigCompiler::Compile`, existing ConfigGUI `dirty` flow, and shared serialization.
- Produces: required TOML key `[experimental].enable_auto_play`, `bool ValidatedConfig::enable_auto_play() const noexcept`, ConfigGUI label `Native auto play`, and the immutable game-launch value used by Task 4.
- Does not produce: a settings class, a NESYS-process field, hot reload, or independent safety sub-options.

- [ ] **Step 1: Add a failing configuration contract test**

Add this unambiguous fixture helper beside `ReadDistributedConfig()` in `ConfigContractTests.cpp`:

```cpp
std::string ReplaceUnique(
    std::string source,
    const std::string_view before,
    const std::string_view after)
{
    const auto position = source.find(before);
    const bool unique = position != std::string::npos &&
        source.find(before, position + before.size()) == std::string::npos;
    Expect(unique, "config fixture edit has one unambiguous source");
    if (unique)
    {
        source.replace(position, before.size(), after);
    }
    return source;
}
```

Add one grouped test, `NativeAutoPlayConfigurationIsStrictAndOwned()`, with these exact observations:

```cpp
void NativeAutoPlayConfigurationIsStrictAndOwned()
{
    const auto distributed = ReadDistributedConfig();
    auto parsed = gc::config::ParseConfigDocument(distributed);
    Expect(parsed.has_value(), "auto-play source document strictly parses");
    if (!parsed)
    {
        return;
    }

    Expect(
        !parsed->document.experimental().enable_auto_play(),
        "distributed auto play is disabled");

    const auto disabled = gc::config::ConfigCompiler::Compile(
        parsed->document);
    Expect(disabled.has_value(), "disabled auto-play document compiles");
    if (disabled)
    {
        Expect(
            !disabled->enable_auto_play(),
            "disabled auto play reaches immutable launch settings");
    }

    const auto missing_text = ReplaceUnique(
        distributed,
        "enable_auto_play = false",
        "");
    const auto missing = gc::config::ParseConfigDocument(missing_text);
    Expect(!missing.has_value(), "missing auto-play field is rejected");
    if (!missing)
    {
        Expect(
            missing.error().code ==
                gc::config::ConfigDocumentLoadErrorCode::strict_shape,
            "missing auto-play field is a strict-shape error");
    }

    const auto canonical_false = gc::config::SerializeConfigDocument(
        parsed->document);
    parsed->document.experimental().enable_auto_play = true;
    const auto enabled = gc::config::ConfigCompiler::Compile(
        parsed->document);
    const auto canonical_true = gc::config::SerializeConfigDocument(
        parsed->document);
    Expect(enabled.has_value(), "enabled auto-play document compiles");
    Expect(canonical_false.has_value(), "disabled document serializes");
    Expect(canonical_true.has_value(), "enabled document serializes");
    if (!enabled || !canonical_false || !canonical_true)
    {
        return;
    }

    Expect(enabled->enable_auto_play(), "enabled launch snapshot is true");
    parsed->document.experimental().enable_auto_play = false;
    Expect(
        enabled->enable_auto_play(),
        "compiled auto-play value does not alias the GUI draft");
    Expect(
        *canonical_true == ReplaceUnique(
            *canonical_false,
            "enable_auto_play = false",
            "enable_auto_play = true"),
        "the GUI document edit changes only auto play");

    const auto reparsed = gc::config::ParseConfigDocument(*canonical_true);
    Expect(reparsed.has_value(), "enabled auto play reparses");
    if (reparsed)
    {
        Expect(
            reparsed->document.experimental().enable_auto_play(),
            "enabled auto play survives serialization and reparse");
    }
}
```

Call it once from `main()`. In `ValidCurrentGameConfigPublishesWithoutWriting()` in `ConfigStartupTests.cpp`, add:

```cpp
Expect(
    !game->settings.enable_auto_play(),
    "distributed game settings disable native auto play");
```

This test compares canonical documents against a one-line independent transformation. It does not introduce a GUI-only production helper solely for testing.

- [ ] **Step 2: Run the focused test and observe the intended failure**

Run from an x86 MSVC Developer PowerShell:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_config_contract_tests gc_config_startup_tests
```

Expected before implementation: compilation fails because `ExperimentalConfig::enable_auto_play` and `ValidatedConfig::enable_auto_play()` do not exist.

- [ ] **Step 3: Add the required document field and distributed default**

Place this immediately after `enable_absolute_time_judgement` in `ExperimentalConfig`:

```cpp
rfl::Rename<"enable_auto_play", bool>
enable_auto_play{false};
```

Place this immediately after `enable_absolute_time_judgement` under `[experimental]` in `config.toml`:

```toml
enable_auto_play = false
```

Do not use `rfl::DefaultIfMissing`; the in-class value is for a newly constructed ConfigGUI document, not backward-compatible parsing.

- [ ] **Step 4: Copy the Boolean into immutable launch settings**

Add this public accessor to `ValidatedConfig`:

```cpp
[[nodiscard]] bool enable_auto_play() const noexcept
{
    return enable_auto_play_;
}
```

Add `bool enable_auto_play` to the private constructor immediately before `bool unlock_all_songs_and_difficulties`, initialize `enable_auto_play_`, and add:

```cpp
bool enable_auto_play_{};
```

Pass the document value in `ConfigCompiler::Compile` immediately before the existing song-unlock Boolean:

```cpp
document.experimental().enable_auto_play(),
document.experimental().unlock_all_songs_and_difficulties(),
```

The NESYS result continues to select only `compiled->logging()` and `compiled->nesys()`; do not add auto play to `NesysProcessConfiguration`.

- [ ] **Step 5: Add the one ConfigGUI control and checked-state warning**

Place this after the absolute-time judgement block and before timer-freeze/song-unlock controls in `DrawExperimental`:

```cpp
bool auto_play = experimental.enable_auto_play();
if (ImGui::Checkbox("Native auto play", &auto_play))
{
    experimental.enable_auto_play = auto_play;
    dirty = true;
}
if (auto_play)
{
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(1.0F, 0.75F, 0.2F, 1.0F),
        "Gameplay input/free taps ignored; card and score saving disabled; "
        "in-game marker mandatory; restart required.");
}
```

Do not add controls for accuracy, mute/HIDDEN behavior, saving, marker styling, or input suppression.

- [ ] **Step 6: Rebuild and run the focused configuration tests**

```powershell
cmake --build --preset msvc32-debug --target gc_config_contract_tests gc_config_startup_tests ConfigGUI
ctest --preset msvc32-debug -R '^(ConfigContract|ConfigStartup)$' -j 4 --output-on-failure
```

Expected: both tests pass and `ConfigGUI.exe` builds. This proves strict parsing, value ownership, serialization, and compilation; it does not prove the visual GUI layout.

- [ ] **Step 7: Commit the configuration surface**

```powershell
git add -- config.toml src/Config/ConfigDocument.h src/Config/ConfigCompiler.h src/Config/ConfigCompiler.cpp tools/ConfigGUI/Main.cpp tests/Config/ConfigContractTests.cpp tests/Config/ConfigStartupTests.cpp
git diff --cached --check
git commit -m "Add native auto play configuration"
```

---

### Task 2: Implement the fixed allocation-free marker producer

**Files:**

- Create: `src/Patches/AutoPlay/AutoPlayMarker.h`
- Create: `src/Patches/AutoPlay/AutoPlayMarker.cpp`
- Create: `tests/Patches/AutoPlay/AutoPlayPatchTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: the spec's exact strings, coordinates, colors, and native `__cdecl` debug-text ABI.
- Produces:

```cpp
namespace gc::auto_play {

using NativeDebugTextFunction = int(__cdecl*)(
    float, float, std::uint32_t, const char*, ...);

struct AutoPlayMarkerTextActions
{
    void* context{};
    bool (*queue)(
        void*, float, float, std::uint32_t, const char*) noexcept{};
};

enum class AutoPlayMarkerFrameResult : std::uint8_t
{
    inactive,
    queued,
    invalid_actions,
    native_text_failure,
};

[[nodiscard]] AutoPlayMarkerFrameResult ProduceAutoPlayMarkerFrame(
    bool active,
    const AutoPlayMarkerTextActions& actions) noexcept;

[[nodiscard]] bool CallNativeDebugTextGuarded(
    NativeDebugTextFunction function,
    float x,
    float y,
    std::uint32_t argb,
    const char* text) noexcept;

} // namespace gc::auto_play
```

- Does not produce: an overlay object, font/resource lifetime, configurable marker, render logging, or a claim that native text is visible.

- [ ] **Step 1: Register a focused test executable and write the failing marker test**

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(gc_auto_play_patch_tests
        Patches/AutoPlay/AutoPlayPatchTests.cpp
)
target_link_libraries(gc_auto_play_patch_tests PRIVATE
        gc_runtime_patches
)

add_test(
        NAME AutoPlayPatch
        COMMAND gc_auto_play_patch_tests
)
```

At the top of `AutoPlayPatchTests.cpp`, record independent fixture provenance:

```cpp
// Marker oracle: docs/superpowers/specs/
// 2026-09-03-native-auto-play-safety-design.md, Visible Marker Contract.
// Native ABI evidence: game471-debug-text-outer-frame-trace.json,
// SHA-256 FD2FBB6475A0368B8C7DE1356F7F16E74DB7F2612481318FB674AB3998B9BBE7.
```

Use the repository's standalone `g_failures`/`Expect` style. Define a fixed recorder:

```cpp
struct TextCall
{
    float x{};
    float y{};
    std::uint32_t argb{};
    std::string_view text;
};

struct TextRecorder
{
    std::array<TextCall, 4> calls{};
    std::size_t count{};
    std::size_t fail_at{std::numeric_limits<std::size_t>::max()};
};

bool RecordText(
    void* context,
    const float x,
    const float y,
    const std::uint32_t argb,
    const char* text) noexcept
{
    auto& recorder = *static_cast<TextRecorder*>(context);
    if (recorder.count == recorder.fail_at)
    {
        return false;
    }
    if (recorder.count >= recorder.calls.size() || text == nullptr)
    {
        return false;
    }
    recorder.calls[recorder.count++] = {x, y, argb, text};
    return true;
}
```

Add `MarkerProducerIsInactiveOrEmitsTheFixedContract()` and assert:

1. `active=false` with an empty action table returns `inactive` and makes no call.
2. `active=true` with `RecordText` returns `queued` and emits exactly these calls in this order:

```cpp
constexpr std::array<TextCall, 4> expected{
    TextCall{34.0F, 34.0F, 0xFF000000U, "AUTO PLAY"},
    TextCall{34.0F, 54.0F, 0xFF000000U, "SCORE SAVE DISABLED"},
    TextCall{32.0F, 32.0F, 0xFFFFFF00U, "AUTO PLAY"},
    TextCall{32.0F, 52.0F, 0xFFFFFF00U, "SCORE SAVE DISABLED"},
};
```

3. An active empty action table returns `invalid_actions`.
4. Setting `fail_at=2` returns `native_text_failure` after two successful calls and never reports `queued`.

- [ ] **Step 2: Run the new test and observe the intended failure**

```powershell
cmake --build --preset msvc32-debug --target gc_auto_play_patch_tests
```

Expected before implementation: compilation fails because `Patches/AutoPlay/AutoPlayMarker.h` does not exist.

- [ ] **Step 3: Implement the fixed marker sequence**

Implement `ProduceAutoPlayMarkerFrame` with an early inactive return before validating actions. For active state, reject a null context or queue callback, then iterate one internal `constexpr std::array` containing the four calls above. Return on the first sink failure; otherwise return `queued`.

The foreground/shadow strings remain static string literals. The successful path uses no `std::string`, `std::vector`, formatting, logging, heap ownership, configuration lookup, or graphics resource.

- [ ] **Step 4: Implement the native ABI leaf with structured-exception containment**

Implement `CallNativeDebugTextGuarded` in a small function with no C++ object requiring destruction inside the guarded region:

```cpp
bool CallNativeDebugTextGuarded(
    const NativeDebugTextFunction function,
    const float x,
    const float y,
    const std::uint32_t argb,
    const char* const text) noexcept
{
    if (function == nullptr || text == nullptr)
    {
        return false;
    }
#if defined(_MSC_VER)
    __try
    {
        (void)function(x, y, argb, "%s", text);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
#error "Native auto play requires the MSVC x86 SEH boundary"
#endif
}
```

Include `<Windows.h>` only in the implementation for `EXCEPTION_EXECUTE_HANDLER`. Do not catch or suppress a marker failure above this leaf; Task 4 turns the returned failure into a one-shot fatal.

- [ ] **Step 5: Compile the producer into `gc_runtime_patches` and run the test**

Add `AutoPlay/AutoPlayMarker.cpp` to `gc_runtime_patches`, then run:

```powershell
cmake --build --preset msvc32-debug --target gc_auto_play_patch_tests
ctest --preset msvc32-debug -R '^AutoPlayPatch$' --output-on-failure
```

Expected: the grouped marker contract passes. This proves only calls submitted to an injected sink.

- [ ] **Step 6: Commit the marker producer**

```powershell
git add -- src/Patches/AutoPlay/AutoPlayMarker.h src/Patches/AutoPlay/AutoPlayMarker.cpp src/Patches/CMakeLists.txt tests/Patches/AutoPlay/AutoPlayPatchTests.cpp tests/CMakeLists.txt
git diff --cached --check
git commit -m "Add native auto play marker producer"
```

---

### Task 3: Implement five-site preflight and ownership-aware rollback

**Files:**

- Create: `src/Patches/AutoPlay/AutoPlayPatchTransaction.h`
- Create: `src/Patches/AutoPlay/AutoPlayPatchTransaction.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/AutoPlay/AutoPlayPatchTests.cpp`

**Interfaces:**

- Consumes: `gc::game_compatibility::GameBinaryMemoryResult` and its read/write error stages.
- Produces:

```cpp
namespace gc::auto_play {

inline constexpr std::size_t kMaximumAutoPlayPatternBytes = 10;

struct AutoPlayBytePattern
{
    std::array<std::byte, kMaximumAutoPlayPatternBytes> bytes{};
    std::uint8_t size{};
    [[nodiscard]] std::span<const std::byte> view() const noexcept;
};

enum class AutoPlayContractSite : std::uint8_t
{
    none,
    do_not_save_card_data,
    complete_is_mute,
    native_auto_play,
    marker_seam,
    native_debug_text,
};

enum class AutoPlayPatchStage : std::uint8_t
{
    none,
    invalid_actions,
    resolve_image_base,
    address_range,
    preflight_read,
    byte_mismatch,
    hook_install,
    direct_write,
};

enum class AutoPlayPatchState : std::uint8_t
{
    disabled,
    enabled,
    already_enabled,
};

struct AutoPlayRuntimeState
{
    std::atomic_bool marker_active{};
    std::uintptr_t native_text_address{};
    std::size_t direct_patched{};
    std::size_t direct_existing{};
};

struct AutoPlayPatchActions
{
    void* context{};
    std::expected<std::uintptr_t, DWORD> (*resolve_image_base)(
        void*) noexcept{};
    game_compatibility::GameBinaryMemoryResult (*read)(
        void*, std::uintptr_t, std::span<std::byte>) noexcept{};
    game_compatibility::GameBinaryMemoryResult (*write)(
        void*, std::uintptr_t, std::span<const std::byte>) noexcept{};
    std::expected<void, std::uint32_t> (*install_marker_hook)(
        void*, std::uintptr_t) noexcept{};
    bool (*reset_marker_hook)(void*) noexcept{};
};

struct AutoPlayPatchResult
{
    AutoPlayPatchState state{AutoPlayPatchState::disabled};
    std::size_t direct_patched{};
    std::size_t direct_existing{};
};

struct AutoPlayPatchError
{
    AutoPlayPatchStage stage{AutoPlayPatchStage::none};
    AutoPlayContractSite site{AutoPlayContractSite::none};
    std::uint32_t rva{};
    AutoPlayBytePattern expected_clean{};
    AutoPlayBytePattern expected_patched{};
    AutoPlayBytePattern actual{};
    game_compatibility::GameBinaryMemoryStage memory_stage{
        game_compatibility::GameBinaryMemoryStage::None};
    DWORD win32_error{};
    std::uint32_t safetyhook_error{};
    bool rollback_attempted{};
    bool rollback_complete{};
    AutoPlayContractSite rollback_site{AutoPlayContractSite::none};
    game_compatibility::GameBinaryMemoryStage rollback_memory_stage{
        game_compatibility::GameBinaryMemoryStage::None};
    DWORD rollback_win32_error{};
};

[[nodiscard]] std::expected<AutoPlayPatchResult, AutoPlayPatchError>
InstallAutoPlayPatch(
    bool enabled,
    AutoPlayRuntimeState& runtime,
    const AutoPlayPatchActions& actions) noexcept;

[[nodiscard]] const char* AutoPlayPatchStageName(
    AutoPlayPatchStage stage) noexcept;
[[nodiscard]] const char* AutoPlayContractSiteName(
    AutoPlayContractSite site) noexcept;
[[nodiscard]] const char* AutoPlayPatchStateName(
    AutoPlayPatchState state) noexcept;

} // namespace gc::auto_play
```

`marker_active` is the commit publication: acquire-load `true` means the hook target and counters were published and all direct sites committed. A second enabled call returns `already_enabled` before validating or invoking any action.

- [ ] **Step 1: Add independent native fixtures and the failing transaction matrix**

Add this provenance comment above the transaction fixture in `AutoPlayPatchTests.cpp`:

```cpp
// Direct-site oracle: game471-autoplay-patch-contract.json,
// SHA-256 06E2C1F788F4DD8BBE072D53E3952E2275D376954EF1A399331C2215022A423F.
// Marker seam/native text oracle: game471-debug-text-outer-frame-trace.json,
// SHA-256 FD2FBB6475A0368B8C7DE1356F7F16E74DB7F2612481318FB674AB3998B9BBE7.
// The fixture intentionally does not consume the production contract table.
```

Use preferred base `0x00400000U` and five fixed `SiteFixture` entries carrying the RVA, size, and independent clean bytes from the frozen table. Store each fixture in a ten-byte array; fake reads and writes locate a fixture by exact `base + rva`, require the requested span size to match, and copy bytes with `std::ranges::copy`.

Use this test-owned helper for the independently transcribed error/fixture patterns:

```cpp
template <std::uint8_t... Values>
consteval gc::auto_play::AutoPlayBytePattern TestPattern() noexcept
{
    static_assert(
        sizeof...(Values) <= gc::auto_play::kMaximumAutoPlayPatternBytes);
    gc::auto_play::AutoPlayBytePattern pattern{};
    std::size_t index{};
    ((pattern.bytes[index++] = std::byte{Values}), ...);
    pattern.size = static_cast<std::uint8_t>(sizeof...(Values));
    return pattern;
}
```

The fake backend records these observable events in one bounded array:

```cpp
enum class EventKind : std::uint8_t
{
    resolve,
    read,
    hook_install,
    direct_write,
    hook_reset,
};

struct Event
{
    EventKind kind{};
    std::uintptr_t address{};
};
```

It also provides exact injection knobs: `read_failure_site`, `unknown_site`, `hook_install_failure`, `hook_reset_failure`, `direct_write_failure_index`, and `rollback_write_failure_address`. Give it a pointer to the `AutoPlayRuntimeState` under test and record whether any hook/write callback observes `marker_active=true`. Return `GameBinaryMemoryError{Read, ERROR_NOACCESS}` for injected reads, `{Copy, ERROR_WRITE_FAULT}` for forward writes, `{RestoreProtection, ERROR_ACCESS_DENIED}` for rollback writes, and SafetyHook code `7` for hook installation failure.

Add four grouped tests rather than one test per helper:

1. `DisabledAndCommittedInitializationPerformNoOperations`:
   - disabled with an entirely empty action table returns `disabled`, leaves `marker_active=false`, and records no event;
   - after one successful enabled run, clear the event log and call again; it returns `already_enabled` with the stored counts and records no event.
2. `EveryDirectStateCombinationCommitsInSafetyOrder`:
   - loop masks `0` through `7`, where a set bit starts that direct site in its independently transcribed patched form;
   - every mask resolves once, installs the hook once, writes only clean direct sites, reports `direct_existing=popcount(mask)` and `direct_patched=3-popcount(mask)`, publishes the native text address, and ends active;
   - the hook-install callback and every direct-write callback observe `marker_active=false`; only the returned successful state observes it as true;
   - among writes, addresses occur only in `0x00269951`, `0x0003CAFA`, `0x0003CADA` order after the hook-install event;
   - mask `7` still installs the marker hook and performs no direct write.
3. `EveryPreflightFailureLeavesTheImageAndHookUntouched`:
   - null each required action callback in turn and expect `invalid_actions` with no event;
   - return an image base whose addition overflows and expect `address_range` with no read/write/hook event after resolution;
   - for each direct site, inject one read failure and one unknown byte and require no write/hook/reset;
   - independently corrupt the marker seam and native-text prologue and require `byte_mismatch` with no write/hook/reset.
4. `HookAndDirectWriteFailuresRollbackOnlyOwnedSites`:
   - hook installation failure occurs after preflight but before any direct write, calls hook reset, stays inactive, and reports rollback attempted/completed;
   - fail forward write positions one, two, and three with all sites initially clean; require hook reset first, then successful prior writes restored in reverse order, with the failing site never restored because it was never owned;
   - start `do_not_save_card_data` already patched and fail the third forward write; require restoration of only `complete_is_mute`, never the pre-existing no-save site;
   - inject hook-reset failure and then a rollback-write failure in separate runs; both retain the original `direct_write` error, set `rollback_attempted=true`, `rollback_complete=false`, identify the rollback site/stage, and never publish marker activity.

Call all four groups from `main()` after the marker group.

- [ ] **Step 2: Run the expanded test and observe the intended failure**

```powershell
cmake --build --preset msvc32-debug --target gc_auto_play_patch_tests
```

Expected before implementation: compilation fails because `AutoPlayPatchTransaction.h` and its types do not exist.

- [ ] **Step 3: Define the internal contract table and checked addressing**

In `AutoPlayPatchTransaction.cpp`, keep one internal direct-contract array in safety-write order:

```cpp
template <std::uint8_t... Values>
consteval AutoPlayBytePattern Pattern() noexcept
{
    static_assert(sizeof...(Values) <= kMaximumAutoPlayPatternBytes);
    AutoPlayBytePattern pattern{};
    std::size_t index{};
    ((pattern.bytes[index++] = std::byte{Values}), ...);
    pattern.size = static_cast<std::uint8_t>(sizeof...(Values));
    return pattern;
}

constexpr std::array<DirectContract, 3> kDirectContracts{
    DirectContract{
        AutoPlayContractSite::do_not_save_card_data,
        0x00269951U,
        Pattern<0x0F, 0x95, 0xC1>(),
        Pattern<0xB1, 0x01, 0x90>()},
    DirectContract{
        AutoPlayContractSite::complete_is_mute,
        0x0003CAFAU,
        Pattern<0x8A, 0x80, 0xA6, 0x00, 0x00, 0x00>(),
        Pattern<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
    DirectContract{
        AutoPlayContractSite::native_auto_play,
        0x0003CADAU,
        Pattern<0x8A, 0x80, 0xA5, 0x00, 0x00, 0x00>(),
        Pattern<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
};
```

Use separate read-only contracts for marker seam RVA `0x00058BE9` with ten bytes and native text RVA `0x00069650` with five bytes. `CheckedAddress(base, rva, size, out)` rejects zero base, zero size, addition overflow, and `address + size - 1` overflow.

- [ ] **Step 4: Implement complete preflight before mutation**

`InstallAutoPlayPatch` follows this exact prefix:

1. Return `disabled` immediately when `enabled` is false.
2. Acquire-load `runtime.marker_active`; if true, return `already_enabled` and the stored counters immediately.
3. Validate non-null context and all five callbacks before invoking any callback.
4. Resolve the image base once; preserve its `DWORD` error under `resolve_image_base`.
5. Compute all five complete address ranges before reading memory.
6. Read all three direct sites, then marker seam, then native-text prologue.
7. Classify every direct site as clean or already patched; any other bytes return `byte_mismatch` with clean, patched, and actual patterns.
8. Require exact equality for marker seam and native text; their errors carry the expected native pattern and actual bytes with an empty patched pattern.

Do not publish `runtime.native_text_address`, install the hook, or write memory until all classifications pass.

- [ ] **Step 5: Implement hook-first mutation, ordered writes, and rollback**

After preflight:

1. Store the checked native-text address in `runtime.native_text_address` while marker activity remains false.
2. Call `install_marker_hook(context, marker_seam_address)`.
3. For each clean direct contract in array order, write its patched bytes and append its index to a three-element owned-index array only after the write succeeds.
4. Store final patched/existing counts in the runtime.
5. Release-store `true` to `runtime.marker_active` as the final non-failing operation.

On hook or direct-write failure, run one rollback lambda that:

1. calls `reset_marker_hook` before any direct restoration;
2. writes clean bytes for owned direct indices in reverse order;
3. records the first failed hook reset or restoration in the rollback fields while still attempting every remaining owned restoration;
4. clears `native_text_address` and both counters;
5. never changes `marker_active` from false;
6. returns the original hook/write error with rollback flags attached.

Already-patched direct sites never enter the owned-index array and therefore are never restored.

- [ ] **Step 6: Implement stable names and run the exhaustive test matrix**

Implement total switches for every stage, site, and result enum, returning `unknown` only for an out-of-range value. Add `AutoPlay/AutoPlayPatchTransaction.cpp` to `gc_runtime_patches`, then run:

```powershell
cmake --build --preset msvc32-debug --target gc_auto_play_patch_tests
ctest --preset msvc32-debug -R '^AutoPlayPatch$' --output-on-failure
```

Expected: all marker, eight-combination, preflight, rollback, and repeated-initialization observations pass.

- [ ] **Step 7: Commit the transaction**

```powershell
git add -- src/Patches/AutoPlay/AutoPlayPatchTransaction.h src/Patches/AutoPlay/AutoPlayPatchTransaction.cpp src/Patches/CMakeLists.txt tests/Patches/AutoPlay/AutoPlayPatchTests.cpp
git diff --cached --check
git commit -m "Add transactional native auto play installer"
```

---

### Task 4: Bind production SafetyHook, diagnostics, and game-only startup

**Files:**

- Create: `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.h`
- Create: `src/Patches/AutoPlay/AutoPlayPatchDiagnostics.cpp`
- Create: `src/Patches/AutoPlay/AutoPlayPatch.h`
- Create: `src/Patches/AutoPlay/AutoPlayPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp:14-37,660-690`
- Modify: `tests/Patches/AutoPlay/AutoPlayPatchTests.cpp`

**Interfaces:**

- Consumes: Tasks 1-3, `ProductionGameBinaryPatchActions()`, `safetyhook::MidHook`, plog, and `gc::system_path::PublishStartupFatal`.
- Produces:

```cpp
namespace gc::auto_play {

struct AutoPlayFatalDiagnostic
{
    std::string log;
    std::wstring modal;
    std::wstring title;
    DWORD exit_code{};
};

[[nodiscard]] AutoPlayFatalDiagnostic BuildAutoPlayPatchFatalDiagnostic(
    const AutoPlayPatchError& error);

void PublishAutoPlaySetupFatal(
    const AutoPlayPatchError& error) noexcept;
void PublishAutoPlaySetupFallbackFatal() noexcept;
void PublishAutoPlayMarkerRuntimeFatal() noexcept;

[[nodiscard]] bool AutoPlayPatchInit(bool enabled) noexcept;

} // namespace gc::auto_play
```

- Production runtime owns one `AutoPlayRuntimeState`, one `safetyhook::MidHook`, and the one-shot setup/runtime fatal latches for process lifetime.

- [ ] **Step 1: Add a failing structured-diagnostic test**

Add `DiagnosticsRetainContractAndRollbackEvidence()` to `AutoPlayPatchTests.cpp`. Construct one `AutoPlayPatchError` for a failed `native_auto_play` direct write with RVA `0x0003CADA`, expected clean/patched/actual patterns, memory stage `Copy`, Win32 error `ERROR_WRITE_FAULT`, and failed rollback at `complete_is_mute` with `RestoreProtection`/`ERROR_ACCESS_DENIED`.

Assert that `BuildAutoPlayPatchFatalDiagnostic` returns:

- title exactly `L"GCLoader auto play setup failed"`;
- feature-owned exit code `30`;
- a log containing `stage=direct_write`, `site=native_auto_play`, `rva=0x3CADA`, `expected_clean=`, `expected_patched=`, `actual=`, `memory_stage=copy`, `win32_error=29`, `rollback_attempted=1`, `rollback_complete=0`, `rollback_site=complete_is_mute`, `rollback_memory_stage=restore_protection`, and `rollback_win32_error=5`;
- a modal containing both `save suppression` and `visible marker`, plus `loader-log.txt`.

Call it from `main()`.

- [ ] **Step 2: Run the test and observe the intended failure**

```powershell
cmake --build --preset msvc32-debug --target gc_auto_play_patch_tests
```

Expected before implementation: compilation fails because `AutoPlayPatchDiagnostics.h` and its builder do not exist.

- [ ] **Step 3: Implement structured setup and runtime fatal diagnostics**

`AutoPlayPatchDiagnostics.cpp` formats patterns as uppercase, two-digit, space-separated bytes and includes every populated transaction field. Use exit code `30` and these exact user-facing strings:

```text
Title: GCLoader auto play setup failed
Message: GCLoader refused to continue because it could not guarantee both save suppression and the visible marker. Check loader-log.txt for the exact contract failure.
```

Expose `PublishAutoPlaySetupFatal`, `PublishAutoPlaySetupFallbackFatal`, and `PublishAutoPlayMarkerRuntimeFatal` to `AutoPlayPatch.cpp`. They call `PublishStartupFatal` with feature-owned atomic latches. The runtime fatal uses title `GCLoader auto play marker failed` and states that playable auto play cannot continue without its mandatory visible marker. Catch formatting failures and publish the fixed setup fallback; do not let an exception escape any publisher.

- [ ] **Step 4: Implement the process-lifetime production adapter**

Create `AutoPlayPatch.h` with only:

```cpp
#pragma once

namespace gc::auto_play
{
    [[nodiscard]] bool AutoPlayPatchInit(bool enabled) noexcept;
} // namespace gc::auto_play
```

In `AutoPlayPatch.cpp`, define one private runtime:

```cpp
struct ProductionAutoPlayRuntime
{
    AutoPlayRuntimeState state;
    safetyhook::MidHook marker_hook;
};

ProductionAutoPlayRuntime g_runtime;
```

The five production actions behave exactly as follows:

- `resolve_image_base`: call `GetModuleHandleW(nullptr)`, return its address, or return `GetLastError()` (use `ERROR_MOD_NOT_FOUND` if the handle is null and last error is zero).
- `read` and `write`: obtain `ProductionGameBinaryPatchActions()` and forward the original address/span into its callbacks, preserving `GameBinaryMemoryError` unchanged.
- `install_marker_hook`: create one enabled SafetyHook mid-hook at the supplied address targeting `AutoPlayMarkerMidHook`; on failure return `static_cast<std::uint32_t>(created.error().type)`, and on success move it into `g_runtime.marker_hook`.
- `reset_marker_hook`: call `g_runtime.marker_hook.reset()` inside a catch-all boundary and return whether reset completed.

Do not add a generic hook registry or place these opt-in RVAs in `GameBinaryPatch.cpp`.

- [ ] **Step 5: Implement the marker callback and mandatory runtime failure path**

The callback signature is:

```cpp
void AutoPlayMarkerMidHook(safetyhook::Context&) noexcept;
```

It acquire-loads `g_runtime.state.marker_active`. If inactive, it returns without reading the native-text address. If active, it converts the preflighted address to `NativeDebugTextFunction`, creates one stack-local `AutoPlayMarkerTextActions` whose queue function calls `CallNativeDebugTextGuarded`, and invokes `ProduceAutoPlayMarkerFrame(true, actions)`.

If the result is not `queued`, call `PublishAutoPlayMarkerRuntimeFatal()`. Wrap the callback body in a C++ catch-all that routes any unexpected failure to the same fatal. Do not log successful frames or read configuration in the callback.

- [ ] **Step 6: Implement the idempotent public initializer and diagnostics**

`AutoPlayPatchInit(bool enabled) noexcept` constructs the production actions and calls `InstallAutoPlayPatch(enabled, g_runtime.state, actions)`.

- On `disabled`, log one info record: `AutoPlayPatch: state=disabled` and return `true`.
- On `enabled`, log one warning record with exact fields `state=enabled direct_patched=<n> direct_existing=<n> marker=active score_save=disabled` and return `true`.
- On `already_enabled`, return `true` without reinstalling and log at most one info record identifying the stored result.
- On error, call `PublishAutoPlaySetupFatal(error)` and return `false` if the injected fatal actions ever return.
- Catch every C++ exception, call `PublishAutoPlaySetupFallbackFatal()`, and return `false`.

There is no normal detach/reset function. The process-lifetime hook and executable writes disappear with process exit.

- [ ] **Step 7: Add production files to the runtime-patch target**

Add these sources to `gc_runtime_patches`:

```cmake
AutoPlay/AutoPlayMarker.cpp
AutoPlay/AutoPlayPatch.cpp
AutoPlay/AutoPlayPatchDiagnostics.cpp
AutoPlay/AutoPlayPatchTransaction.cpp
```

Each appears exactly once. Keep the existing `gc_system_path`, `plog`, `gc_logging`, and SafetyHook links.

- [ ] **Step 8: Invoke auto play in the required game-only order**

Add this include to `DllMain.cpp`:

```cpp
#include "Patches/AutoPlay/AutoPlayPatch.h"
```

Immediately after the input-runtime configuration try/catch and before `SongUnlockPatchInit`, add:

```cpp
if (!gc::auto_play::AutoPlayPatchInit(settings.enable_auto_play()))
{
    PLOG_ERROR << "AutoPlayPatch: fail-closed DLL attach";
    return FALSE;
}
```

This code is already below the game-only role branch. Do not add a call to the NESYS branch or move auto play below song unlock, audio, absolute judgement, RFID, framerate, or Switch input.

- [ ] **Step 9: Run focused tests and build both user-facing targets**

```powershell
cmake --build --preset msvc32-debug --target gc_auto_play_patch_tests gc_config_contract_tests gc_config_startup_tests iDmacDrv32 ConfigGUI
ctest --preset msvc32-debug -R '^(AutoPlayPatch|ConfigContract|ConfigStartup)$' -j 4 --output-on-failure
```

Expected: focused tests pass; `iDmacDrv32.dll` and `ConfigGUI.exe` link under x86 Debug. This does not establish that the game renders the marker or suppresses persistence.

- [ ] **Step 10: Commit production integration**

```powershell
git add -- src/Patches/AutoPlay/AutoPlayPatchDiagnostics.h src/Patches/AutoPlay/AutoPlayPatchDiagnostics.cpp src/Patches/AutoPlay/AutoPlayPatch.h src/Patches/AutoPlay/AutoPlayPatch.cpp src/Patches/CMakeLists.txt src/Loader/DllMain.cpp tests/Patches/AutoPlay/AutoPlayPatchTests.cpp
git diff --cached --check
git commit -m "Integrate native auto play safety patch"
```

---

### Task 5: Revalidate native evidence and complete static verification

**Files:**

- Read-only scripts: `.codex-tmp/ida_autoplay_patch_contract.py`
- Read-only scripts: `.codex-tmp/ida_autoplay_mute_audio_closure.py`
- Read-only scripts: `.codex-tmp/ida_autoplay_grade_state.py`
- Read-only scripts: `.codex-tmp/ida_debug_text_outer_frame_trace.py`
- Local ignored verifier: `.codex-tmp/verify_autoplay_static.ps1`
- Build outputs: `build-msvc32-debug/dist/iDmacDrv32.dll`, `build-msvc32-release/dist/iDmacDrv32.dll`

**Interfaces:**

- Consumes: the four implementation commits and saved read-only IDA scripts.
- Produces: fresh artifact hashes, complete Debug/Release build and CTest evidence, bounded built-DLL presence checks, and a clean diff/status report.
- Does not produce: deployment, a game launch, NESYS traffic, saved-card observations, audible-note observations, or visual acceptance.

- [ ] **Step 1: Rerun each saved IDA analysis script without holding a session**

Run each file as its own process. Each script uses `with AgentSession.connect(...)` and disconnects on completion:

```powershell
python .codex-tmp\ida_autoplay_patch_contract.py
python .codex-tmp\ida_autoplay_mute_audio_closure.py
python .codex-tmp\ida_autoplay_grade_state.py
python .codex-tmp\ida_debug_text_outer_frame_trace.py
```

Expected: every command reports backend `idalib`, database `H:\gc\game471.exe.i64`, and an artifact beneath `H:\IDACLI\runs\20260902T140921Z-f5cdda06\artifacts`. If a script reports a changed contract, stop implementation at static verification; do not search for replacement bytes or alter the daemon.

- [ ] **Step 2: Verify the exact evidence hashes**

Create `.codex-tmp/verify_autoplay_static.ps1` with the following evidence-hash prefix. This file is ignored local tooling and is not staged:

```powershell
$artifactRoot = 'H:\IDACLI\runs\20260902T140921Z-f5cdda06\artifacts'
$expectedArtifacts = [ordered]@{
    'game471-autoplay-patch-contract.json' = '06E2C1F788F4DD8BBE072D53E3952E2275D376954EF1A399331C2215022A423F'
    'game471-autoplay-mute-audio-closure.json' = 'B44046F98BCEBFE2F353BF64A54CA9CFCD7B4932325AD1218BA069972F7C10F9'
    'game471-autoplay-grade-state.json' = '06DB7C31042170D84C1893F977F7C25BDE02818EBFCF7B84FEF07AC0A7136471'
    'game471-debug-text-outer-frame-trace.json' = 'FD2FBB6475A0368B8C7DE1356F7F16E74DB7F2612481318FB674AB3998B9BBE7'
}

foreach ($entry in $expectedArtifacts.GetEnumerator())
{
    $path = Join-Path $artifactRoot $entry.Key
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $entry.Value)
    {
        throw "IDA artifact hash mismatch: $($entry.Key) expected=$($entry.Value) actual=$actual"
    }
    Write-Output "IDA artifact verified: $($entry.Key) $actual"
}
```

Run it once after appending the DLL checks in Step 5.

- [ ] **Step 3: Configure, build, and test the complete Debug graph**

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4 --output-on-failure
```

Expected: the complete Debug build succeeds and every registered test passes, including `AutoPlayPatch`, `ConfigContract`, and `ConfigStartup`.

- [ ] **Step 4: Configure, build, and test the complete Release graph**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4 --output-on-failure
```

Expected: the complete RelWithDebInfo build succeeds and every registered test passes.

- [ ] **Step 5: Append bounded marker/RVA presence checks to the saved verifier**

Append this code to `.codex-tmp/verify_autoplay_static.ps1`:

```powershell
function Find-ByteSequence([byte[]]$haystack, [byte[]]$needle)
{
    for ($offset = 0; $offset -le $haystack.Length - $needle.Length; ++$offset)
    {
        $match = $true
        for ($index = 0; $index -lt $needle.Length; ++$index)
        {
            if ($haystack[$offset + $index] -ne $needle[$index])
            {
                $match = $false
                break
            }
        }
        if ($match)
        {
            return $offset
        }
    }
    return -1
}

$artifactPatterns = [ordered]@{
    'AUTO PLAY' = [Text.Encoding]::ASCII.GetBytes('AUTO PLAY')
    'SCORE SAVE DISABLED' = [Text.Encoding]::ASCII.GetBytes('SCORE SAVE DISABLED')
    'RVA native auto play 0x0003CADA' = [byte[]](0xDA, 0xCA, 0x03, 0x00)
    'RVA complete IsMute 0x0003CAFA' = [byte[]](0xFA, 0xCA, 0x03, 0x00)
    'RVA DoNotSaveCardData 0x00269951' = [byte[]](0x51, 0x99, 0x26, 0x00)
    'RVA marker seam 0x00058BE9' = [byte[]](0xE9, 0x8B, 0x05, 0x00)
    'RVA native debug text 0x00069650' = [byte[]](0x50, 0x96, 0x06, 0x00)
}

$dllPaths = @(
    'build-msvc32-debug\dist\iDmacDrv32.dll',
    'build-msvc32-release\dist\iDmacDrv32.dll'
)
foreach ($dllPath in $dllPaths)
{
    $resolved = (Resolve-Path -LiteralPath $dllPath).Path
    $bytes = [IO.File]::ReadAllBytes($resolved)
    foreach ($entry in $artifactPatterns.GetEnumerator())
    {
        $offset = Find-ByteSequence $bytes $entry.Value
        if ($offset -lt 0)
        {
            throw "Built DLL is missing $($entry.Key): $resolved"
        }
        Write-Output "Built DLL contains $($entry.Key) at file offset 0x$($offset.ToString('X')): $resolved"
    }
}
```

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .codex-tmp\verify_autoplay_static.ps1
```

These byte-presence checks establish only that the intended literals/RVA immediates reached both DLL artifacts; transaction tests and native evidence remain the semantic proof.

- [ ] **Step 6: Inspect the final source delta and repository state**

```powershell
git diff --check c787b50d42084c892e794b9301e86765bf923cf2..HEAD
git diff --stat c787b50d42084c892e794b9301e86765bf923cf2..HEAD
git status --short
git log --oneline --decorate -6
```

Expected: `git diff --check` is silent; the stat contains only the file map in this plan plus the approved-status/plan documents; `.codex-tmp` remains ignored; and a clean execution worktree has no status entries. If unrelated user changes appeared during execution, report and preserve them instead of staging or reverting them.

---

## Runtime Acceptance Requiring Separate Authorization

Implementation ends after Task 5. A later operator-run deployment must separately establish every item below before the feature is described as accepted in game:

1. With `enable_auto_play=false`, gameplay input, free taps, card saving, CSV behavior, and marker absence remain native.
2. With `enable_auto_play=true`, booster input creates neither judgements nor free taps, while pause, service, and exit remain usable.
3. A representative chart containing taps, holds, slides, scratches, paired/dual components, and HIDDEN/AD-LIB descriptors finishes with native GREAT results at authored timestamps and no auto-play misses.
4. HIDDEN/AD-LIB arrangement sounds remain audible and ignored physical gameplay input produces no synthetic generic tap sound.
5. Both marker lines remain legible and unobscured through gameplay and results in normal output and with windowed widescreen enabled.
6. No new result CSV is created.
7. Captured NESYS traffic shows no normal finish-game player/card-data save transaction, and server/card state confirms no score, clear/rank state, currency, unlock, or inventory persistence.
8. After restarting with the setting false, the marker is absent and normal saving resumes.

Do not infer any item from CTest, fake memory, marker-sink calls, artifact scanning, or IDA control flow.

## Self-Review

- **Spec coverage:** Task 1 covers the required Boolean, immutable launch value, ConfigGUI warning, strict missing-field failure, and false/true round trip. Task 2 covers the exact fixed marker call set and native ABI boundary. Task 3 covers all five native contracts, eight direct-state combinations, zero-mutation preflight, hook-first order, safety-write order, ownership-aware rollback, rollback failure, atomic activation, and repeated initialization. Task 4 covers feature-owned SafetyHook state, structured diagnostics, one-shot runtime fatal, game-only ordering, and fail-closed attach. Task 5 covers all required static evidence while leaving runtime acceptance separate.
- **Non-goals:** The plan adds no input injection/block, grade patch, scoring change, separate safety toggle, disk executable/config patch, server/NESYS behavior, D3D overlay, reusable renderer framework, hot reload, signature scan, or new game-build support.
- **Type consistency:** `ValidatedConfig::enable_auto_play()`, `AutoPlayRuntimeState::marker_active`, `InstallAutoPlayPatch(bool, AutoPlayRuntimeState&, const AutoPlayPatchActions&)`, `ProduceAutoPlayMarkerFrame(bool, const AutoPlayMarkerTextActions&)`, and `AutoPlayPatchInit(bool)` are named and typed consistently across producers and consumers.
- **Test quality:** Native byte and marker expectations are independently sourced and identified by artifact/spec hashes. Tests observe action order, memory state, ownership, and publication rather than scraping source or asserting a duplicate production manifest.
- **Placeholder scan:** Every task names exact files, public interfaces, native RVAs/bytes, test matrices, commands, expected outcomes, and commit boundaries.
