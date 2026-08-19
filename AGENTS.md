# GCLoader Repository Guide

## Repository Scope

GCLoader is a replacement `iDmacDrv32` DLL that adapts Groove Coaster's
arcade-machine interfaces to local Windows input, audio, storage, network, and
runtime-patch behavior.

- `H:\gc\artifacts\GCLoader` is the Git source repository. Keep source,
  configuration, tests, documentation, plans, and commits here.
- `H:\gc` is the runtime and deployment tree. Its executable, IDB, logs, and
  deployed files may be inspected as evidence, but do not deploy to or mutate
  that tree unless the task explicitly includes runtime deployment.
- Production code belongs under `src/`, ConfigGUI code under `tools/`, and
  tests under the matching feature directory in `tests/`.
- Preserve unrelated working-tree changes and do not edit generated build
  trees as source.

## Build and Verification

The project targets Windows x86, uses C++23, and links the static MSVC runtime.
Run CMake from an x86 MSVC developer environment.

Configure and build with the checked-in presets:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug

cmake --preset msvc32-release
cmake --build --preset msvc32-release
```

If a build tree contains a stale MSVC cache, refresh that preset rather than
patching generated cache files:

```powershell
cmake --fresh --preset msvc32-debug
```

- Build the affected targets while iterating.
- Before completing a production change, build the complete affected preset
  graph. Use both Debug and Release when the change can depend on optimization,
  ABI, memory layout, timing, or configuration.
- For documentation-only changes, reference checks and diff validation are
  sufficient unless the documentation changes executable commands or behavior.
- Always run `git diff --check` and inspect `git status` before committing.

## Architecture and Correctness

- Keep game-process and NESYS-process behavior explicitly separated. Do not
  initialize game-only input, audio, RFID/storage, or gameplay patches in the
  NESYS process.
- Preserve feature ownership. Sharing Kernel32 or hook infrastructure does not
  make RFID/JVS, test-mode storage, NESYS, input, audio, or runtime patches one
  feature.
- Preserve the exported iDmac ABI: DLL/export names, ordinals, x86 calling
  conventions, established return values, and initialized caller outputs.
- Input polling owns keyboard and controller capture and publishes booster
  snapshots. FastIO projects those snapshots into register values; the iDmac
  exports remain thin driver-contract adapters.
- Do not let exceptions cross `DllMain`, an exported iDmac function, a hooked
  Win32 function, a COM interface, or an audio callback.
- Runtime `config.toml` is strict and complete. ConfigGUI defaults do not make
  missing runtime fields optional. Runtime and ConfigGUI must use the same
  parsing, keycode codecs, and semantic validation.
- A runtime patch is a guarded executable-image change. Base it on current
  executable or IDB evidence; give every site a named RVA and expected original
  bytes; check address arithmetic, accessibility, and bytes before mutation;
  and roll back earlier writes or hooks when a transaction fails.
- Use "hook" only when a detour is installed. Direct checked byte writes are
  runtime patches, not hooks.
- Preserve forwarded Win32 arguments, return values, and last-error behavior.
- Keep normal polling, render, resolver, audio-callback, and successful-hook
  paths free of per-call diagnostic logging.
- Prefer existing package targets and shared infrastructure. Add a new
  abstraction only when at least two production callers demonstrate a real
  shared seam.

## Test Policy

The unit-test suite was intentionally removed. It repeatedly encoded expected
values derived from the same unproven model as the implementation, so incorrect
designs passed while actual game behavior remained broken. Do not restore the
suite, add test targets, or add tests merely to satisfy TDD or workflow policy.

A new automated test is prohibited unless every asserted expectation has an
independent, documented oracle that is formally and strictly derived from one
of these sources:

- verified bytes, disassembly, ABI, or control flow from the supported game
  binary/IDB;
- a mathematically proven identity whose assumptions are also verified; or
- recorded behavior from the actual game or an external protocol authority.

Expected values copied from the implementation, chosen by the design, produced
by a loader-side emulation of native behavior, or updated until a test turns
green are not evidence and must not be tested. If a qualifying formal oracle is
not available, use binary inspection, guarded runtime instrumentation, build
checks, and actual game runs instead.

Compilation proves only that the code builds. Static binary evidence proves
only the facts inspected. Gameplay correctness and acceptance come from the
supported executable and observed game behavior, not from a simulated test
model.

## Runtime Evidence and Acceptance

- For reverse engineering, recheck the current `game471.exe` binary or
  `game471.exe.i64` database rather than trusting stale addresses, planning
  prose, or field-name guesses.
- Treat install-time byte guards, focused tests, full CTest results, export
  inspection, and artifact inspection as static evidence.
- Treat boot, NESYS child startup, input feel, card scanning, storage behavior,
  audio quality, high-FPS timing, and shutdown behavior as runtime acceptance.
- When runtime behavior is in scope, use `loader-log.txt` and
  `loader-service-log.txt` as process-specific evidence and state clearly which
  artifact was actually tested.

## Domain Terminology

### Processes and driver surface

**Game process**

The `game471.exe` process. It receives every GCLoader feature, including iDmac,
input, audio, RFID/JVS, storage, NESYS, and runtime patches.

Avoid: client, main app.

**NESYS process**

The injected `NesysService.exe` process. It receives only the NESYS and
process-logging behavior selected for that process role.

Avoid: service when the distinction from a Windows service matters.

**iDmac**

The driver-facing arcade hardware contract exposed through `iDmacDrv32.dll`;
repository casing follows the binary name. The vendor product is iDMAC, short
for Intelligent DMA Controller.

Avoid: Dmac, generic DMA.

**FastIO**

The register-facing arcade input and I/O behavior reached through iDmac register
operations. FastIO values include board identity, status, digital input,
analog, coin-slot, and GPIO registers.

Avoid: input manager, iDmac itself.

### Arcade features

**Booster input**

The logical left- and right-booster directions and buttons used by Groove
Coaster. FastIO field labels such as `p1_up` are transport labels and are not
physical booster names.

Avoid: player-one direction when describing the logical control.

**RFID/JVS**

The emulated RFID reader and its JAMMA Video Standard serial protocol behavior
exposed through the virtual COM port. RFID state, JVS framing, and Taito
commands are distinct responsibilities inside this feature.

Avoid: card hook as a name for the whole feature.

**Test-mode storage**

The redirection policy for Groove Coaster test-mode files and related
filesystem queries. It is independent of RFID/JVS even though both currently
share Kernel32 hook routing.

Avoid: RFID storage.

**NESYS**

The Taito network environment emulated for both the game process and NESYS
process, including process launch, synthetic adapter, resolver, ping, and
registry behavior.

Avoid: networking when registry or process-launch behavior is also meant.

**Runtime patch**

A guarded change to the loaded Groove Coaster or NESYS executable image,
identified by an RVA and expected original bytes. Runtime patches include
framerate, countdown, Switch-input, and NESYS ping behavior.

Avoid: hook when no detour is installed.

**Exclusive audio pipeline**

The DirectSound-compatible, miniaudio-mixed, WASAPI-exclusive output path used
when the experimental audio feature is enabled.

Avoid: WASAPI patch when referring to the complete pipeline.

## Flagged Ambiguities

- **iDmac vs. DMA:** use iDmac for the driver and emulated hardware contract.
  Use DMA only for the generic transfer mechanism or exported DMA operations.
- **NESYS process vs. Windows service:** use NESYS process for
  `NesysService.exe`; do not imply that every reference describes Windows
  Service Control Manager behavior.
- **FastIO labels vs. booster directions:** preserve exact config/register
  labels at transport seams, but use booster language inside input behavior.
