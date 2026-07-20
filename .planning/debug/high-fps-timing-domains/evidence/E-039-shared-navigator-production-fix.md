# E-039: shared navigator production cadence fix

## Implementation

The confirmed manual navigator state advance at VA `0x005B6310` / RVA
`0x001B6310` is now an inline SafetyHook contract named
`NavigatorAdvance`. Its preflight pattern is the 15-byte function entry
confirmed in the current IDB:

`55 8B EC 83 EC 08 89 4D FC 8B 45 FC 8B 48 60`

`HookNavigatorAdvance` uses the existing outer-frame authored-60-Hz phase:

- authored tick: call the original `thiscall` function once;
- non-authored tick: return the unchanged renderer pointer;
- native 60 FPS: the transformed hook set is not installed.

The draw callback and `sub_5B6C30` remain native-rate. This changes only the
global navigator's transition and face-cell counters documented in E-037 and
therefore covers every task mapped in E-038 without changing other menu,
MovieClip, input, or gameplay clocks.

The transactional hook capacity increased from 41 to 42. Exact hook order,
RVA, entry bytes, runtime binding, and rollback at every hook index are covered
by the existing framerate plan/runtime/transaction test targets.

## Normal build-path verification

The production output was configured and built through the checked-in
`msvc32-release` CMake preset. These focused CTest entries passed:

- `FrameratePatchTransactionTests`
- `FrameratePatchPlanTests`
- `FramerateRuntimeTests`

CTest reported 3/3 passed and zero failures. `git diff --check` also completed
with no errors.

An earlier command incorrectly targeted the legacy `build-msvc32` directory.
Its CMake reconfigure reconciled only that ignored build tree's private
FetchContent copies and exposed a stale reflect-cpp PCH; repository dependency
definitions were never changed. The manual temporary compile artifact was
deleted, `cmake/Dependencies.cmake` and `CMakePresets.json` were verified with
zero diff, and all accepted build/test evidence above comes exclusively from
the normal checked-in release preset.

## Deployment

Built and deployed DLL:

- source: `H:\gc\artifacts\GCLoader\build-msvc32-release\dist\iDmacDrv32.dll`
- target: `H:\gc\iDmacDrv32.dll`
- SHA256: `3001A110B4A69AF0E675EC03ACCCBE3F7B918E72ABB3AAAC818EEFA14C78B3F8`
- size: 5,587,456 bytes

Rollback DLL:

- `H:\gc\deploy-backups\iDmacDrv32.navigator-pre-20260721-020832.dll`
- previous SHA256:
  `456CCD0F5AF8C8C163A0F39150252259573B7B213DF8FD8050707D162858F3F3`

Runtime acceptance at high FPS remains operator-owned and pending.
