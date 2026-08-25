# All Songs and Difficulties Unlock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in, fail-closed GCLoader runtime patch that unlocks all songs, difficulties, and eligible EXTRA charts without enabling any other tournament-mode behavior.

**Architecture:** Add one dedicated `SongUnlock` patch unit for the verified six-byte branch at RVA `0x00257854`. Reuse the existing guarded game-binary read/write actions, expose one strict config Boolean and ConfigGUI checkbox, and invoke the patch only in the game process after configuration is loaded.

**Tech Stack:** C++23, Win32 x86, MSVC, CMake presets, reflect-cpp TOML configuration, ImGui ConfigGUI, CLion MCP diagnostics, IDA 9 read-only evidence.

**Spec:** `docs/superpowers/specs/2026-08-24-all-songs-and-difficulties-unlock-design.md`

## Global Constraints

- Use CLion MCP only for source editing and IDE diagnostics. Run Git, builds,
  tests, IDA, and every other command through the normal shell. Plain helper
  scripts may be written under `H:\\gc\\temp` when shell escaping is cumbersome.
- Work only in the selected GCLoader source checkout. `H:\gc` remains read-only runtime/IDA evidence; do not deploy or modify its executables.
- Preserve every unrelated change in the currently open `asio-audio-backend` worktree. Do not stage, revert, reformat, or include those files in this feature.
- The option is exactly `[experimental].unlock_all_songs_and_difficulties`, defaults to `false`, and requires restart.
- Patch only RVA `0x00257854`: clean `0F 85 1D 02 00 00`, patched `E9 1E 02 00 00 90`.
- Accept only exact clean or exact already-patched bytes. Unknown or unreadable bytes and every write-stage failure abort game startup.
- Never write SystemSetting `+0x8C`, install a detour, emulate tournament mode, or alter items, cosmetics, judgement, scoring, chain score, events, timers, or saving.
- Apply the patch only to the game process; the NESYS process must not inspect or mutate the site.
- Do not add a unit-test target merely for workflow compliance. The independent oracle is the verified IDA/binary control flow; use that evidence, compiler diagnostics, builds, and later game acceptance.
- Static/build success is not cabinet acceptance.

---

## File Structure

- Create `src/Patches/SongUnlock/SongUnlockPatch.h`: public game-start initializer.
- Create `src/Patches/SongUnlock/SongUnlockPatch.cpp`: the one-site byte contract, exact state classification, guarded write, and failure logging.
- Modify `src/Patches/CMakeLists.txt`: compile the new source into `gc_runtime_patches`.
- Modify `src/Config/config.h`: strict TOML field and `ConfigManager` getter.
- Modify `config.toml`: explicit disabled runtime default.
- Modify `tools/ConfigGUI/Main.cpp`: checkbox and scope/restart tooltip.
- Modify `src/Loader/DllMain.cpp`: game-only, fail-closed startup call after config load.

---

### Task 1: Implement the Isolated Song-Availability Patch

**Files:**
- Create: `src/Patches/SongUnlock/SongUnlockPatch.h`
- Create: `src/Patches/SongUnlock/SongUnlockPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `src/Config/config.h`
- Modify: `config.toml`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `src/Loader/DllMain.cpp`

**Interfaces:**
- Consumes: `gc::game_compatibility::ProductionGameBinaryPatchActions()`, `ConfigManager`, and `gc::nesys_service::ShouldRunGameOnlyInitialization()`.
- Produces:

```cpp
namespace gc::song_unlock {
[[nodiscard]] bool SongUnlockPatchInit(bool enabled) noexcept;
}

bool ConfigManager::GetUnlockAllSongsAndDifficulties() const;
```

- [x] **Step 1: Add the strict disabled-by-default option**

In `ExperimentalConfig`, place this Boolean with the other runtime-patch
options:

```cpp
rfl::Rename<"unlock_all_songs_and_difficulties", bool>
    unlock_all_songs_and_difficulties = false;
```

Add this `ConfigManager` getter:

```cpp
[[nodiscard]] bool GetUnlockAllSongsAndDifficulties() const
{
    return config.experimental().unlock_all_songs_and_difficulties();
}
```

Add the explicit runtime key under `[experimental]` in `config.toml`:

```toml
unlock_all_songs_and_difficulties = false
```

- [x] **Step 2: Add the matching ConfigGUI control**

Place the checkbox next to the other experimental runtime-patch options:

```cpp
bool song_unlock =
    experimental.unlock_all_songs_and_difficulties();
if (ImGui::Checkbox(
        "Unlock all songs and difficulties",
        &song_unlock))
{
    experimental.unlock_all_songs_and_difficulties = song_unlock;
    dirty = true;
}
ImGui::SameLine();
ImGui::TextDisabled("(?)");
if (ImGui::IsItemHovered())
{
    ImGui::SetTooltip(
        "Unlocks all songs, difficulties, and eligible EXTRA charts.\n"
        "Does not enable tournament mode or alter items, judgement, "
        "scoring, events, or card saving.\n"
        "Requires restart.");
}
```

- [x] **Step 3: Define the minimal public patch surface**

Create `SongUnlockPatch.h`:

```cpp
#pragma once

namespace gc::song_unlock {

[[nodiscard]] bool SongUnlockPatchInit(bool enabled) noexcept;

} // namespace gc::song_unlock
```

- [x] **Step 4: Implement exact classification and fail-closed mutation**

In `SongUnlockPatch.cpp`, use these independently verified constants:

```cpp
constexpr std::uint32_t kAvailabilityBranchRva = 0x00257854U;
constexpr std::array<std::byte, 6> kCleanBytes{
    std::byte{0x0F}, std::byte{0x85}, std::byte{0x1D},
    std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
};
constexpr std::array<std::byte, 6> kPatchedBytes{
    std::byte{0xE9}, std::byte{0x1E}, std::byte{0x02},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x90},
};
```

Implement `SongUnlockPatchInit(bool enabled) noexcept` with this exact order:

1. If disabled, log `SongUnlockPatch: state=disabled` and return `true`
   without resolving, reading, or writing the site.
2. Resolve `GetModuleHandleW(nullptr)`; a null result logs
   `stage=resolve_module` and returns `false`.
3. Check `image_base + kAvailabilityBranchRva` and the six-byte end address
   for `std::uintptr_t` overflow; failure logs `stage=address_range` and
   returns `false`.
4. Obtain `ProductionGameBinaryPatchActions()` and read exactly six bytes.
   Preserve its memory stage and Win32 error in the failure log, then return
   `false`.
5. If bytes equal `kPatchedBytes`, log
   `SongUnlockPatch: state=already_patched rva=0x00257854` and return
   `true`.
6. If bytes are neither clean nor patched, log
   `stage=unknown_bytes`, the RVA, and uppercase two-digit hexadecimal
   `expected_clean`, `expected_patched`, and `actual` sequences; return
   `false` without writing.
7. Write exactly `kPatchedBytes` through the existing production action.
   On failure, log `stage=site_write`, memory stage, Win32 error, and RVA;
   return `false`.
8. Log `SongUnlockPatch: state=patched rva=0x00257854` and return `true`.
9. Catch every exception inside the `noexcept` initializer, emit a bounded
   `stage=exception` error when logging remains possible, and return
   `false`.

Use `std::span` over the arrays when calling the shared actions. Do not add a
hook, background state, retry, fallback, signature scan, global tournament
write, or second patch site.

- [x] **Step 5: Compile the patch and wire fail-closed startup**

Add `SongUnlock/SongUnlockPatch.cpp` to `gc_runtime_patches` in
`src/Patches/CMakeLists.txt`.

In `DllMain.cpp`, include the new header and, immediately after
`ApplyConfiguredLogLevel(config)`, add:

```cpp
if (gc::nesys_service::ShouldRunGameOnlyInitialization(role) &&
    !gc::song_unlock::SongUnlockPatchInit(
        config.GetUnlockAllSongsAndDifficulties()))
{
    PLOG_ERROR << "SongUnlockPatch: fail-closed DLL attach";
    return FALSE;
}
```

Do not move or modify the mandatory `GameBinaryPatchInit()` call. The new
initializer must never run in the NESYS branch.

- [x] **Step 6: Run CLion source diagnostics**

Through the CLion MCP, run `get_file_problems` for:

- `src/Patches/SongUnlock/SongUnlockPatch.h`
- `src/Patches/SongUnlock/SongUnlockPatch.cpp`
- `src/Config/config.h`
- `tools/ConfigGUI/Main.cpp`
- `src/Loader/DllMain.cpp`

Require no new errors or warnings attributable to this feature. Then run
`get_compiler_info` for `SongUnlockPatch.cpp`. If CLion has not modeled the
new file because CMake auto-reload is disabled, record that stale IDE-model
limitation and use the shell-generated CMake/Ninja model plus the real builds
as compiler authority. Require MSVC x86, `-std:c++latest`, and the configured
static runtime. Do not execute CMake or another command through CLion to
refresh the model.

- [x] **Step 7: Build the affected Debug and Release products through the normal shell**

Use a readable helper script under `H:\\gc\\temp` from the normal shell:

```powershell
cmd.exe /d /c H:\gc\temp\build_gcloader_unlock.cmd msvc32-debug
```

```powershell
cmd.exe /d /c H:\gc\temp\build_gcloader_unlock.cmd msvc32-release
```

Expected: both configure/build commands exit `0`; the Win32 DLL and ConfigGUI
link successfully in both configurations.

- [x] **Step 8: Reconfirm the native oracle**

Run the existing plain-text, read-only IDA 9 audit:

```powershell
python H:\gc\temp\ida_tournament_unlock_analysis.py
```

Require database `H:\gc\game471.exe.i64`, clean bytes
`0F 85 1D 02 00 00` at RVA `0x00257854`, target `0x657A77`, the
difficulty cap/EXTRA availability effects, and separate tournament-global
score/judgement consumers. The script must not patch or save the IDB.

- [x] **Step 9: Verify scope without disturbing unrelated work**

Run through the normal shell:

```powershell
git diff --check
git status --short
git diff -- src/Patches/SongUnlock src/Patches/CMakeLists.txt src/Config/config.h config.toml tools/ConfigGUI/Main.cpp src/Loader/DllMain.cpp docs/superpowers/specs/2026-08-24-all-songs-and-difficulties-unlock-design.md docs/superpowers/plans/2026-08-24-all-songs-and-difficulties-unlock.md
```

Expected: no whitespace errors; the scoped diff contains only the option,
checkbox, one-site patch, build registration, startup wiring, and these two
documents. Existing ASIO/high-FPS worktree modifications remain byte-for-byte
untouched and unstaged.

- [ ] **Step 10: Commit only the isolated feature when explicitly authorized**

```powershell
git add -- src/Patches/SongUnlock/SongUnlockPatch.h src/Patches/SongUnlock/SongUnlockPatch.cpp src/Patches/CMakeLists.txt src/Config/config.h config.toml tools/ConfigGUI/Main.cpp src/Loader/DllMain.cpp docs/superpowers/specs/2026-08-24-all-songs-and-difficulties-unlock-design.md docs/superpowers/plans/2026-08-24-all-songs-and-difficulties-unlock.md
git commit -m "Add isolated song and difficulty unlock patch"
```

Do not stage or commit any pre-existing worktree change. Skip this step unless
the user authorizes a commit during execution.

---

## Runtime Acceptance Outside This Plan

After a separately authorized build deployment:

1. With the option `false`, confirm the native song/difficulty locks remain.
2. With the option `true` and no card/server, confirm every song, authored
   difficulty, and eligible EXTRA chart is selectable.
3. Confirm normal judgement windows and normal chain-score behavior.
4. Confirm gameplay item inventory and consumption are unchanged.
5. Confirm event-mode availability and card/server saving follow normal mode,
   not tournament mode.

Until those checks pass on the game/cabinet, report only static and build
verification.

## Self-Review

- **Spec coverage:** The plan implements the exact opt-in key, one verified
  branch, game-only startup, clean/already-patched acceptance, hard failure,
  ConfigGUI exposure, static/build proof, and separate runtime acceptance.
- **Non-goals:** No tournament flag, item grant, cosmetics, judgement/score
  patch, timer patch, detour, signature scan, deployment, or unrelated
  refactor is included.
- **Type consistency:** `SongUnlockPatchInit(bool)` and
  `GetUnlockAllSongsAndDifficulties()` are used consistently in their
  declarations, implementation, and startup call.
- **Placeholder scan:** Every file, symbol, RVA, byte sequence, diagnostic,
  build command, and acceptance boundary is explicit.
