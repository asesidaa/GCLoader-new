# Automatic Test-Mode Storage Redirect Design

## Goal

Automatically persist `experimental.enable_testmode_storage_redirect = true`
when the game cannot persist its native test-mode files on `D:`, even when
`registry.system_path` is already configured to a different drive or to a
relative path.

## Source of truth

Native test-mode storage availability is independent of system-path routing.
Game-process startup therefore performs a dedicated probe against `D:\` on
every launch. It does not infer availability from `RuntimeRoot::redirect_enabled`
or from whether `D:\system` happened to be prepared.

The probe creates a unique disposable file on `D:`, writes one byte, flushes
the file, closes it, and removes it. Native storage is available only when the
create, write, and flush operations succeed. Cleanup is always attempted. A
cleanup failure is logged but does not change the capability result after the
write and flush have succeeded.

## Startup and persistence flow

The probe runs only in the game-process initialization path and before config
preparation is published. Its Boolean result is passed into the existing
configuration-preparation transaction.

When native test-mode storage is unavailable:

- set `experimental.enable_testmode_storage_redirect` to `true` if it is not
  already true;
- include that mutation in the same final `InputConfig` that contains any
  registry schema migration or `D:\system` to `.\system` fallback;
- persist the combined config with the existing atomic writer;
- fail startup through the existing config-persistence error path if that
  atomic write fails.

When native storage is available, leave the configured redirect value
unchanged. Availability never automatically changes `true` back to `false`.
If the redirect was already enabled and no other configuration changed, the
probe does not cause a redundant config rewrite.

## Component boundaries

- `TestModeStorage` owns the real native-storage probe and its cleanup.
- `Loader/DllMain.cpp` runs the probe in the existing game-only startup branch
  and passes only the availability result onward.
- `ConfigManager` and `ConfigDocument` fold the result into the existing
  system-path preparation and atomic persistence transaction.
- Existing Kernel32 test-mode routing remains unchanged; after the persisted
  flag is published, `Rfid::Feature` enables the current redirect hooks.

## Error handling and logging

Probe failure is a recoverable capability result, not a startup failure. The
loader logs the failed operation and Win32 error, then persists the redirect.
Probe cleanup failure is logged as a warning. Failure to persist the resulting
configuration remains fail-closed under the existing transaction rules.

## Verification

Behavioral tests cover:

- a writable probe root succeeds and leaves no probe file;
- an unavailable probe root reports failure;
- unavailable native storage forces and serializes the redirect while a
  custom non-`D:` system path is configured;
- available native storage preserves an explicitly disabled redirect without
  rewriting the config;
- an already-enabled redirect is not rewritten solely because the probe
  fails;
- simultaneous system-path fallback and unavailable native storage produce
  one atomic config write containing both changes.

Focused verification builds and runs the test-mode-storage and system-path
configuration tests, then builds the 32-bit Debug and Release loader targets.

## Non-goals

- Changing the test-mode path-matching or Kernel32 routing rules.
- Redirecting system paths based on the test-mode probe.
- Automatically disabling a user-enabled redirect.
- Persisting any probe artifact or touching the game's hashed test-mode
  directory.
