# GCLoader Loader-Wide Cleanup Design

Date: 2026-09-05

Status: Approved design for repository review

## Context

GCLoader now has recognizable feature modules and substantially better source
ownership than its historical flat layout, but several low-level mechanisms
were independently rebuilt inside those modules. The largest remaining
duplication is in runtime-image access, versioned-site validation, hook
installation, error reporting, enum membership checks, and Win32 utility code.

The duplication is no longer merely stylistic. Equivalent operations have
different failure semantics and levels of checking. Patch implementations
repeat byte-pattern types, RVA arithmetic, guarded reads, VirtualProtect calls,
instruction-cache flushing, protection restoration, and rollback bookkeeping.
Three separate MinHook integrations resolve and install function hooks.
Kernel32 routing manually couples several otherwise independent features.

This design deepens the proven shared seams while preserving feature ownership.
It is an umbrella design implemented through independently reviewed migration
slices rather than one flag-day rewrite.

The source repository is H:\gc\artifacts\GCLoader. H:\gc remains the runtime
and evidence tree and is not changed by this cleanup unless a later task
explicitly includes deployment or runtime acceptance.

## Relationship to Existing Designs

Feature-specific approved designs remain authoritative for gameplay, audio,
input, rendering, RFID/JVS, test-mode storage, NESYS, configuration, and iDmac
behavior.

This document supersedes the following parts of the 2026-07-17 codebase
modernization design:

- MinHook as the shared hook implementation.
- Hook-set and feature rollback.
- Checked-patch rollback.
- Global atomic hook activation.
- Completion criteria that retain MinHook or rollback transactions.

The existing source layout and feature ownership decisions remain valid.

The 2026-08-07 game-binary compatibility design already uses the required
fatal-abort policy after mutation and remains compatible with this design.

## Chosen Approach

Use a staged, substrate-first cleanup.

A flag-day rewrite is rejected because it would combine infrastructure changes
with high-risk gameplay behavior. Isolated local cleanup is rejected because it
would preserve the duplicated mechanisms and inconsistent failure behavior.

Each migration slice must leave the repository buildable. A legacy
implementation is deleted as soon as its last production caller migrates.
There is no permanent compatibility interface between old and new
implementations.

## Fixed Decisions

- Mandatory and enabled versioned sites are validated as one preflight barrier
  before any versioned mutation.
- An exact known executable SHA-256 selects its known build and image variant
  directly.
- A known-hash selection does not repeat site-by-site byte verification.
- An unknown hash requires structural detection followed by complete local
  contract verification.
- Version-specific RVAs, byte and pointer contracts, ABIs, and capabilities
  remain feature-owned.
- Win32 and other exported-function hooks are not versioned contracts and stay
  outside the versioned-site barrier.
- SafetyHook v0.7.0 becomes the only third-party hooking dependency.
- Hook and patch installation is fail-fast: detailed log, one popup, abort.
- There is no reverse rollback, feature rollback, or global atomic activation.
- Arbitrary partially patched executables are not a supported compatibility
  contract. Explicit known patched-image hashes can be supported.
- Runtime handler state is complete before the corresponding hook is enabled.
- No exception crosses DLL entry points, iDmac exports, hooks, COM calls, or
  audio callbacks.
- Static verification and successful builds are not runtime acceptance.

## Goals

- Give every checked loaded-image read and write one implementation.
- Select one process build once and share the typed result.
- Keep each feature's binary knowledge beside that feature.
- Validate the complete required versioned contract before mutation.
- Give every physical hook one owner.
- Allow multiple features to share one Win32 export safely.
- Remove MinHook and its duplicated transactions.
- Distinguish code detours, global VMT slots, per-object VMT hooks, and
  synthetic vtables.
- Use reflect-cpp metadata where it is authoritative.
- Remove hypothetical one-adapter seams and obsolete synthetic-test plumbing.
- Consolidate repeated Win32 text, error, and ordinary-handle mechanics.
- Improve locality inside large feature implementations and the CMake graph.
- Preserve the iDmac ABI, configuration schema, process roles, and established
  runtime behavior.

## Non-Goals

- Adding or changing gameplay behavior.
- Inventing RVAs or ABI facts for an older game version.
- Making unsupported features silently disappear on an older build.
- Accepting arbitrary hybrid or partially patched executables.
- Adding a generic dependency-injection framework.
- Adding a generic hook-backend interface.
- Turning all patches and hooks into an untyped descriptor language.
- Replacing every explicit enum label with reflection.
- Adding fake hook engines, fake executable memory, copied binary fixtures, or
  source-grep tests.
- Introducing a new detach, unload, or worker-shutdown protocol.
- Mutating or deploying files in H:\gc as part of the cleanup implementation.

## Target Architecture

    Loader
    |-- process-role startup
    |-- configuration compilation
    |-- game or NESYS build detection
    |-- versioned-plan coordination
    |-- process-wide hook ownership
    \-- fatal startup publication

    Feature modules
    |-- validated settings
    |-- feature-owned version profiles
    |-- immutable patch and hook plan contribution
    |-- callbacks and process-lifetime state
    \-- feature-specific diagnostics

    Shared modules
    |-- RuntimeImage
    |   |-- checked RVA resolution
    |   |-- guarded reads
    |   \-- checked writes
    |-- Hooking
    |   |-- SafetyHook ownership
    |   |-- collision detection
    |   |-- shared Win32 hook points
    |   \-- VMT hook primitives
    \-- Platform/Win32
        |-- text conversion
        |-- captured Win32 errors
        \-- ordinary HANDLE ownership

Shared modules never include feature headers. Loader is the composition root.
Feature modules contribute plans and typed handlers without becoming mutually
dependent.

## RuntimeImage Module

RuntimeImage owns the mechanical correctness of accessing a loaded executable
image:

- Resolve image base plus RVA with overflow checks.
- Validate the requested span against the PE image and mapped memory.
- Perform guarded fixed-size reads.
- Represent bounded byte patterns with one type.
- Change memory protection for the exact required span.
- Perform a guarded copy or pointer exchange.
- Flush the instruction cache for executable code changes.
- Restore the original protection.
- Read back and verify a completed write.

A write is successful only if every required operation, including protection
restoration and read-back verification, succeeds. If a failure occurs after
memory changed, the caller publishes a fatal error and aborts. RuntimeImage
does not store rollback bytes and does not expose rollback.

RuntimeImage errors contain:

- Operation stage.
- Feature identifier.
- Feature-local site name.
- RVA or resolved address.
- Expected bytes or pointer.
- Observed bytes or pointer when available.
- Win32 error when applicable.

The interface does not expose injected read/write action tables. Production
memory access is its only adapter.

RuntimeImage replaces GameBinaryBytePattern, AutoPlayBytePattern, framerate
BytePattern, TimingBytePattern, the Widescreen pattern type, and the
corresponding feature-local memory implementations.

## Build Detection

Game and NESYS builds have distinct typed identifiers while sharing detector
mechanics.

Detection follows this sequence:

1. Resolve the loaded image and executable path.
2. Calculate the executable SHA-256 once.
3. Look for an exact known image descriptor.
4. If found, select its build and image variant directly.
5. Otherwise, use stable PE and structural facts to select candidate builds.
6. Require exactly one candidate.
7. Verify the complete mandatory and enabled versioned contract for that
   candidate.
8. Abort when no coherent profile matches.

A known image descriptor records:

- Typed build identifier.
- Typed image-variant identifier.
- SHA-256.
- Stable PE identity facts.
- Known state of versioned sites that differ between clean and explicitly
  supported patched variants.

The exact hash is itself the compatibility proof for a known image. The
coordinator still checks plan construction, address ranges, capabilities,
duplicates, and overlaps, but it does not reread every site merely to prove
the same file identity again.

For unknown hashes, structural detection never mutates memory. Every mandatory
and enabled site is locally verified before the candidate is accepted.

## Feature-Owned Version Profiles

There is no central RVA database. Each versioned feature exposes the
conceptual interface:

    ProfileFor(GameBuild build) -> optional<FeatureProfile>

A feature profile contains only that feature's:

- Named RVAs.
- Original byte or pointer contracts.
- Replacement bytes.
- Hook kind and callback ABI.
- Calling conventions.
- Structure offsets.
- Required capabilities.

The central coordinator understands build identity, enabled features, and
generic operation kinds. It does not understand the semantic meaning of a
feature address.

Enabling a feature for a build with no matching profile is a fatal
unsupported-build error. A disabled optional feature contributes no sites.
A mandatory feature without a profile always fails startup.

Adding an older version requires:

- A detector descriptor for that build.
- Profiles only for features supported by evidence.
- Explicit capability absence for unsupported features.
- Direct binary or IDA verification of every supplied RVA, byte contract, ABI,
  and control-flow assumption.

No address is copied from another build merely because nearby code looks
similar.

## Versioned Plan and Preflight

Each mandatory or enabled feature produces an immutable plan containing any
combination of:

- Direct byte patches.
- Inline-hook targets.
- Mid-hook targets.
- Global vtable-slot targets.
- Read-only ABI, byte, or pointer contracts.

The coordinator collects all plans and validates:

- Build capability.
- RVA and span validity.
- Required current bytes or pointers on the unknown-hash path.
- Duplicate or overlapping byte ranges.
- Code-hook target collisions.
- Byte patches overlapping a hook profile's declared protected instruction
  span.
- Required callback and original-function storage.
- Explicit installation dependencies.

Only after the complete barrier succeeds can any versioned operation install.

Installation order is explicit and behavior-preserving. It is not derived from
CMake order, static construction order, or incidental registration timing.

The lifecycle is:

    construct immutable plans
    -> validate the complete plan set
    -> install in declared order
    -> success or fatal abort

There is no Prepare, Commit, Rollback transaction interface.

## Hooking Module

Platform/Win32/Hooking becomes a concrete SafetyHook module. It owns:

- safetyhook::InlineHook objects.
- safetyhook::MidHook objects.
- Process-lifetime hook records.
- Resolved target identities.
- Original trampolines.
- SafetyHook error translation.
- Collision validation.

It deliberately has no generic backend seam. MinHook initialization,
transactions, status types, queued operations, rollback code, includes, links,
and FetchContent declarations are deleted.

### Local Hook Installation

For an inline hook whose callback needs its original trampoline, installation
uses:

    create disabled
    -> retain the hook
    -> publish the trampoline
    -> enable

The same local ordering may be used for a mid hook whose callback state must
already be published.

This is one hook's safe installation sequence, not a global activation
transaction. A creation or enable failure publishes the exact SafetyHook error
and aborts.

Successful hook ownership intentionally lasts for the process lifetime. The
registry is not destroyed during normal DLL detach. This design does not add
support for unloading GCLoader from a live process.

### Target Identity and Collisions

Hooks are keyed by resolved process address, not only by module and export
text. This detects forwarded exports and aliases that resolve to the same
function.

- An exclusive hook permits one owner.
- A named shared hook point permits multiple typed handlers behind one
  physical detour.
- Two incompatible exclusive hooks at the same address fail before install.
- Fixed-RVA or mid-hook sharing is not invented automatically.
- Exactly one physical SafetyHook detour exists at a resolved target.

## Shared Win32 Hook Points

Win32Hooks defines typed dispatch for exports intentionally shared by
features, including CreateFileA/W, ReadFile, WriteFile, FlushFileBuffers, and
CloseHandle.

Each hook point owns:

- A typed mutable call context.
- Deterministically ordered pre-call handlers.
- The one original-function invocation.
- Deterministically ordered post-call observers.
- LastError preservation.

A pre-call handler may:

- Continue unchanged.
- Transform supported arguments.
- Complete the call with an explicit result and LastError.

The dispatcher calls the original export at most once. Feature handlers do not
call it themselves. Post-call observers receive the result and captured
LastError but cannot silently replace them. The dispatcher restores LastError
before returning to game code.

Transformed argument storage remains alive for the full call. Normal calls do
not log.

Handler order is declared at the Loader composition root and preserves the
current RFID/JVS, system-path, test-mode-storage, and NESYS behavior. Feature
headers no longer appear in the Win32Hooks module.

Exports without a demonstrated sharing requirement use exclusive
registrations through the same Hooking module. This includes locale,
crash-filter, DirectSound, and most NESYS hooks.

Export resolution and collision checking are pre-install validation, but they
are not part of game-version preflight.

## VMT Mechanisms

Three mechanisms remain explicit.

### Global Vtable-Slot Hooks

Widescreen currently replaces two fixed entries in game-owned global vtables.
Those changes affect current and future instances.

A loader-owned VtableSlotHook performs:

- Versioned expected-pointer preflight.
- Checked protection change.
- Atomic compare/exchange from expected original to replacement.
- Protection restoration.
- Structured failure reporting.

It uses RuntimeImage's checked memory mechanics. SafetyHook's unprotect helper
is not sufficient because SafetyHook v0.7.0 does not expose protection-
restoration failure from that helper.

### Per-Object Virtual-Method Hooks

A future hook targeting one or more known live objects uses SafetyHook VmtHook
and VmHook. VmtHook clones the object's VMT, changes the object's vptr, and
VmHook changes one method entry in that clone.

This ownership is scoped to the object lifetime. The feature must remove the
cloned VMT before the object dies. These hooks are not retained blindly in the
process-lifetime registry.

### Synthetic or Carrier Vtables

Test Mode Timing's carrier vtable is part of constructing a game-compatible
object. It is not interception of a live object. It remains an explicit,
feature-owned table backed by the selected version profile.

## Fatal Startup Reporting

All fatal startup failures cross one deep interface:

    typed feature error
    -> detailed process log
    -> exactly one user-facing popup
    -> std::abort

The module owns:

- Once-only publication.
- Formatting fallback.
- Process-appropriate log selection.
- Popup display.
- Termination.

It exposes no StartupFatalActions table.

Feature errors remain typed. The Loader formats a tagged cause containing one
of:

- Configuration failure.
- Unsupported build or profile.
- Versioned contract mismatch.
- RuntimeImage failure.
- Hook target or collision failure.
- SafetyHook failure.
- Feature initialization failure.

Once a patch or hook may point into GCLoader, failure must not return from DLL
attach in a way that permits the DLL to unload. It always aborts.

## reflect-cpp and Enum Policy

reflect-cpp is authoritative at the configuration serialization seam.

Use reflect-cpp for:

- TOML enum serialization and parsing.
- Enumerating declared values.
- Configuration enum membership validation.
- Bidirectional names when the external token exactly equals the C++
  enumerator name.

ConfigCompiler replaces repeated enumerator comparisons with one internal
declared-value predicate based on rfl::get_enumerator_array.

rfl::string_to_enum success alone is not a membership check. reflect-cpp
v0.25.0 accepts numeric strings and converts them to the enum's underlying
value. The compiler must confirm that the resulting value appears in the
enumerator array.

Keep an explicit mapping when it carries domain meaning:

- ConfigGUI presentation labels.
- Protocol tokens.
- Stable snake-case log fields that differ from C++ identifiers.
- Hardware or vendor names.
- Hot hook and audio paths where allocation is unacceptable.
- Values requiring an explicit unknown policy.

When both explicit directions are needed, one constexpr mapping table supplies
both. Separate string-to-enum and enum-to-string switches are not retained.

reflect-cpp is not introduced into an unrelated feature merely to remove a
small domain-specific label switch.

## Shallow Seam Cleanup

There is no blanket rule against types named Actions or Api. Every seam receives
the deletion test.

Remove a seam when:

- It has one production adapter.
- Its only variation is a fake or removed synthetic test.
- Callers must understand nearly its complete implementation.
- Deleting it makes complexity disappear rather than reappear across callers.

Known removals are:

- HidApi.
- StartupFatalActions.
- MinHook resolver and operation tables.
- Audio's embedded MinHook table.
- Feature-specific executable-memory action tables.
- Public Widescreen hook-action types with no external production caller.

Likely candidates requiring a focused caller audit include:

- Configuration and startup filesystem action tables.
- IME action tables.
- ASIO registry and COM action tables.
- Card-worker and foreground action tables.
- Raw-input action tables.

Retain a seam when the deletion test proves depth or real runtime variation.
Examples include dynamically loaded XInput functions, process-launch behavior
shared by ASIO probe and control-panel clients, and actual platform or hardware
variation.

A fake test adapter alone does not justify a production interface. Where a
decision is valuable to test, separate the pure decision from direct platform
I/O instead of exposing the complete platform operation table.

## Shared Win32 Primitives

A narrow Platform/Win32 target owns:

- Strict UTF-8 to UTF-16 conversion.
- Strict UTF-16 to UTF-8 conversion.
- Formatting a previously captured Win32 error.
- Movable ownership of ordinary HANDLE values closed only by CloseHandle.

Callers retain feature policy:

- ANSI code-page choice.
- Registry byte limits.
- Empty-string behavior.
- Fallback text.
- User-facing wording.
- Allocation restrictions on hot paths.

Specialized lifecycle remains specialized. Thread joining, pipe disconnection,
RegCloseKey, COM release, MMCSS restoration, and intentionally leaked
process-lifetime state do not pass through the ordinary HANDLE owner.

The target is not a general string or resource collection.

## Improving Locality

Files are not split merely because they are large. A split is justified when
unrelated behavior crosses the same implementation.

Target changes include:

- AudioPatch becomes small feature composition. DirectSound export hooking
  moves beside DirectSound, and ASIO-close handling moves beside ASIO.
- Kernel32Hooks owns typed export adapters and dispatch only. Feature routing
  policy stays inside each feature.
- FrameratePatch becomes composition over frame timing, menu timing, effect
  timing, gameplay clock, and feature-owned profiles.
- WindowedWidescreenPatch separates the hook manifest, window policy, render
  hooks, gameplay-HUD hooks, and network-status hooks.
- ConfigCompiler retains one public Compile interface while internal
  validation moves beside the corresponding settings domains.
- DllMain becomes a thin outer adapter over explicitly separate game and NESYS
  startup functions.
- Diagnostic modules separate record collection from formatting and sink
  publication.

The design does not introduce a generic feature registry. Game and NESYS
startup have materially different ownership and remain explicit.

## CMake Architecture

The coarse gc_runtime_patches target is divided only where dependency
direction improves. The final internal target ownership is:

- gc_platform_win32
- gc_hooking
- gc_runtime_image
- gc_game_version
- gc_patch_game_compatibility
- gc_patch_auto_play
- gc_patch_song_unlock
- gc_patch_framerate
- gc_patch_countdown
- gc_test_mode_timing
- gc_renderer_device_loss
- gc_windowed_widescreen
- gc_game_startup
- gc_nesys_startup

Rules:

- Only gc_hooking names the external SafetyHook target. SafetyHook headers may
  be exposed transitively where the callback ABI requires them.
- Only gc_config depends directly on reflect-cpp.
- Shared targets never depend upward on Loader or unrelated features.
- Feature targets own their profiles and callbacks.
- Final target and CTest executable names remain stable.
- MinHook disappears from dependency declarations and source audits.
- A new static-library target must represent a coherent module, not one source
  file for its own sake.

## Runtime Startup Flow

Common startup:

1. Detect the process role.
2. Select and initialize the process-specific log.
3. Load and compile the strict configuration.

Game startup:

1. Detect the game build and image variant.
2. Ask mandatory and enabled game features for versioned plans.
3. Validate the complete versioned plan set.
4. Build and validate non-versioned export-hook registrations.
5. Construct and publish feature runtime state and shared handler tables.
6. Install versioned patches and hooks in declared order.
7. Install non-versioned hooks in declared order.
8. Return successfully from attach.

NESYS startup:

1. Detect the NESYS build where fixed-RVA behavior is enabled.
2. Build and validate only NESYS-process plans.
3. Construct and publish NESYS runtime state.
4. Install its versioned operations and export hooks.
5. Return successfully from attach.

Any failure invokes the fatal reporter and does not return.

## Migration Sequence

### Slice 1: Baseline and Seam Ledger

- Record Debug and Release build status, CTest inventory, DLL exports, patch
  sites, hook sites, and target graph.
- Record every Actions or Api seam and its keep/remove reason.
- Record the existing behavior order for every shared Kernel32 export.

### Slice 2: RuntimeImage Foundation

- Add the common pattern, address, read, and write implementation.
- Migrate mandatory game-compatibility patches and Countdown first.
- Remove their superseded memory implementations.

### Slice 3: Build Detection and Profiles

- Add typed build and image-variant identities.
- Add known executable descriptors.
- Add feature-owned profile selection.
- Establish the global versioned preflight barrier.

### Slice 4: Unified Hooking

- Add concrete SafetyHook ownership and collision validation.
- Migrate crash, locale, RFID/Win32, NESYS, and audio export hooks.
- Delete all MinHook implementation and dependency declarations.

### Slice 5: Shared Win32 Dispatch

- Replace direct Kernel32 feature coupling with typed handler chains.
- Preserve the exact current routing and observation order.
- Preserve arguments, return values, output writes, and LastError.

### Slice 6: Versioned Feature Migration

- Migrate AutoPlay and SongUnlock.
- Migrate Switch input, Framerate, and Countdown.
- Migrate Test Mode Timing.
- Migrate Renderer Device Loss and Widescreen, including global VMT slots.
- Migrate fixed-RVA NESYS behavior separately in the NESYS process.

High-risk feature families may receive separate implementation plans within
this slice.

### Slice 7: Configuration and Reflection

- Replace repeated enum membership lists.
- Consolidate genuinely bidirectional mappings.
- Preserve presentation, protocol, and stable diagnostic labels.

### Slice 8: Platform and Seam Cleanup

- Consolidate Win32 text, captured error, and ordinary HANDLE mechanics.
- Remove every hypothetical adapter identified by the seam ledger.
- Remove obsolete synthetic-test machinery only when its production seam is
  removed.

### Slice 9: Locality and CMake Closeout

- Split large implementations by coherent behavior.
- Narrow target dependencies.
- Reduce Loader to role-specific composition and fatal publication.
- Audit that no migration bridge or legacy implementation remains.

Each slice receives its own implementation plan and review. The slices are not
combined into one broad code change.

## Verification

For every migration slice:

- Configure and build the complete x86 Debug graph.
- Configure and build the complete x86 Release graph.
- Run applicable focused tests.
- Run the complete CTest suite.
- Run git diff --check.
- Inspect repository status.
- Compare final DLL exports and ordinals when final linkage changes.
- Confirm that the superseded implementation has no caller before deleting it.

Do not introduce fake hook engines, fake executable memory, copied executable
fixtures, source-grep tests, or callback recorders as proof of native behavior.

Configuration changes are tested through production parsing and compilation.
Pure deterministic decisions may be tested only through their real interface
and may claim only that decision.

Runtime hook and patch integration requires explicit target-process
acceptance. Builds and static checks do not establish:

- Successful game or NESYS startup.
- Correct hook execution.
- Input feel.
- Audio quality or pacing.
- RFID/JVS behavior.
- Test-mode storage behavior.
- Framerate or countdown behavior.
- Widescreen rendering or device-loss recovery.

Runtime acceptance is performed separately for:

- Known clean and known patched 4.71 images.
- Every future older profile.
- Game and NESYS process startup.
- RFID/JVS, system-path, and test-mode-storage sharing.
- NESYS request observation.
- Locale and crash handling.
- DirectSound, WASAPI, and ASIO selections.
- Framerate, Countdown, Switch input, AutoPlay, and SongUnlock.
- Test Mode Timing.
- Widescreen rendering, VMT hooks, and device-loss recovery.

## Completion Criteria

The cleanup is complete when:

- One RuntimeImage implementation owns all checked loaded-image writes.
- One build detector selects feature-owned version profiles.
- All mandatory and enabled versioned sites pass one barrier before mutation.
- SafetyHook is the only third-party hook library.
- No MinHook source, include, status, link, or package declaration remains.
- One physical detour exists per resolved address.
- Shared Win32 exports use one typed dispatcher.
- Global VMT slots, per-object VMT hooks, and synthetic vtables use their
  correct distinct mechanisms.
- No rollback transaction implementation remains.
- Any installation failure logs, displays one popup, and aborts.
- Configuration enum membership derives from reflect-cpp metadata.
- Explicit string mappings remain only where they carry domain meaning.
- Every retained Actions or Api seam passes the deletion test.
- Shared Win32 mechanics have one implementation.
- Large feature modules have coherent internal locality.
- Game/NESYS separation, iDmac ABI, strict configuration, and established
  runtime behavior remain intact.
