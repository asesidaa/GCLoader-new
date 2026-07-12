# Process Logging and Registry Failure Hardening Design

**Date:** 2026-07-12

**Status:** Approved conversational design; written review pending

## Context

The registry-configuration virtualization work is complete and passes its x86 build, focused tests, full CTest suite, and static registry-boundary checks. Whole-branch review found one merge-blocking resource issue and two smaller failure-path issues:

- the cross-process append-only `loader-log.txt` can grow across sessions without a bound;
- `DisableThreadLibraryCalls` is incompatible with this static-CRT build and its non-trivial thread-local state;
- a successful registry open can leak its returned HKEY if process-local tracking allocation throws.

The review also proposed a stronger parent/child injection bootstrap. That change is not part of this design. The runtime contract is that one configuration remains immutable for the complete game/service run, including DLL injection and any service launch during that run. Under that contract, the existing fail-closed `LoadLibraryW` initialization handshake remains the selected behavior.

## Goals

- Preserve only the current process session's log.
- Keep the established game log filename while separating service diagnostics.
- Prevent a single session from consuming more than 100 MiB per process log.
- Keep logging failure non-fatal to game or service startup.
- Retain required static-CRT thread notifications.
- Close and clear a successfully opened registry handle when overlay tracking cannot allocate.
- Cover the new behavior with deterministic automated tests and repeat the full verification suite.

## Non-goals

- Retaining logs from prior sessions or creating numbered backup files.
- Coordinating rotation between processes.
- Changing log severity, existing diagnostic content, or input instrumentation.
- Adding a new parent/child configuration transport, digest, or readiness protocol.
- Supporting runtime configuration edits after the game process has initialized.
- Changing registry ownership, hook inventory, query formatting, or normal handle lifecycle.

## Session Log Architecture

Add a focused `SessionLog` unit with two layers:

1. A bounded session-file writer owns the Win32 file handle, current byte count, hard limit, one-time cap state, and in-process mutex.
2. A plog appender formats each record with the existing `TxtFormatter` and UTF-8/native-EOL converter, then submits those bytes to the writer.

The file is opened with `CREATE_ALWAYS`, so initialization discards the previous process session before the first record. The game process writes `loader-log.txt`; the NESYS service process writes `loader-service-log.txt`. Because the files are distinct, no inter-process write or rotation lock is required. Read/delete sharing remains enabled so an operator can inspect the active file.

The production byte ceiling is exactly 100 MiB (`100 * 1024 * 1024`). If the next formatted record would cross the ceiling, the writer emits one fixed limit marker when the marker fits, marks the log capped, and drops that record and every later record. No write may grow the file beyond the ceiling. The unit accepts a smaller limit in tests so boundary behavior is deterministic and fast.

File-open or write failure disables further writes for that process. Logging failure does not fail DLL attach. The failure path may emit one `OutputDebugStringW` diagnostic, but it must not call plog recursively.

## DLL Startup Ordering

`DllMain` detects `ProcessRole` before selecting the log filename, initializes the role-specific session appender, and then logs the detected role and initializes the selected NESYS policy. No existing game/service policy order changes.

Remove `DisableThreadLibraryCalls`. The target links the static CRT and contains non-trivial `thread_local` state, so thread attach/detach notifications must remain available. The existing thread cases continue to perform no custom work and return normally.

## Registry Allocation-Failure Cleanup

The registry open detour continues to call the original `RegOpenKeyExA` first. If the physical Type X open succeeds but insertion into the tracked-handle set throws, the detour:

1. calls the original `RegCloseKey` trampoline for the returned handle when available;
2. clears the caller's `HKEY` result;
3. returns `ERROR_NOT_ENOUGH_MEMORY`.

The caller must never receive an error together with an apparently usable leaked handle. Normal successful opens, physical open failures, close/reuse serialization, and pass-through behavior remain unchanged.

The regression test reaches the real detour through `AppendRegistryOverrideHookRequests`, installs fake original open/close trampolines, and uses a fail-next global allocation hook so the tracked-set insertion throws at a controlled point. It asserts OOM, one close of the exact opened handle, and a null result.

## Verification

Add focused session-log tests that prove:

- pre-existing file contents are truncated at initialization;
- bytes append in order within the configured limit;
- the limit marker is emitted at most once;
- the strict limit is never exceeded;
- later records are dropped after capping;
- game and service filenames are distinct and retain the approved names;
- an invalid/unopenable path disables writing without throwing.

Extend registry overlay tests with the deterministic allocation-failure cleanup case. Static inspection must show no `DisableThreadLibraryCalls` call.

After focused GREEN, run the complete x86 build, the focused hardening tests, all configured CTests (expected total: 11), the existing allowed/forbidden registry scans, `git diff --check`, and repository-state inspection. Runtime acceptance remains pending the user's existing game/service checklist.

## Acceptance Criteria

1. Every game start truncates and writes only `loader-log.txt` for that game process.
2. Every injected service start truncates and writes only `loader-service-log.txt` for that service process.
3. Neither file exceeds 100 MiB, and no backup generations are created.
4. Logging failure cannot fail DLL attach or recurse through plog.
5. Static-CRT thread notifications are not disabled.
6. Registry tracking allocation failure closes and nulls the fresh HKEY and returns OOM.
7. Focused tests, the full x86 build, all 11 CTests, and registry static boundaries pass.
8. No new parent/child bootstrap protocol is introduced; configuration immutability for the full run is the explicit operating contract.
