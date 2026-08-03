# Font Selection Diagnostic Design

Date: 2026-08-04

Status: Approved temporary diagnostic contract

## Evidence Correction

The prior charset-compatibility design assumed that selecting `MS PGothic`
was the desired result. The result XFL and bundled
`InfinityFont_midiam_dot.ttf` disprove that assumption: the score scene asks
for `InfinityFont_midiam_dot`, whose U+0024 glyph is the intended upward
triangle. The operator also observes a literal `$` without GCLoader's charset
rewrite, so that rewrite is neither the original cause nor a successful fix.

## Goal

Capture one elevated Locale Emulator run that distinguishes these remaining
causes without attempting another fix:

1. the game's private bundled-font registration fails before the
   `InfinityFont_*` files become available; or
2. a deeper GDI hook rewrites the requested charset after the public
   `CreateFontIndirectW` call.

## Diagnostic Behavior

Keep the existing game-only MinHook transaction, but make the
`CreateFontIndirectW` detour observational: forward the original pointer and
all fields unchanged, then log the returned logical charset and physical face.

Add a second detour for `GDI32!AddFontResourceExA`. It forwards the original
path, flags, and reserved pointer unchanged, captures the return value and last
error immediately, logs the input path, resolved path, file visibility,
result, and error, then restores the captured last error before returning.
Logging is bounded because the game registers eight bundled fonts during
startup.

On the first font-creation call, log the loaded-module handles and the first 16
bytes of `win32u!NtGdiHfontCreate`. This detects a deeper inline hook even when
Locale Emulator is manually mapped and absent from `GetModuleHandleW`.

## Interpretation

- A failed or missing `InfinityFont_*` registration identifies the resource
  loading boundary.
- Successful registration plus an input charset of `0` or `1` becoming a
  returned logical charset of `128` identifies a deeper GDI rewrite.
- Successful registration with an unchanged logical charset but a physical
  fallback identifies GDI face eligibility or resource visibility instead.

## Constraints and Verification

- Do not mutate the runtime tree until deployment is explicitly approved.
- Preserve Win32 arguments, return values, and last-error behavior.
- Keep traces bounded and removable in a follow-up commit.
- Verify the forwarding seams with focused behavioral tests.
- Build and run the complete Debug and Release test suites; runtime acceptance
  remains the operator's elevated result-page run.
