# Fullscreen Alt+Tab Device-Loss Recovery Design

## Goal and Scope

Keep the supported `game471.exe` process alive when the game is running in
fullscreen, the player Alt+Tabs to another application, and then Alt+Tabs back
to the game. The supported failure window is the Direct3D 9 sequence in which
`Present` reports device loss, the game releases default-pool resources,
`Reset` becomes possible, and the renderer lazily recreates its streaming
buffers on a later frame.

`CheckDeviceLost=1` is a support assumption. The patch neither reads nor
modifies `system.cfg`, and it adds no loader configuration surface. Generic
allocation failure, corrupt assets, unsupported executables, and arbitrary
renderer corruption are outside this design.

## Evidence and Root Cause

The earlier dump `game471.1DD2113D7FDFDC4.crash.dmp` captured
`0xC0000417` (`INVALID_CRUNTIME_PARAMETER`) after the renderer's dynamic
vertex-buffer creation failed. Initialization continued with an empty vertex-
buffer vector and reached the checked access at `0x004E7B31`.

The later dump `game471.1DD22990B268BA4.crash.dmp`, produced on another
computer, contains the exact empty renderer state at the second checked access:
`ECX=0`, `EDI=0`, and `EBX=0`, with stack addresses `0x004E5581` and
`0x004E6B84`. In the current IDB, `0x004E557C` calls
`_invalid_parameter_noinfo` when the vertex-buffer vector is empty, and
`0x004E5581` is the instruction immediately after that call.

The current `game471.exe.i64` establishes the common lifecycle defect:

- `GWDrawFunc::OnLost` (`sub_4E66E0`) releases and empties the main vertex-
  buffer vector and releases the other renderer-owned device resources.
- It leaves the initialized byte at renderer offset `+0x484` set, so later
  rendering can treat the now-empty renderer as ready.
- It also leaves the index buffer stored through the holder at renderer offset
  `+0x778` alive. The constructor allocates this holder, the initializer stores
  the created index buffer through it, and the destructor releases it.
- The main dynamic vertex buffer created at `0x004E79F5` and the dynamic index
  buffer created at `0x004E7A82` both hardcode `D3DPOOL_DEFAULT` (`Pool=0`),
  even when `CheckDeviceLost=1`. They therefore must be released before a
  successful reset and recreated afterward.

The primary correction is to complete that missing OnLost lifecycle work.
Localized failure guards remain necessary because rendering and lazy
recreation can still encounter negative Direct3D HRESULTs during the Alt+Tab
transition.

## Alternatives Considered

A global lost-device flag could suppress every renderer entry between OnLost
and successful reset. That adds shared state and ordering-sensitive hooks, yet
still cannot make `Reset` succeed while the forgotten default-pool index buffer
remains alive. It is therefore broader without replacing the lifecycle repair.

Rewriting the two streaming buffers to `D3DPOOL_MANAGED` would avoid their
explicit lost/reset lifecycle, but it conflicts with their hardcoded dynamic
streaming usage and changes normal rendering behavior. `CheckDeviceLost=1`
already moves eligible resources to managed storage; these two buffers remain
explicit default-pool exceptions in the executable. Neither alternative is
selected.

## Chosen Design

Extend the focused, always-on `RendererDeviceLoss` runtime-patch module. It
will own six SafetyHook mid-hooks and publish them only as one successful
transaction. Normal nonnegative Direct3D results and nonempty renderer state
retain the game's native behavior.

| Recovery point | RVA | Failure behavior |
| --- | ---: | --- |
| OnLost lifecycle tail | `0x000E67D8` | Clear renderer `+0x484`; detach and release the index buffer held through `+0x778`; then execute the native tail. |
| Dynamic vertex-buffer result | `0x000E79F7` | On a negative HRESULT, clear `+0x484` and jump to the native initializer epilogue at `0x000E7EE9`. |
| Dynamic index-buffer result | `0x000E7A84` | On a negative HRESULT, perform the same deferred-initialization retry. This bypasses the native thrown-integer path. |
| Buffered vertex-buffer empty check | `0x000E5578` | For the exact empty state from the later dump, return the native zero output pair through the failure epilogue at `0x000E55E2`. |
| Direct-batch vertex-buffer Lock result | `0x000E691E` | On a negative HRESULT, skip the unchecked copy and continue at the native no-geometry cleanup path at `0x000E6AD6`. |
| Buffered vertex-buffer Unlock result | `0x000E5662` | On a negative HRESULT, bypass the native thrown-integer path and continue through the native batch-state completion block at `0x000E5679`. |

### OnLost Lifecycle Repair

The OnLost callback receives the renderer in `ESI`. Through a guarded
production adapter it will:

1. clear the initialized byte at `renderer + 0x484`;
2. read the persistent holder pointer at `renderer + 0x778`;
3. replace the holder's inner index-buffer pointer with null before invoking
   `Release` once when the old pointer is non-null; and
4. return to `0x004E67D8` so the original counter resets and epilogue execute
   normally.

Null inner pointers are valid and require no `Release`. Pointer reads, writes,
and the COM call are guarded against structured exceptions. The hook performs
no allocation and emits no per-transition logging. Detaching before `Release`
prevents the native destructor or a later OnLost pass from releasing the same
pointer twice.

### Creation Retry

One pure negative-HRESULT transform is shared by the vertex- and index-buffer
creation hooks. For a negative `EAX`, it clears the initialized byte using the
guarded adapter and redirects to the existing initializer epilogue. It changes
`EIP` only after the byte write succeeds. Zero and positive HRESULTs leave the
complete register context and memory unchanged.

The next render traversal sees the renderer as uninitialized and retries the
native lazy initializer. Any buffer that was successfully created before the
other creation failed remains under native ownership and is either reused by
that retry or released by the next OnLost pass.

### Transition-Time Draw Guards

The checked access in `sub_4E5540` is redirected only for the proven empty
state: computed vector length `ECX=0`, index `EDI=0`, and zero register
`EBX=0`. The hook obtains the hidden output-pair pointer from the guarded stack
read at `ESP+0x14`, places it in `EAX`, and enters the game's existing Lock-
failure epilogue, which writes two zero values and returns normally.

The direct-batch path at `sub_4E6800` does not inspect the Lock HRESULT before
using the output pointer in `memcpy`. Its hook checks the still-live result in
`EAX`; only a negative result redirects to the function's existing no-geometry
cleanup. The buffered Unlock path already tests its HRESULT but throws an
integer on failure; its hook redirects negative results to the same state-
completion block used after a successful Unlock. These redirects prevent a
transient lost-device result from becoming an invalid memory access or an
uncaught C++ exception.

## Binary Contracts and Transactional Installation

The supported image base remains `0x00400000`. Every hook and every redirect
target is preflighted before the first hook is created:

| Contract | Expected bytes |
| --- | --- |
| OnLost tail `0x000E67D8` | `89 BE 18 01 00 00 89 BE 1C 01 00 00` |
| Vertex-buffer result `0x000E79F7` | `85 C0 7C 59 8B 4F 0C` |
| Index-buffer result `0x000E7A84` | `85 C0 7D 13 68 E4 A5 71 00` |
| Initializer epilogue `0x000E7EE9` | `5F 5E 5B 8B E5 5D C3` |
| Empty-vector check `0x000E5578` | `3B F9 72 05 E8 66 00 02 00` |
| Lock-failure epilogue `0x000E55E2` | `5F 5E 89 18 89 58 04 5B 59 C2 08 00` |
| Direct Lock result `0x000E691E` | `8B 4C 24 14 51 8B 8E E4 01 00 00` |
| Direct no-geometry cleanup `0x000E6AD6` | `8B B6 E4 01 00 00 8B 5E 10 39 5E 0C` |
| Unlock result `0x000E5662` | `85 C0 7D 13 68 E4 A5 71 00` |
| Unlock continuation `0x000E5679` | `8B 86 80 04 00 00 8B 8E 44 07 00 00` |

An unexpected image base, unreadable memory, any byte mismatch, or any hook-
creation failure rejects installation. A hook-creation failure resets every
hook already created in the candidate runtime, and global ownership is
published only after all six hooks succeed. Installation logs one success or
one precise stage/site failure; the render callbacks do not log.

## Tests

Tests use pure context transforms and injected memory, release, and hook
operations rather than source-text checks or a duplicated executable fixture.
They will cover:

- OnLost with a non-null index buffer clears initialized state, nulls the
  holder before release, and releases exactly once;
- OnLost with an already-null inner pointer clears initialized state without a
  release, while rejected memory operations cannot escape the callback;
- negative vertex- and index-buffer creation results defer initialization,
  while zero and positive results preserve the complete native context;
- the exact empty-vector state returns the native zero pair, while any
  nonmatching register state or failed stack read remains untouched;
- a negative direct Lock result selects native no-geometry cleanup and a
  negative buffered Unlock result selects native state completion, while
  nonnegative results remain untouched;
- all ten binary contracts must preflight before installation begins; and
- failure at any of the six hook installations resets the whole candidate and
  never publishes a partial patch.

Each new behavior is introduced with a focused failing test before production
code is changed.

## Verification and Runtime Acceptance

Static verification consists of the focused patch tests, complete Debug and
Release builds and CTest suites, `git diff --check`, and a fresh IDA daemon
read of every contract in the table. These checks prove the supported binary
mapping, context transforms, and transactional installation, but they do not
prove runtime acceptance.

Runtime acceptance is user-run on the affected configuration:

1. set or confirm `CheckDeviceLost=1` in the Shift-JIS `system.cfg`;
2. launch the supported game in fullscreen and reach an actively rendering
   post-load scene;
3. Alt+Tab to another application and Alt+Tab back to the game;
4. confirm that the process remains alive and rendering resumes; and
5. repeat the out/back cycle several times.

No DLL is deployed into the runtime tree as part of implementation or static
verification.
