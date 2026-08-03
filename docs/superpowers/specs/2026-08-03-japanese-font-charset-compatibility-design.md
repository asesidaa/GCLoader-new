# Japanese Font Charset Compatibility Design

Date: 2026-08-03

Status: Approved design contract

## Context

`game471.exe` loads its bundled `InfinityFont_*` files and creates fonts through
`GDI32!CreateFontIndirectW`. The analyzed font paths pass `ANSI_CHARSET` or
`DEFAULT_CHARSET`, including the number-rendering paths that request
`InfinityFont_midiam_dot`.

Locale Emulator's normal Japanese profile changes those requests to
`SHIFTJIS_CHARSET` in a private `win32u!NtGdiHfontCreate` hook. On the current
machine, that conversion makes GDI select `MS PGothic`; without it, GDI selects
the bundled Infinity face and the numbers have the reported English-looking
appearance. Elevated launches can lose Locale Emulator's private GDI hook even
when its other Japanese-locale behavior remains useful.

GCLoader is already loaded into the game process before the game creates its
fonts. It can supply only the missing public-API behavior while Locale Emulator
continues to own code-page, locale, time-zone, and registry emulation.

The source repository is `H:\gc\artifacts\GCLoader`. `H:\gc` remains the
runtime and deployment tree and is not changed by implementation.

## Goals

- Preserve Japanese font selection when Locale Emulator's elevated GDI hook is
  missing.
- Match Locale Emulator's successful charset conversion closely.
- Keep the fix game-process-only, configuration-free, and independent of
  private Windows syscall implementations.
- Coexist harmlessly with a fully working Locale Emulator launch.
- Make hook installation failure explicit in the loader log without preventing
  an otherwise usable game launch.

## Non-Goals

- Replacing Locale Emulator's code-page, locale, time-zone, or registry work.
- Forking or rebuilding Locale Emulator.
- Renaming font faces, replacing font files, or changing game assets.
- Patching individual `game471.exe` call sites or adding version-specific RVAs.
- Adding a `config.toml` or ConfigGUI setting for this compatibility behavior.
- Deploying a built DLL or claiming in-game acceptance without operator testing.

## Chosen Approach

Add one game-only MinHook detour for `GDI32!CreateFontIndirectW`. The detour:

1. Forwards a null `LOGFONTW` pointer unchanged.
2. Copies every non-null caller structure into local storage.
3. Changes only `lfCharSet == ANSI_CHARSET` or
   `lfCharSet == DEFAULT_CHARSET` to `SHIFTJIS_CHARSET`.
4. Calls the original function with the copy.
5. Leaves every explicit charset and every other `LOGFONTW` field unchanged.

The caller's memory is never modified. The detour performs no allocation,
logging, locking, or other Win32 calls on the successful per-font path.

No face-name filter is used. Locale Emulator applies the same charset rule to
all fonts in the emulated process, and applying it game-wide is both simpler and
closer to the known-good behavior. When Locale Emulator's deeper hook is also
active, it receives an already-explicit charset 128 and leaves it unchanged, so
the two hooks are idempotent rather than competing.

Hooking the public GDI32 export is preferred over patching
`win32u!NtGdiHfontCreate`. It covers the game's observed import while avoiding
the undocumented syscall stub whose implementation has changed across Windows
releases.

## Source Architecture

Add a focused font-compatibility unit under `src/Font` and a matching focused
test under `tests/Font`. The unit owns:

- the original `CreateFontIndirectW` trampoline;
- the detour and charset transformation;
- one process-lifetime `gc::win32_hooks::MinHookTransaction`; and
- a game-startup installation function.

`Loader/DllMain.cpp` invokes installation only inside the existing game-process
initialization boundary and before returning from process attach. The game does
not create its runtime fonts until afterward.

Installation uses the existing transactional hook infrastructure. A failed
resolve, create, or enable operation is logged once with its stage and status,
then initialization continues. This deliberately differs from required
hardware/runtime hooks: incorrect font styling is visible but does not justify
blocking game startup. Successful installation logs one startup confirmation;
the detour itself never logs per call.

## Testing

Focused behavioral tests call the production transformation/detour seam with a
capturing original function and establish that:

- `ANSI_CHARSET` becomes `SHIFTJIS_CHARSET`;
- `DEFAULT_CHARSET` becomes `SHIFTJIS_CHARSET`;
- an explicit non-default charset is preserved;
- all non-charset fields are preserved;
- the caller's input structure is not mutated; and
- a null pointer is forwarded unchanged.

The existing MinHook transaction tests remain authoritative for resolve,
create, enable, and rollback mechanics; the font tests do not duplicate that
infrastructure.

Static verification consists of the focused test, the full affected Debug and
Release preset suites, `git diff --check`, and loader artifact inspection.
Runtime acceptance remains separate: launch `game471.exe` with Locale
Emulator's Japanese administrator profile, confirm the startup hook log, and
visually compare the affected numbers with the normal Japanese-profile result.
