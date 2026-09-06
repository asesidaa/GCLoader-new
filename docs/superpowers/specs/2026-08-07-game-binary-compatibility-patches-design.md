# Game Binary Compatibility Patches Design

Date: 2026-08-07

The 2026-09-06 port adds game 2.06 and independently selected NESYS 2.86.1
profiles through the refactored startup plan. Its compatibility writes are
`0xA3FF6` (mouse), `0xF7E9B` (dongle failure), `0xF90F6` (transmit)
and `0x2B68C7` (COM character). Known SHA-256 identities select their exact
profile; unknown hashes require structural selection and full local
contracts. Only the known 4.71 variant admits already-installed compatibility
writes. See the [port record](../../reverse-engineering/gc206-implementation-2026-09-06.md).
The historical 4.71 evidence below remains applicable; current installation
uses the shared approval barrier and fatal failure, without reverse rollback.

## Context

GCLoader has historically run with `H:\gc\game471.exe`. That executable is a
memory-derived image whose file already contains several intentional game
patches, a resolved Import Address Table, and writable global state captured or
repaired after the image was loaded. Existing loader behavior therefore
implicitly depends on modifications that are not present in a clean executable.

`H:\gc\game_decrypted.exe` is the clean decrypted image of the same game build.
The loader must establish the four intentional compatibility changes at process
startup so operators no longer need a modified executable on disk. It must not
replay resolved imports or writable dump state against the clean image.

The implementation belongs in `H:\gc\artifacts\GCLoader`. The runtime tree at
`H:\gc` supplies binary evidence only and is not changed by this feature.

## Binary Evidence

The two files have the same size and PE layout but different SHA-256 hashes:

| Image | SHA-256 |
|---|---|
| Clean `game_decrypted.exe` | `795AB03F944BA7716AB257869C6BA394D19288E6484A17FACF1600ED377595DF` |
| Legacy patched `game471.exe` | `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522` |

Both images identify the supported build with these PE properties:

| Property | Value |
|---|---:|
| Machine | `IMAGE_FILE_MACHINE_I386` (`0x014C`) |
| COFF timestamp | `0x5FA90825` |
| Preferred image base | `0x00400000` |
| Entry-point RVA | `0x0010964A` |
| Size of image | `0x00433000` |
| Size of headers | `0x00000400` |
| Section count | `5` |

A byte-for-byte comparison found 2,067 changed bytes. Every difference is
accounted for:

| Region | Changed bytes | Classification |
|---|---:|---|
| PE headers | `0` | Identical |
| `.text` | `8` across three sites | Intentional instruction patches |
| `PTLCRYPT` | `0` | Identical |
| `.rdata` IAT (`RVA 0x002AD000`, size `0x664`) | `1,497` | Normal import resolution in the memory-derived image |
| `.rdata` outside the IAT | `1` | Intentional `COM1` to `COM2` patch |
| Writable `.data` | `561` across 129 ranges | Captured or repaired mutable global state |
| `.rsrc` | `0` | Identical |

Consequently, exactly four intentional changes must be recreated. No header,
`PTLCRYPT`, resource, resolved-IAT, or writable-global difference belongs in the
runtime patch set.

### Patch sites

Addresses below are RVAs from the loaded main executable. The loader resolves
the actual module base and does not assume that the preferred base is the
runtime base.

| Name | RVA | Clean bytes | Legacy patched bytes | Effect |
|---|---:|---|---|---|
| Disable native mouse events | `0x000B0896` | `75 02` | `90 90` | In `GWPCGWDeviceMouse` update logic, skip buffered DirectInput mouse-button event consumption |
| Bypass dongle failure | `0x00102C7B` | `75 3B` | `EB 3B` | Always take the success continuation after the recurring dongle security check |
| Suppress dongle security transmit | `0x00103EE6` | `E8 45 F6 FF FF` | `90 90 90 90 90` | Remove the security-interval call that submits the dongle packet |
| Select emulated RFID port | `0x002F7AC3` | `31` | `32` | Change the embedded `COM1` string at `RVA 0x002F7AC0` to `COM2` |

The semantic findings were verified against both `game471.exe.i64` and
`game_decrypted.exe.i64` through reusable daemon-backed IDA sessions. The mouse
site belongs to the `GWPCGWDeviceMouse` vtable method. The two dongle sites are
inside the recurring security path. The COM string has one code reference from
the game's serial-device initialization path.

## Goals

- Let the clean `game_decrypted.exe` reach the same intentional compatibility
  baseline as the legacy prepatched image.
- Accept every combination of clean and already-patched sites, including the
  exact legacy `game471.exe` state, and write only missing patches.
- Decide compatibility from the four patch-site bytes rather than PE version
  metadata. The DLL having loaded already establishes a usable x86 process
  image; timestamp, entry point, image base, and other PE identity fields do
  not determine whether these four writes are applicable.
- Validate every site before any mutation.
- Abort startup immediately if a required write fails. Earlier successful
  writes remain only in the process that the fatal path terminates.
- Show a clear user-facing error when the loaded executable is unsupported.
- Keep detailed failure evidence in `loader-log.txt`.
- Run only in the game process and never patch the NESYS process.

## Non-Goals

- Copying, rewriting, renaming, or deploying either executable.
- Reproducing the memory dump's resolved IAT.
- Reproducing any writable `.data` value from `game471.exe`.
- Discovering patch sites through signatures, scanning, or heuristics. The four
  verified RVAs remain fixed.
- Making any of the four required patches configurable.
- Replacing the four direct writes with detours.
- Refactoring unrelated framerate, renderer, input, RFID, or NESYS patch code.
- Treating build or static checks as proof of successful in-game behavior.

## Architecture

Add a focused game-process bootstrap unit under
`src/Patches/GameCompatibility/`. It owns the four patch contracts, per-site
state classification, checked memory operations, selective installation, and
structured errors.

The public entry point returns a result instead of displaying UI itself:

```cpp
std::expected<GameBinaryPatchResult, GameBinaryPatchError>
GameBinaryPatchInit() noexcept;
```

`GameBinaryPatchResult` reports either `PatchedImage` or
`AlreadyPatchedImage`. `GameBinaryPatchError` identifies the failure stage,
contract name, RVA, expected clean and patched patterns, actual bytes when
available, and any Windows error.

The unit exposes a test seam that accepts an image base and injected read/write
actions. Production actions use guarded memory access, `VirtualProtect`, and
`FlushInstructionCache`. Tests use a synthetic image and deterministic failure
injection. The fixed patch manifest remains production-owned; no general patch
framework or unrelated refactor is introduced.

`src/Loader/DllMain.cpp` invokes the unit only when process-role detection says
the current process is the game. It converts structured errors into log and
modal text through the existing `SystemPath/StartupFatal` facility.

## Applicable Image State Model

Preflight reads all four sites before any write. Each site is classified
independently:

| Site state | Definition | Result |
|---|---|---|
| Clean | Bytes exactly match the verified clean pattern | Queue this site's replacement write |
| Already patched | Bytes exactly match the verified patched pattern | Accept without writing this site |
| Unknown | Read succeeds, but bytes match neither pattern | Reject as unsupported before any write |
| Unreadable | Guarded read fails | Report a setup failure before any write |

Any combination containing only clean and already-patched sites is applicable.
This includes the all-clean image, the all-patched legacy image, and every
partially patched image. An all-patched image returns `AlreadyPatchedImage`
with zero writes. Every other applicable combination writes its clean sites and
returns `PatchedImage`.

Repeated initialization after successful selective installation observes all
four patched patterns and is harmless, although `DllMain` still initializes
only once per process attach.

## Installation Flow

Initialization performs these steps in order:

1. Resolve the loaded main-module base with `GetModuleHandleW(nullptr)`.
2. Check every `base + RVA` calculation for integer overflow.
3. Read all four sites through guarded memory access. Do not read or validate PE
   headers or version metadata.
4. Classify every site without writing and reject if any site is unknown.
5. Return immediately when all four sites are already patched.
6. Write only clean sites, preserving manifest order and skipping sites already
   patched on entry.
7. For each write, change only the containing range to executable/read/write,
   copy the exact bytes, flush the process instruction cache, and restore the
   original protection.
8. Publish success only after every required write and protection restoration
   succeeds.

If a write, cache flush, or protection restoration fails, installation stops at
that site. No rollback is attempted. The existing startup-fatal path logs the
failure, displays the setup error, terminates the process, and invokes fail-fast
as a fallback. Any earlier writes are process-local and disappear with that
terminated process.

## Startup Ordering

The patch step is the first game-specific executable mutation:

1. Detect the current process role.
2. Initialize the process-specific log.
3. If this is the game process, validate and install the binary compatibility
   patches.
4. Continue with locale compatibility, crash-dump setup, configuration, system
   paths, RFID/Kernel32 hooks, renderer recovery, audio, framerate, and Switch
   input initialization.

The NESYS process skips step 3 entirely. Applying the COM patch before RFID
initialization ensures the game's subsequent serial open targets the existing
emulated `COM2` route. Applying the dongle and mouse changes before other hooks
recreates the baseline on which prior GCLoader behavior was developed.

## Error Handling and User Prompt

Every error is fail-closed. No other game feature initializes after this step
fails.

Unknown patch-site bytes use the title:

```text
GCLoader unsupported game version
```

The modal states that the loaded executable does not contain a supported clean
or already-patched pattern at the named site. It includes the failing RVA and
directs the operator to `loader-log.txt` for the exact byte comparison.

The log includes:

- failure stage;
- patch name and RVA;
- expected clean bytes;
- expected legacy-patched bytes;
- actual bytes when the read succeeded;
- Windows error for memory-operation failures.

Address overflow, read, protection, write, cache-flush, or protection-restore
failures use a game-patch setup error title rather than misreporting an
applicable binary as unsupported. Both paths reuse the existing one-shot
startup fatal sequence: log, modal dialog, process termination, and fail-fast
fallback. No exception may cross `DllMain`.

Successful startup logs exactly one concise state line:

```text
GameBinaryPatch: state=patched sites=4
```

or:

```text
GameBinaryPatch: state=already_patched sites=4
```

There is no per-frame or per-call logging.

## Testing

Add `GameBinaryPatchTests` under `tests/Patches/`. The tests exercise observable
installation behavior through an injected memory API.

The exact clean and patched site bytes form a small binary fixture whose
provenance is the independently hashed `game_decrypted.exe` and `game471.exe`
pair documented above. This fixture protects the supported binary contract; it
is not generated from or source-grepped out of the production manifest.

Required cases are:

- All 16 combinations of clean and already-patched sites are accepted, write
  only their clean sites, and finish with all four patched patterns.
- The all-clean image performs exactly four writes; the all-patched legacy
  image performs zero writes.
- Installation reads only the four patch sites and never consults PE headers or
  version metadata.
- An unknown byte at each individual site is rejected with that contract and
  RVA, with zero writes.
- A site-read failure is reported with zero writes.
- A failure at each required write position stops immediately, reports that
  site and memory stage, performs no later writes, and does not roll back
  earlier writes.
- Invalid injected actions and overflowing addresses are rejected without
  memory access.
- Re-running installation after successful selective writes returns the
  already-patched result without additional writes.

Tests do not patch a live process and do not duplicate the entire executable.
No source-text, regex, or nominal-coverage tests are added.

## Verification and Acceptance

Implementation verification requires:

1. Focused Debug and Release builds and execution of
   `GameBinaryPatchTests`.
2. Full `msvc32-debug` and `msvc32-release` builds and CTest suites.
3. `git diff --check` and final status inspection.
4. Artifact inspection confirming the production DLL contains the unsupported
   version prompt and the four named patch contracts.

These checks prove compilation, patch-site applicability handling, selective
write behavior, and loader integration. They do not prove gameplay behavior.

Runtime acceptance remains separate and requires an operator-run game process:

- clean `game_decrypted.exe` boots with `state=patched`;
- legacy `game471.exe` still boots with `state=already_patched`;
- a partially patched executable boots with `state=patched` and only its clean
  sites are written;
- the game opens the emulated `COM2` RFID path;
- the recurring dongle path does not suspend the game;
- native buffered mouse-button events remain disabled as in the legacy image;
- a deliberately unsupported executable copy displays the version prompt and
  does not continue initialization.

Deployment or modification of `H:\gc` is not part of implementation unless the
user explicitly requests it.
