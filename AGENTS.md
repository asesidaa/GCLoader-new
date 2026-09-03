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

Configure, build, and test with the checked-in presets:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4

cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

If a build tree contains a stale MSVC cache, refresh that preset rather than
patching generated cache files:

```powershell
cmake --fresh --preset msvc32-debug
```

- Build the affected targets and run focused tests while iterating.
- Before completing a production change, build the complete affected preset
  graph and run the full suite. Use both Debug and Release when the change can
  depend on optimization, ABI, memory layout, timing, or configuration.
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

For reverse-engineering and native runtime-patch work, do not use TDD and do
not add or extend unit tests, fake executable memory, synthetic backends,
mocks, copied binary fixtures, callback recorders, or standalone verification
scripts unless the user explicitly requests that exact testing work. This rule
overrides the general focused/full-suite guidance above for that class of work.
Establish the native contract by direct IDA/binary analysis, use compilation as
build proof, and reserve behavioral claims for an explicitly authorized run in
the real target process.

Do not calculate or compare hashes of analysis scripts or generated analysis
artifacts in specifications, plans, implementation work, or verification.
Their helper implementation and serialization are not native-contract
evidence. Check the relevant RVAs, bytes, decoded instructions, ABI, ownership,
and control flow directly against the actual target instead.

Every test must have a plausible regression it can catch. Prefer tests of:

- observable behavior and public or production-facing contracts;
- boundary values, invalid input, and failure behavior;
- protocol encoding, decoding, and state transitions;
- concurrency, ownership, lifetime, and real-time invariants;
- preflight rejection, partial failure, and transactional rollback;
- transformations whose expected result is independently derived.

Do not add tests merely because a workflow requests tests. In particular, avoid:

- CMake or script tests that grep implementation source for names, tokens, or
  regex patterns;
- copied production RVA tables, byte manifests, defaults, or lookup tables that
  are updated in lockstep with the implementation and provide no independent
  oracle;
- tests that exercise only a test-local helper or restate a trivial inline
  expression;
- enormous fixtures that duplicate `config.toml` instead of exercising the
  distributed file and production parser;
- one-test-per-function or coverage-padding tests with no meaningful failure
  mode.

Exact binary fixtures are appropriate only when their provenance is independent
and the test explains what accidental change they protect. Prefer invariant and
transaction tests for patch-plan plumbing; keep authoritative binary evidence
in the relevant reverse-engineering record.

Static tests must not be written or reported as proof of game behavior, native
hook integration, renderer behavior, visual placement, timing feel, or runtime
acceptance. In particular, models, mocks, fake devices, synthetic executable
memory, copied constants, and test-only callback wrappers are not acceptable
oracles for those claims. Do not write a static test whose expected result
asserts any such behavior, and do not add tests that merely encode the intended
design and then assert that the design is correct.

The only exception is a narrowly scoped property whose result is formally
derivable from an authoritative, independently sourced contract. Such a test
may claim only that property; it still must not be promoted into evidence that
the target game executes the relevant path or that the user-visible result is
correct. Runtime acceptance requires the actual deployed artifact to execute in
the target process and the relevant behavior to be observed there.

Automated tests prove only what they execute. Keep build/static proof separate
from in-game acceptance, and do not report gameplay success without the user's
runtime confirmation.

## Runtime Evidence and Acceptance

- For reverse engineering, recheck the current `game471.exe` binary or
  `game471.exe.i64` database rather than trusting stale addresses, planning
  prose, or field-name guesses.
- Treat install-time byte guards, compilation/linkage, export inspection, and
  artifact inspection as static evidence only. A permitted formal-property
  test is evidence solely for the exact independent contract it executes;
  neither focused tests nor a full CTest run are evidence of game behavior.
- Treat boot, NESYS child startup, input feel, card scanning, storage behavior,
  audio quality, high-FPS timing, and shutdown behavior as runtime acceptance.
- When runtime behavior is in scope, use `loader-log.txt` and
  `loader-service-log.txt` as process-specific evidence and state clearly which
  artifact was actually tested.

## Process Lifecycle Safety

- Never stop, terminate, kill, restart, close, or otherwise alter the lifetime
  of any process, service, IDE, or application window unless the user
  explicitly authorizes that exact process-lifecycle action.
- Authorization to inspect a process or close an editor file/tab is not
  authorization to close its hosting application.
- If the requested MCP or IDE operation is unavailable, report the missing
  capability and ask the user. Never invent a substitute using keystrokes, UI
  automation, `Stop-Process`, `taskkill`, `TerminateProcess`, or equivalent
  process/window controls.
- More generally, if an operation is unclear or the required capability is not
  documented and available, stop and ask the user explicitly before attempting
  any workaround or alternative mechanism.

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

## User Input

-  When calling the `request_user_input` tool, never set `autoResolutionMs`. Wait for the user to answer explicitly.
