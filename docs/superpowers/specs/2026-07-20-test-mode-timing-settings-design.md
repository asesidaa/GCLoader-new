# Groove Coaster Test-Mode Timing Settings Design

Date: 2026-07-20

Status: Approved design contract

## Context

Groove Coaster reads `GameTimeOffset` and `JudgTimeOffset` from
`data\system.cfg` during startup. Operators currently have to terminate the
game, edit the file, and restart the process for each timing adjustment.

Reverse engineering of the supported `game471.exe` build established that the
native test-mode UI is a parent form with dynamically allocated child slots,
an implicit null `EXIT` row, native input dispatch and repeat behavior, and
per-form virtual methods. The same analysis established the live timing
globals and native timing-manager setters. This makes it possible to add a
native-looking timing form and apply saved values without restarting the game.

The supported executable is:

- SHA-256:
  `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`
- Preferred image base: `0x00400000`

The repository is `H:\gc\artifacts\GCLoader`. `H:\gc` remains the runtime and
deployment tree and is not the destination for source, specifications, or
commits.

## Goals

- Add a dedicated `TIMING SETTINGS` entry immediately before test mode's
  existing `EXIT` row.
- Let an operator adjust music and judgment timing in 1 ms steps from
  -50 through +50 ms.
- Stage edits until an explicit Save action.
- Persist both settings to `data\system.cfg` without re-encoding or otherwise
  rewriting the file.
- Apply both settings to the running game after a successful save so the next
  tune uses them without a process restart.
- Guard every executable-image mutation and roll back this feature completely
  if its own installation is incomplete.
- Keep the feature independent of test-mode encrypted-record storage and of
  GCLoader's TOML configuration.

## Non-Goals

- Editing any timing field other than `GameTimeOffset` and `JudgTimeOffset`.
- Applying unsaved values as a live preview.
- Retiming a tune that is already in progress.
- Adding rows to the existing Audio Settings form.
- Adding a GCLoader `config.toml` key or ConfigGUI control.
- Appending fields to the native 364-byte encrypted `SystemSetting` record.
- Editing `data\TestModeLaungage` CSV files.
- Supporting an uncharacterized Groove Coaster executable build.
- Deploying a DLL or changing runtime files as part of specification or
  planning work.

## Operator Experience

### Main menu

The existing child entries remain at indices 0 through 9. The parent form gains
one child slot and exposes:

```text
...
TIMING SETTINGS
EXIT
```

`TIMING SETTINGS` is child index 10. The existing implicit null `EXIT` row
moves from index 10 to index 11. No pre-existing entry changes index or
behavior.

### Timing form

The dedicated form contains exactly four rows:

```text
MUSIC OFFSET       +0 ms
JUDGE OFFSET      -16 ms
SAVE AND BACK
CANCEL
```

The values shown above are illustrative of the current runtime file. Rendering
uses an explicit sign, a decimal value, and an `ms` suffix, including `+0 ms`
for zero. This keeps the signed value column aligned.

On every activation, the form snapshots the live `GameTimeOffset` and
`JudgTimeOffset` globals into staged values. Each staged value is clamped to
the form's -50 through +50 ms editing range. If an externally supplied live
value lies outside that range, Cancel preserves it while Save commits the
displayed clamped value.

### Controls

- Up and Down use the native form-navigation path.
- Left and Right change the selected offset by exactly 1 ms.
- Holding Left or Right uses the game's existing input-repeat schedule:
  immediate input, a 24-tick delay, and then one event every 4 ticks.
- Left and Right have no effect on `SAVE AND BACK` or `CANCEL`.
- Confirm has no effect on an offset row.
- Confirm on `SAVE AND BACK` runs the save transaction. Success returns to the
  main test-mode menu; failure keeps the form open.
- Confirm on `CANCEL` discards staged values and returns to the main menu.
- The Test/Back control from any row behaves like Cancel.

The native dispatcher exposes navigation through masks `0x01`, `0x02`,
`0x04`, and `0x08`, Confirm through `0x10`, Test/Back through `0x20`, and the
two adjustment directions through `0x40` and `0x80`. The adapter follows the
native direction mapping rather than reinterpreting these masks independently.

### Save status

On a failed save, neither live timing value changes and the form displays:

```text
SAVE FAILED - CHECK loader-log.txt
```

The message remains until the operator changes a staged value, cancels, or
successfully saves. Detailed failure information belongs only in the log.

## Chosen Implementation Approach

The feature uses a native carrier form.

A second native Sound Test form is constructed through the game's own x86 ABI.
That initialized instance supplies known-good object layout, base-form state,
allocation behavior, and destruction behavior. GCLoader replaces only that
instance's vtable pointer with a process-lifetime copy of the native table and
overrides the characterized timing-specific slots.

This approach was selected over two alternatives:

- A completely synthetic form would make GCLoader responsible for every
  object field, virtual slot, allocator rule, and destructor invariant.
- A modal overlay on the parent form would duplicate navigation and rendering
  while competing with the native active-child state machine.

The carrier minimizes the ABI surface while remaining a real child form.

## Source Architecture

The feature is a runtime patch, not a storage redirector. It lives in a new
package:

```text
src/Patches/TestModeTiming/
  CMakeLists.txt
  TimingSettingsModel.h
  TimingSettingsModel.cpp
  SystemConfigTimingStore.h
  SystemConfigTimingStore.cpp
  TimingSettingsGameAbi.h
  TimingSettingsGameAbi.cpp
  TimingSettingsPatch.h
  TimingSettingsPatch.cpp

tests/Patches/TestModeTiming/
  CMakeLists.txt
  TimingSettingsModelTests.cpp
  SystemConfigTimingStoreTests.cpp
  TimingSettingsPatchTests.cpp
```

`gc_test_mode_timing` is a separate static-library target linked into
`iDmacDrv32`. The separation prevents timing-menu policy from entering
`gc_test_mode_storage`, whose responsibility remains Kernel32 redirection for
the native encrypted records.

Responsibilities are:

- `TimingSettingsModel`: staged values, selection, clamping, dirty state,
  status state, and command transitions. It contains no Win32 or game ABI.
- `SystemConfigTimingStore`: byte-level assignment discovery and atomic file
  replacement. It contains no game addresses or UI behavior.
- `TimingSettingsGameAbi`: checked executable address resolution, native
  calling conventions, carrier layout/vtable descriptors, and live timing
  accessors.
- `TimingSettingsPatch`: carrier creation, hook ownership, model/store
  orchestration, native rendering, and feature initialization.

The runtime data flow is:

```text
native test-mode input
        |
        v
TimingSettingsPatch -> TimingSettingsModel
        |                     |
        | Save                | staged display state
        v                     v
SystemConfigTimingStore   native renderer
        |
        | atomic replace succeeded
        v
TimingSettingsGameAbi -> globals + native timing-manager setters
```

## Binary Evidence and Integration Contract

The analyzed functions and fields are:

| Role | EA | RVA |
|---|---:|---:|
| Main test-mode render | `0x573C60` | `0x173C60` |
| Main test-mode constructor | `0x573EA0` | `0x173EA0` |
| Main row-count immediate | `0x573ED5` | `0x173ED5` |
| Sound Test constructor | `0x56AE80` | `0x16AE80` |
| Test UI initialization | `0x5771D0` | `0x1771D0` |
| Base form row/child allocation | `0x4C2A00` | `0x0C2A00` |
| Active form drive | `0x4C2E40` | `0x0C2E40` |
| Input-to-virtual dispatch | `0x4C2F20` | `0x0C2F20` |
| Startup config timing load | `0x635250` | `0x235250` |
| Startup timing application | `0x63CD60` | `0x23CD60` |
| Gameplay audio sync | `0x640070` | `0x240070` |
| Tune gameplay-state initialization | `0x6624F0` | `0x2624F0` |
| Timing manager accessor | `0x401040` | `0x001040` |
| Judgment timing setter | `0x659310` | `0x259310` |
| Game/music timing setter | `0x659350` | `0x259350` |
| `JudgTimeOffset` global | `0x7D9878` | `0x3D9878` |
| `GameTimeOffset` global | `0x7D987C` | `0x3D987C` |

The only direct byte replacement approved by this design is:

| Site | Expected | Replacement | Meaning |
|---|---|---|---|
| RVA `0x173ED5` | `6A 0B` | `6A 0C` | Allocate 12 parent rows instead of 11 |

The constructor and render hooks require exact expected prologue bytes. Before
implementation mutates the image, the current IDA database must be used to
record those bytes along with:

- Carrier allocation size and allocator/deallocator path.
- Sound Test constructor and destructor calling conventions.
- Native destructor vtable slot.
- Slots for activation/reset, rendering, navigation/adjustment, Confirm, and
  Test/Back.
- Parent child-array, row-count, selection, and help-window field offsets.
- Carrier row/window/selection field offsets.
- Ownership of every Sound Test subordinate allocation created by its native
  constructor.

These are evidence gates, not design choices. They are encoded as named ABI
descriptors and assertions. If native subordinate ownership cannot be retained
or released through the native lifecycle, implementation stops rather than
orphaning allocations or substituting a guessed destructor.

## Carrier Lifecycle

The guarded row-count write expands the parent allocation before the main form
is constructed. The hooked constructor calls the original constructor first,
then performs these operations:

1. Allocate and construct a second native Sound Test form.
2. Preserve or natively release every Sound Test subordinate object according
   to the characterized destructor contract; never zero an owning pointer and
   leak the allocation.
3. Configure four visible rows and reset selection to `MUSIC OFFSET`.
4. Copy the complete native vtable into loader-owned process-lifetime storage.
5. Replace only the characterized behavioral slots and install the copied
   vtable on this instance.
6. Register the carrier in parent child slot 10.
7. Leave slot 11 null so it remains the implicit `EXIT` row.

The parent remains the carrier's owner. Its normal destruction path reaches
the retained native destructor slot. GCLoader does not free the carrier from
DLL detach.

Every unknown vtable slot stays byte-for-byte identical to the native Sound
Test table. Every adapter callback is x86-only, uses the characterized calling
convention, is `noexcept`, and catches internal failures before they can unwind
through game code.

## Parent Render Compatibility

The original main renderer has an 11-case help-text dispatch. Expanding the row
count without adapting that dispatch would index beyond its original range
when `EXIT` moves to index 11.

The render hook preserves the native cases for indices 0 through 9, supplies a
loader-owned help string for `TIMING SETTINGS` at index 10, and routes index 11
through the original Exit case 10. Any temporary selection translation is
confined to the original help dispatch and is restored before returning so
selection, highlighting, activation, and parent state remain at their true
indices.

No language CSV is modified. The new title, row labels, values, help text, and
failure message are process-lifetime loader-owned strings rendered through the
native test-mode text/window primitives.

## Installation and Failure Policy

The feature is always enabled in the game process. It does not run in the
injected NESYS process.

Initialization follows one owned transaction:

1. Resolve the main executable base and validate the supported x86 image.
2. Read and compare every complete expected byte sequence.
3. Validate all required native pointers, vtable entries, and relative-address
   arithmetic before mutation.
4. Install the constructor and render hooks.
5. Apply the guarded row-count write.
6. Publish process-lifetime ownership only after every operation succeeds.

A preflight mismatch performs no write and installs no hook. A later failure
restores the original row-count bytes and resets every hook installed by this
feature in reverse order. Incomplete rollback is reported explicitly.

`TimingSettingsPatchInit()` returns `false` on any installation failure, and
the game-process `DllMain` returns `FALSE` under the selected fail-closed
policy. Successful ownership remains for the process lifetime.

## Staged State and Live Application

The model owns:

- Original music and judgment values for the current activation.
- Staged music and judgment values.
- Selected row.
- Dirty state derived from staged versus original values.
- Save status: idle, failed, or succeeded.

Cancel and Test/Back discard the model without writing globals or disk. A
successful no-op Save returns to the parent without rewriting the file or
calling the setters.

After a changed Save completes its atomic disk replacement, the ABI adapter:

1. Writes both `GameTimeOffset` and `JudgTimeOffset` globals.
2. Calls the timing manager accessor.
3. Calls the native GameTime setter with the saved music value.
4. Calls the native JudgTime setter with the saved judgment value.

This mirrors startup application ordering. Gameplay audio synchronization
already reads `GameTimeOffset` live, while tune initialization consumes
`JudgTimeOffset`; therefore the next tune in the same process observes both
saved values.

## `system.cfg` Persistence Contract

### Path and encoding

Feature initialization resolves `data\system.cfg` against the game process's
current working directory and retains the resulting absolute path. This
matches the native startup reader's relative path context while avoiding a
later working-directory change redirecting saves.

The store treats the file as an opaque byte vector. The current file is
Shift-JIS with ASCII assignment keys, no BOM, and CRLF line endings. The store
does not decode or encode the document.

### Assignment matching

The store requires exactly one active line-start assignment for each key:

- `GameTimeOffset`
- `JudgTimeOffset`

An assignment may contain horizontal whitespace around the key, equals sign,
and value. Its value must be one optional ASCII sign followed by one or more
ASCII decimal digits. Only that signed numeric token is replaceable.

Lines whose first non-whitespace bytes are `//`, declarations in block
comments, similarly named keys, and trailing comments are not assignments for
this operation. Missing, duplicate, or malformed active assignments fail the
save. Values produced by the menu are always within -50 through +50.

### Atomic replacement

For a changed save, the store:

1. Reads the current target bytes at Save time.
2. Finds and validates both assignment tokens.
3. Produces a copy with only those tokens replaced.
4. Creates a unique temporary file in the same directory with create-new
   semantics.
5. Writes all bytes and verifies the complete byte count.
6. Flushes the temporary file to disk and closes it.
7. Replaces the existing target through the Windows replace operation with
   write-through semantics.
8. Removes the temporary file on every pre-replace failure and on a failed
   replacement when it still exists.

There is no fallback that truncates or overwrites the live file in place. The
operation preserves every non-token byte, including Shift-JIS comments, tabs,
line endings, ordering, and trailing content.

The operation does not claim to coordinate with another process concurrently
editing `system.cfg`. It reads immediately before saving and guarantees a
non-torn replacement for the bytes it read.

### Failure behavior

The following are distinct logged stages:

- Path resolution.
- Target open/read.
- Assignment discovery or validation.
- Temporary-file creation.
- Temporary-file write.
- Flush or close.
- Atomic replacement.
- Temporary cleanup.
- Live ABI application.

Every failure before replacement leaves the original file and live globals
unchanged. Install-time ABI validation makes live application a no-fail
operation after replacement; a structured-exception guard remains at the ABI
boundary and reports a fatal inconsistency if that invariant is violated.

## Diagnostics

Diagnostics are bounded and operational:

- One successful installation message identifying the supported image and
  installed carrier slot.
- Exact feature/site/stage information for preflight, hook, write, or rollback
  failure.
- On successful changed Save, old and new integer values for both settings.
- On failed Save, the path and failure stage plus Win32 status where relevant.
- No dump of the config file, Shift-JIS comments, vtable contents, or repeated
  per-frame input/render logging.

## Automated Verification

Agent-owned verification is build, unit, integration, and static evidence. It
does not claim final in-game acceptance.

### Model tests

- Activation snapshots both live values and clamps staged values to the
  approved range.
- Left and Right change only the selected offset by 1 ms.
- Both boundaries saturate at -50 and +50.
- Offset rows ignore Confirm; action rows ignore adjustment.
- Save and Cancel transitions are correct.
- Dirty state clears when adjustments return to the original value.
- No-op Save performs no store or live-apply call.
- Failed Save retains staged values and exposes the failure status.
- Test/Back follows Cancel from every row.

### Byte-store tests

- Realistic Shift-JIS and CRLF fixture bytes remain identical outside the two
  numeric token ranges.
- Positive, negative, zero, sign, and digit-width changes are correct.
- Tabs, spaces, CRLF, LF, trailing comments, and file ordering are preserved.
- Commented assignments, block-comment declarations, and similarly named keys
  are ignored.
- Missing, duplicate, and malformed active assignments fail without mutation.
- Unchanged values do not create a temporary file.
- Injected create, short-write, flush, close, replace, and cleanup failures
  report the correct stage.
- Every failure before replacement preserves the original file and prevents
  live application.
- Temporary files are removed whenever recovery is possible.

### Patch and ABI tests

- The plan contains the exact row-count descriptor and two required hooks.
- Expected and replacement row-count bytes are `6A 0B` and `6A 0C`.
- Preflight mismatch performs zero mutation.
- Every partial write/hook failure rolls back owned operations in reverse
  order and reports rollback completeness.
- Main indices 0 through 9 retain native mapping, index 10 selects timing, and
  index 11 maps to the native Exit help case without changing true selection.
- The copied vtable preserves every unoverridden slot and the native destructor
  slot.
- Carrier preparation preserves or natively releases every constructor-owned
  subordinate allocation.
- Fake ABI calls prove globals are written together and setters run only after
  successful persistence, in GameTime-then-JudgTime order.
- Callback error paths never propagate C++ exceptions.

### Build and static verification

- Configure and build the focused library and all three focused test
  executables with the repository's x86 MSVC setup.
- Run focused CTest cases, then the complete CTest suite.
- Confirm the local `game471.exe` SHA-256 matches the supported hash.
- Confirm every checked hook/write signature against the local executable.
- Inspect the produced DLL for the new menu labels and bounded diagnostics.
- Run `git diff --check` and verify that no runtime/deploy file changed.

## User-Owned Runtime Acceptance

The user performs final runtime validation:

1. Start the game with the current runtime values and verify the form displays
   Music `+0 ms` and Judge `-16 ms`.
2. Verify all existing main-menu entries still open correctly and `EXIT` works
   from its new index.
3. Verify Up/Down navigation, single-press 1 ms adjustment, native hold repeat,
   and both -50/+50 clamps.
4. Adjust both values, choose Cancel, re-enter, and confirm both original values
   remain.
5. Repeat using Test/Back instead of Cancel.
6. Save changed values, return from test mode, begin gameplay without restarting
   the process, and confirm both music and judgment timing changes take effect.
7. Inspect `data\system.cfg` and confirm only the two numeric tokens changed and
   the Shift-JIS/CRLF content remains intact.
8. Restart the game and confirm the saved values reload.
9. Make the config file read-only, attempt Save, and confirm the form stays
   open, live behavior remains unchanged, and `loader-log.txt` identifies the
   failure stage.
10. Restore the runtime file and decide whether the feature is accepted.

## Acceptance Criteria

The feature is complete when:

1. `TIMING SETTINGS` is a dedicated native child immediately before `EXIT`.
2. Music and judgment offsets stage independently in 1 ms steps within
   -50 through +50 ms.
3. Cancel and Test/Back never change live or persisted values.
4. Save changes exactly the two active numeric assignments through an atomic,
   byte-preserving replacement.
5. A failed Save leaves disk and live timing unchanged and remains actionable
   through the form and log.
6. A successful Save applies both globals and native setters so the next tune
   uses the values without restarting.
7. The patch is always-on for the supported game process, signature guarded,
   feature-transactional, and fail-closed.
8. Existing test-mode forms, controls, rendering, and Exit behavior remain
   unchanged.
9. Focused and full automated verification pass without being overstated as
   runtime acceptance.
10. The user completes and accepts the in-game checklist.
