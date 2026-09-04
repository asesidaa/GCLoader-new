# Loader-Wide Cleanup Plan Set

This directory implements
[`2026-09-05-loader-codebase-cleanup-design.md`](../../specs/2026-09-05-loader-codebase-cleanup-design.md)
as a sequence of independently reviewable plans. The plans are intentionally
split at ownership and runtime-acceptance boundaries; they are not permission
to combine the cleanup into one broad change.

## Fixed execution rules

- Work only in `H:\gc\artifacts\GCLoader`. `H:\gc` is read-only runtime and
  binary evidence unless deployment or a target-process run is separately
  authorized.
- Preserve the x86 iDmac export ABI, game/NESYS process separation, strict
  configuration, existing behavior, return values, output writes, calling
  conventions, and `LastError` behavior.
- Preflight and hook installation are separate gates. Preflight proves target
  identity and versioned contracts. Hook creation/enabling proves that the
  selected hook library could install the requested detour.
- An exact known executable SHA-256 selects its known profile directly.
  Unknown hashes require structural candidate selection followed by complete
  local validation of every mandatory and enabled versioned contract.
- Do not write a subset of a versioned plan after failed preflight. Once
  mutation or hook installation begins, any failure is logged, shown once in
  a popup, and ends in `std::abort()`. There is no reverse rollback.
- SafetyHook v0.7.0 is the only third-party hook library. One physical detour
  owns each resolved process address. Shared Win32 targets dispatch multiple
  feature-owned typed handlers.
- Native patch and hook work does not add fake executable memory, mock hook
  engines, copied binary fixtures, callback recorders, or source-grep tests.
  Builds are static evidence; target-process behavior requires separately
  authorized runtime acceptance.
- Do not deploy a DLL, edit runtime configuration, launch the game, launch the
  NESYS process, or alter another process's lifetime while executing these
  plans unless the user explicitly expands the task.

## Dependency order

| Order | Plan | Outcome |
|---:|---|---|
| 1 | [01 Baseline and seam ledger](01-baseline-and-seam-ledger.md) | Frozen source, target, export, hook, patch, behavior-order, and seam inventory |
| 2 | [02 RuntimeImage foundation](02-runtime-image-foundation.md) | One checked loaded-image memory implementation and no rollback API |
| 3 | [03 Build detection and preflight](03-build-detection-and-preflight.md) | Typed game/NESYS identities, exact-hash fast path, feature-owned profiles, complete-plan validator |
| 4 | [04 SafetyHook registry](04-safetyhook-registry-and-minhook-removal.md) | Central SafetyHook ownership for export hooks and complete MinHook removal |
| 5 | [05 Shared Win32 dispatch](05-shared-win32-dispatch.md) | One typed dispatcher per intentionally shared export |
| 6a | [06a Compatibility, AutoPlay, and SongUnlock](06a-compatibility-autoplay-song-unlock.md) | Simple game-image patches and AutoPlay hook moved into profiles/plans |
| 6b | [06b Switch input](06b-switch-input.md) | Switch inline/mid hooks moved into the versioned barrier |
| 6c | [06c Absolute Judgement](06c-absolute-judgement.md) | Judgement hook/ABI manifest moved into the versioned barrier |
| 6d | [06d Framerate and Countdown](06d-framerate-and-countdown.md) | Large timing manifest and countdown writes moved into one validated install plan |
| 6e | [06e Test Mode Timing](06e-test-mode-timing.md) | Timing hooks, write, pointer contracts, and carrier-vtable ABI moved into a profile |
| 6f | [06f Renderer Device Loss](06f-renderer-device-loss.md) | Renderer hooks moved into the versioned barrier while preserving resource lifecycle |
| 6g | [06g Widescreen and VMT](06g-widescreen-and-vmt.md) | Widescreen manifest migrated and global vtable-slot replacement made checked |
| 6h | [06h Audio and NESYS sites](06h-audio-and-nesys-versioned-sites.md) | ASIO-close and NESYS ping fixed-RVA hooks moved into their process-specific profiles |
| 7 | [07 Configuration reflection](07-configuration-reflection.md) | reflect-cpp-backed membership and one-table bidirectional codecs |
| 8 | [08 Win32 primitives and seam deletion](08-win32-primitives-and-seam-deletion.md) | Shared text/error/HANDLE mechanics and removal of unjustified production adapters |
| 9 | [09 Locality, CMake, and startup closeout](09-locality-cmake-startup-closeout.md) | Thin composition roots, coherent targets/files, legacy audit, full static handoff |

Plans 06a through 06h are ordered because they all extend the same immutable
startup-plan model and composition root. Do not execute them concurrently in
one checkout. Plan 06g depends additionally on 06f. Plan 09 is the only point
at which the migration may be described as loader-wide complete.

Intermediate builds after Plans 03 through 06h are migration checkpoints, not
deployable compatibility releases. Do not perform runtime acceptance until
all enabled and mandatory versioned features contribute to the global barrier.

## Known image inputs

These identities were re-read from the current runtime evidence while writing
the plans:

| Process | Image variant | Size | SHA-256 |
|---|---|---:|---|
| Game | 4.71 clean `game_decrypted.exe` | 3,691,008 | `795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF` |
| Game | 4.71 legacy-patched `game471.exe` | 3,691,008 | `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522` |
| NESYS | current `NesysService.exe` | 368,640 | `487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2` |

The game descriptors identify the same 4.71 build with different known image
variants. Future 2.06 or other descriptors are added only after direct binary
and IDA evidence supplies every enabled feature profile. A capability may be
explicitly absent; a 4.71 RVA is never copied into an older profile by guess.

## Standard static verification

Run from an x86 MSVC Developer PowerShell with the ASIO SDK configured:

```powershell
$env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

If a preset contains a stale compiler cache, use `cmake --fresh --preset` for
that preset. Never patch generated cache files.

When final linkage changes, compare the Release DLL's `dumpbin /headers` and
`dumpbin /exports` output against the baseline captured by Plan 01. Static
verification must be reported separately from target-process acceptance.

## Final acceptance boundary

After Plan 09 passes static verification, request explicit authorization for
deployment and runtime tests. The runtime matrix must cover both known 4.71
images, both process roles, shared Win32 behavior, locale/crash/audio hooks,
all enabled patch families, and later every older build profile. A successful
build or CTest run does not satisfy any row in that matrix.
