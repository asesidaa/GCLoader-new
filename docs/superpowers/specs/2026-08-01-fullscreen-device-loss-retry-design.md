# Fullscreen Device-Loss Retry Design

## Goal

Prevent `game471.exe` from terminating when fullscreen Alt+Tab temporarily
leaves the Direct3D 9 device unable to create the renderer's dynamic vertex
buffer. The fix is always enabled for the supported executable and does not
add a `config.toml` setting.

## Binary Evidence

The player dump `game471.1DD2113D7FDFDC4.crash.dmp` records exception
`0xC0000417` (`INVALID_CRUNTIME_PARAMETER`) at `game471+0x1055F3`.
The raw stack contains return address `0x004E7B36`, which identifies the
invalid-parameter call at `0x004E7B31` inside `sub_4E72C0`, the lazy renderer
initializer reached from the normal render traversal.

IDA shows this sequence:

1. `0x004E79F5` calls `IDirect3DDevice9::CreateVertexBuffer` for a dynamic
   60,000-byte vertex buffer.
2. `0x004E79F7` tests the returned HRESULT. On failure, the game branches past
   insertion into its vertex-buffer vector but continues initialization.
3. `0x004E7B1A` computes the vector size from renderer offsets `+0x174` and
   `+0x178`.
4. `0x004E7B20` loads the buffer index from renderer offset `+0x47C`.
5. With the vector still empty, the checked `vector[index]` access calls
   `_invalid_parameter_noinfo` at `0x004E7B31`, deliberately raising the dump's
   `0xC0000417` exception.

The index is initialized to zero and the identified in-class reset paths also
write zero, so bypassing or clamping the index cannot repair an empty vector.
Fullscreen Alt+Tab explains why the Direct3D creation call temporarily fails:
the device-loss window reaches renderer initialization before creation is
available again.

## Patch Design

Add a focused `RendererDeviceLoss` runtime-patch module and initialize it only
in the game process.

The module installs one SafetyHook mid-hook at RVA `0x000E79F7`, immediately
after `CreateVertexBuffer` returns. The callback treats `EAX` as a signed
HRESULT:

- For a nonnegative result, it changes nothing and lets every original
  instruction execute.
- For a negative result, it clears the renderer initialized byte at
  `ESI + 0x484` and redirects `EIP` to the function's existing epilogue at RVA
  `0x000E7EE9`.

Clearing the flag makes the existing lazy initializer retry on a later render
pass. Redirecting at the actual failed creation prevents the partial state from
reaching either the empty-vector check or subsequent rendering. The callback
does not allocate, call Direct3D, or log on the render path. Its renderer-byte
write is guarded so a bad runtime pointer cannot escape the callback as an
exception; redirection occurs only after that write succeeds.

## Binary Contracts and Installation

The supported image base remains `0x00400000`. Before installing the hook, the
module checks both binary contracts:

- HRESULT site at RVA `0x000E79F7`:
  `85 C0 7C 59 8B 4F 0C`
- clean epilogue at RVA `0x000E7EE9`:
  `5F 5E 5B 8B E5 5D C3`

Unreadable memory, a byte mismatch, an unexpected image base, or hook creation
failure rejects installation without publishing an active hook. As with the
other mandatory game runtime patches, initialization fails closed and reports
only the install-stage error. One successful installation log records the hook
and retry-target RVAs.

## Tests

Focused tests will verify observable patch behavior:

- a failed HRESULT clears the initialized byte and redirects to the epilogue;
- zero and positive HRESULTs leave the byte and complete register context
  unchanged;
- a guarded write failure leaves the native execution path unchanged;
- exact binary contracts pass preflight, while either mismatch or read failure
  prevents hook installation;
- hook creation failure does not publish an active patch.

Tests use injected memory and hook operations rather than source-text checks or
a duplicated executable fixture.

## Verification and Acceptance

Static verification consists of focused tests, full Debug and Release builds
and CTest suites, `git diff --check`, and re-reading the current executable
bytes at both RVAs. These checks prove the hook contract and retry transform.

Runtime acceptance remains separate: launch the supported game in fullscreen,
reach normal gameplay or another post-load scene, Alt+Tab out and back in, and
confirm that the process stays alive and rendering resumes. Loader logs may
confirm installation, but only that in-game exercise confirms the reported
crash is fixed.
