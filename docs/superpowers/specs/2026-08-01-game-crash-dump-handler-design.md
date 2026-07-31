# GCLoader Game Crash Dump Handler Design

Date: 2026-08-01

Status: Approved design contract

## Context

GCLoader currently logs normal runtime diagnostics, but an unhandled exception
can terminate `game471.exe` without preserving the process state needed to
diagnose the failure. The loader should capture a Windows dump beside the game
executable automatically whenever the game reaches top-level exception
handling.

The repository is `H:\gc\artifacts\GCLoader`. `H:\gc` remains the runtime and
deployment tree and is not modified by this implementation.

Current binary evidence from `H:\gc\game471.exe.i64` shows that the game already
has termination-oriented CRT exception handling:

- the initializer at `0x0051A8D3` installs
  `__CxxUnhandledExceptionFilter` through `SetUnhandledExceptionFilter`;
- that filter recognizes Microsoft C++ exceptions and calls `terminate()`, but
  does not write a dump; and
- `_invoke_watson` at `0x00505499`, `abort` at `0x0050E51E`, and
  `__report_gsfailure` at `0x00511537` clear the top-level filter before calling
  `UnhandledExceptionFilter`.

A one-time filter installed from GCLoader's `DllMain` would therefore be
replaced by the game's later CRT initializer and bypassed by several important
fatal-error paths.

The current `game471.exe` PE image is 32-bit and does not set
`IMAGE_FILE_LARGE_ADDRESS_AWARE`. On 64-bit Windows its user-mode virtual
address space is consequently limited to 2 GiB. A full-memory minidump writes
accessible process memory rather than the entire address range, so normal dump
files should be below that limit plus minidump metadata. This is an observation
about the current executable, not a permanent file-size guarantee.

## Goals

- Capture a dump for unhandled faults in the game process, including ordinary
  access violations, unhandled C++ exceptions, CRT abort/invalid-parameter
  failures, and security-cookie failures that reach Windows top-level handling.
- Preserve as much diagnostically useful process state as the native Windows
  minidump API can reliably provide, including full accessible memory.
- Write each dump beside the process executable, independently of the process
  current directory.
- Preserve the game's existing CRT exception policy as downstream behavior.
- Keep crash diagnostics fail-open: inability to prepare, hook, or write a
  dump must never prevent the game from starting.
- Exercise the real handler, replacement protection, path contract, and dump
  contents in an automated child-process crash test.

## Non-Goals

- Capturing dumps from the injected NESYS process. The handler is game-only.
- Deploying the built DLL or modifying the runtime tree.
- Adding a `config.toml` option or ConfigGUI control.
- Uploading, rotating, compressing, or deleting operator crash dumps.
- Replacing Windows Error Reporting or providing a guaranteed out-of-process
  dumper for a catastrophically corrupted process.
- Treating build/static validation as proof that the real game has crashed and
  produced a usable runtime dump.

## Chosen Approach

Add one game-only crash-dump component that installs a native Windows top-level
exception filter and uses the existing MinHook infrastructure to intercept
later `SetUnhandledExceptionFilter` calls.

The detour keeps GCLoader's filter registered with Windows. It stores the
caller's requested filter as the current downstream filter and returns the
previous downstream filter, preserving the observable setter contract. When an
exception reaches GCLoader's top-level filter, GCLoader writes its dump first
and then invokes the current downstream filter. If no downstream filter exists,
it returns `EXCEPTION_CONTINUE_SEARCH` so normal Windows termination/reporting
continues.

This preserves the game's `__CxxUnhandledExceptionFilter` rather than removing
it. It also prevents the CRT's fatal paths from disabling dump capture when they
call `SetUnhandledExceptionFilter(nullptr)` before calling
`UnhandledExceptionFilter`.

A plain one-time `SetUnhandledExceptionFilter` registration was rejected
because the current game binary demonstrably replaces it after DLL attach.
Windows Error Reporting `LocalDumps` was rejected as the implementation because
it requires persistent external registry configuration rather than a
self-contained GCLoader feature. An out-of-process watchdog was rejected as
unnecessary process-lifecycle complexity for this diagnostic patch.

## Initialization and Ownership

The component lives in a focused Win32 diagnostics module and exposes one
non-throwing installation entry point. `DllMain` invokes it only after detecting
the game process role and initializing the process log, but before configuration
and game-only feature initialization. This captures failures in the remaining
GCLoader startup path as well as later gameplay.

Installation performs these steps:

1. Resolve and cache the executable directory and stem with wide Win32 APIs.
2. Register GCLoader's top-level filter and retain the filter previously
   registered with Windows as the initial downstream filter.
3. Install and enable one MinHook detour for
   `Kernel32!SetUnhandledExceptionFilter` using the shared checked hook
   transaction.
4. Report full or degraded installation through the already-initialized loader
   log. A failed detour leaves the one-time filter active but explicitly records
   that later replacement protection is unavailable.

The handler remains process-lifetime state. No detach-time cleanup is needed
because Windows is terminating the process and unloading a live exception
filter during normal detach would create a race.

## Dump File Contract

Each crash attempts to create a new file with this UTC, collision-resistant
name beside the executable:

```text
game471-crash-YYYYMMDDTHHMMSS.mmmZ-p<PID>-t<TID>.dmp
```

The executable stem is derived from the actual process image, so a renamed game
image receives the corresponding `<stem>-crash-...dmp` name. File creation uses
wide Win32 APIs and `CREATE_NEW`; it never depends on or changes the current
directory. Existing dumps are not overwritten.

The primary `MiniDumpWriteDump` request combines:

- `MiniDumpWithFullMemory`;
- `MiniDumpWithHandleData`;
- `MiniDumpWithUnloadedModules`;
- `MiniDumpWithProcessThreadData`;
- `MiniDumpWithFullMemoryInfo`;
- `MiniDumpWithThreadInfo`;
- `MiniDumpWithFullAuxiliaryState`;
- `MiniDumpWithPrivateWriteCopyMemory`;
- `MiniDumpIgnoreInaccessibleMemory`;
- `MiniDumpWithTokenInformation`;
- `MiniDumpWithModuleHeaders`; and
- `MiniDumpWithAvxXStateContext`.

Filtering and data-removal flags are excluded. Data-, code-, indirect-, and
private-read/write-memory selections are not added separately because full
memory already subsumes their memory content. Intel Processor Trace is not
requested because the flag cannot create historical trace data when no IPT
session was active and can reduce compatibility without adding evidence.

If the comprehensive request fails because the local integrated `DbgHelp.dll`
does not support a requested metadata flag, the handler truncates the partial
file and retries with a compatibility full-memory set. If that also fails, it
makes one final `MiniDumpNormal` attempt so disk pressure or a damaged memory
region can still leave basic thread stacks. The dump header records which flag
set actually succeeded.

Full-memory dumps contain arbitrary process data and must be treated as
sensitive local diagnostic artifacts.

## Crash-Path Safety and Failure Behavior

The crash path uses a single interlocked re-entry gate because DbgHelp is not
safe for concurrent calls and downstream CRT termination can recursively enter
top-level handling. A recursive entry skips dump creation and continues to the
downstream/Windows policy.

Executable path storage is prepared during installation. Filename formatting,
file creation, dump writing, truncation, and handle closing use fixed storage
and native APIs; the handler performs no diagnostic logging, C++ stream work,
heap allocation, or lock acquisition. It supplies the original exception
record, context, and crashing thread ID through
`MINIDUMP_EXCEPTION_INFORMATION`.

Every failure is contained. If path preparation or file creation fails, the
handler still invokes downstream behavior. A failed dump is closed and any
partial file may remain as evidence; the game is never resumed merely because
dump creation failed.

This remains best-effort. An in-process handler can fail after severe stack,
heap, loader, or filesystem corruption. Guaranteed capture for those cases
would require Windows Error Reporting local dumps or a separately running
dumper process.

## Testing

Add one behavioral integration-test executable with parent and child modes.
The parent copies or launches the child from a directory containing Chinese and
Japanese characters, with a different current directory. The child:

1. installs the production crash handler;
2. calls `SetUnhandledExceptionFilter` with a test downstream filter returning
   `EXCEPTION_EXECUTE_HANDLER`; and
3. deliberately raises an unhandled access violation.

The later setter call is essential: if replacement protection regresses, the
test downstream filter consumes the crash and no GCLoader dump appears. The
parent waits for child termination, locates the dump by the child's PID beside
the copied executable, and verifies with DbgHelp that:

- the file is a parseable minidump;
- its exception stream records the deliberate access violation and child
  thread;
- its header contains the comprehensive requested dump flags;
- full-memory and memory-information streams are present; and
- no dump was written in the unrelated current directory.

The parent removes only its uniquely identified generated test dump and copied
test directory after successful or failed inspection. Existing files are never
globally cleaned.

Focused iteration runs this crash-dump integration test. Completion requires
fresh Debug and Release x86 builds, both full CTest suites, `git diff --check`,
and artifact inspection showing that `iDmacDrv32.dll` imports the native
DbgHelp dump writer. Real-game acceptance remains a separate step: deliberately
or naturally crash the deployed game, confirm a dump appears beside
`game471.exe`, and open it in a debugger with useful exception and stack state.
