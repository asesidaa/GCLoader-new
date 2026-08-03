# Japanese Locale Compatibility Design

Date: 2026-08-04

Status: Approved design awaiting written-spec review

## Decision

GCLoader will replace this game's dependency on Locale Emulator with a small,
mandatory Japanese-locale compatibility layer built from documented Win32 API
hooks. The layer will run in both the game process and the injected NESYS
process. It will emulate only the code-page, locale, and Tokyo-time behavior
demonstrated to matter to these binaries.

The current font hooks and font diagnostics will be removed. GCLoader will not
rewrite GDI charset requests. Runtime acceptance must launch without Locale
Emulator, because Locale Emulator's private GDI hook is the component that
changes the bundled Infinity font request into an MS PGothic selection.

Filesystem behavior remains an open evidence question. This change therefore
adds bounded, pass-through filesystem diagnostics, not an ANSI-to-Unicode
filesystem compatibility policy. Any filesystem rewrite requires a separate
evidence-backed design after the diagnostic run.

This design supersedes the implementation direction in
`2026-08-03-japanese-font-charset-compatibility-design.md` and completes the
temporary investigation described by
`2026-08-04-font-selection-diagnostic-design.md`.

## Evidence

The result scene at
`H:\gc\artifacts\2d_boost\result_xfl\result.xfl` uses `$` for the positive-score
indicator. In the bundled `InfinityFont_midiam_dot` font, U+0024 contains the
intended upward-triangle glyph. A literal dollar sign therefore proves that GDI
selected a fallback font; it is not a text-substitution bug.

The completed runtime trace established that:

- all eight bundled Infinity font resources register successfully;
- the result path requests `InfinityFont_midiam_dot` with charset 1;
- after that public call, the returned logical font has charset 128 and the
  physical face is MS PGothic; and
- `win32u!NtGdiHfontCreate` is detoured in the Locale Emulator launch.

Locale Emulator Core explains the transition. Its GDI hook rewrites default
and ANSI font requests to the emulated charset below the public
`CreateFontIndirectW` boundary. Consequently, removing or passing through
GCLoader's public font hook cannot fix a launch that still injects Locale
Emulator Core.

The Locale Emulator C# application is only the front end. `LEProc` marshals a
profile into `LoaderDll!LeCreateProcess`; the native loader creates a helper,
hooks process creation, and manually maps Locale Emulator Core into the game
and descendants. Reusing that launch path would retain the unwanted private
GDI behavior and its other generic process modifications.

Current IDA analysis of `game471.exe`, `TtxUpdateDownloader.dll`, and
`NesysService.exe` supports a smaller boundary:

- `game471.exe` imports GCLoader as `iDmacDrv32`, so GCLoader process attach
  runs before the executable's CRT startup;
- the game CRT's `___initmbctable` at `0x0051864D` calls `_setmbcp(-3)`, whose
  system-code-page path reaches `GetACP` and `GetCPInfo`;
- the game uses `MultiByteToWideChar(CP_ACP)` for gameplay strings and font
  names, and also calls the public locale APIs selected below;
- `NesysService.exe` uses the same code-page APIs and CRT multibyte routines on
  HTTP, path, and downloaded-data strings;
- the game reads local wall time directly and its CRT reaches
  `GetTimeZoneInformation`;
- the game attempts one `SetLocalTime` after parsing server time but does not
  branch on the result; and
- the only relevant child launch is `NesysService.exe -app`, already handled by
  GCLoader's targeted suspended-process injector.

No analyzed binary directly reads the Windows NLS registry keys that Locale
Emulator redirects. No evidence requires Locale Emulator's PEB/TEB mutation,
private `Nt*` hooks, USER/GDI/clipboard hooks, or generic child propagation.

## Goals

- Make the game and NESYS process parse Japanese CP932 data consistently on a
  non-Japanese Windows installation.
- Present Japanese thread/user locale identity through the public APIs used by
  the game.
- Present a fixed UTC+09:00 Tokyo wall clock without changing the host clock.
- Inject the same compatibility layer before `NesysService.exe` resumes.
- Let the bundled Infinity fonts select naturally, without any GDI hook.
- Collect finite filesystem evidence while leaving filesystem results and
  paths unchanged.
- Keep the behavior mandatory, configuration-free, transactional, and visible
  in the process-specific loader logs.

## Non-Goals

- Building a general-purpose Locale Emulator replacement.
- Running this compatibility layer together with Locale Emulator Core.
- Hooking `ntdll`, `win32u`, the PEB, the TEB, NLS registry access, GDI, USER,
  clipboard, or arbitrary child creation.
- Changing the process ANSI code page globally for unrelated software.
- Translating arbitrary explicit code pages or locale identifiers.
- Converting ANSI filesystem calls to Unicode in this change.
- Changing game assets, font files, XFL content, or the `$` character.
- Adding a `config.toml` or ConfigGUI option.
- Modifying either checked-out Locale Emulator source tree.
- Deploying into `H:\gc` or claiming runtime acceptance without explicit
  operator testing.

## Architecture and Startup Order

Replace `src/Font/FontCharsetCompatibility.*` with a focused module under
`src/Locale`. The locale module owns the required API trampolines, policy
helpers, and one process-lifetime MinHook transaction. A separate filesystem
diagnostic unit owns only event classification, deduplication, bounded path
formatting, and observation state.

`Loader/DllMain.cpp` installs required locale compatibility immediately after
detecting the process role and initializing that role's log. This occurs before
`ConfigManager`, before the game executable's CRT startup, and before the
injected NESYS process resumes. The same locale policy is installed for both
`ProcessRole::Game` and `ProcessRole::Service`; game-only input, audio, RFID,
storage, and runtime patches remain excluded from the NESYS process.

The required locale hooks use the existing MinHook transaction abstraction.
All targets are resolved before any hook is enabled. A resolve, create, or
enable failure rolls the transaction back, identifies the failed stage and
export in the process log, publishes a startup-fatal message, and returns
`FALSE` from process attach. Starting under the host locale after a partial or
missing compatibility install is not a supported fallback.

The filesystem observer does not share ownership of an API target already
owned by the game `Kernel32Hooks` layer. Game-process observations are inserted
at the pass-through branches of that existing layer. The NESYS process, which
does not install the game Kernel32 layer, owns a separate temporary diagnostic
transaction for its file APIs. This prevents two MinHook transactions from
detouring the same export.

No SafetyHook or executable-image patch is required by the approved design.
Those are fallback tools only if implementation evidence disproves a public API
boundary.

## Code-Page and Locale Contract

The required public hooks are:

| API | Emulated behavior |
| --- | --- |
| `GetACP` | Return code page 932. |
| `GetOEMCP` | Return code page 932. |
| `GetThreadLocale` | Return Japanese LCID `0x0411`. |
| `GetUserDefaultLCID` | Return Japanese LCID `0x0411`. |
| `GetCPInfo` | Replace `CP_ACP` or `CP_THREAD_ACP` with 932, then call the original API with the original output pointer. |
| `MultiByteToWideChar` | Replace `CP_ACP` or `CP_THREAD_ACP` with 932 and forward every other argument unchanged. |
| `WideCharToMultiByte` | Replace `CP_ACP` or `CP_THREAD_ACP` with 932 and forward every other argument unchanged. |

Every explicit code page, including UTF-8 and an explicit 932, passes through
unchanged. The hooks do not transform buffers themselves; Windows remains the
conversion implementation and supplies the original return and last-error
behavior. The constant-return hooks do not disturb the caller's last-error
value. A literal `CP_OEMCP` conversion token also remains unchanged because no
analyzed conversion caller uses it; callers that ask `GetOEMCP` receive the
explicit value 932 and therefore use that code page on subsequent calls.

`GetLocaleInfoA/W` is deliberately not hooked. The analyzed callers either use
the now-emulated Japanese LCID or supply an explicit locale, so overriding the
host defaults more broadly has no demonstrated consumer. The same evidence
rule excludes `GetSystemDefaultLCID`, locale-name APIs, environment variables,
and NLS-registry virtualization.

Installing before executable CRT startup is essential. It ensures the static
CRT observes 932 while building its multibyte lead-byte tables, rather than
trying to repair `_mbctype` state after parsing has begun.

## Tokyo Time Contract

The required time hooks are `GetTimeZoneInformation`, `GetLocalTime`, and
`SetLocalTime`.

`GetTimeZoneInformation` returns one fixed Tokyo definition:

- bias `-540` minutes;
- standard and daylight names `Tokyo Standard Time`;
- standard and daylight bias `0`;
- no standard/daylight transition dates; and
- `TIME_ZONE_ID_UNKNOWN`, the documented result for a zone without seasonal
  transitions.

The output structure is zero-initialized before those fields are assigned, and
the hook preserves the incoming last-error value.

`GetLocalTime` obtains current UTC time and converts it to UTC+09:00 using
checked FILETIME arithmetic. It never derives Tokyo time by adding nine hours
to the host's already-local time. The conversion handles day, month, leap-year,
and year boundaries and restores the incoming last-error value.

`SetLocalTime` is a successful no-op for the game's observed server-time write.
The detour does not call the operating-system setter or perform any privilege
work, and it does not change the host clock; the game's preceding token-
privilege call is otherwise left unchanged. The detour preserves last error and
emits at most one process-lifetime diagnostic stating that the write was
suppressed. This is safe for the observed call because the game ignores the
return value and all subsequent time reads already receive the emulated Tokyo
view.

UTC APIs and epoch time remain untouched. This keeps elapsed-time and protocol
timestamps on their existing absolute timeline while changing only the local
calendar view that Locale Emulator previously supplied.

## Targeted NESYS Propagation

GCLoader's existing `CreateProcessA` policy recognizes only
`NesysService.exe -app`, creates that child suspended, injects the current
GCLoader DLL with `LoadLibraryW`, waits for child initialization, and resumes
the child when the caller did not request suspension. That path remains the
sole propagation mechanism.

The launcher can no longer be conditional on network-adapter or registry
virtualization settings. Japanese locale compatibility always requires the
targeted launcher in the game process, even when those unrelated NESYS options
are disabled. `ResolveNesysFeaturePlan` and its tests must represent that
independent requirement explicitly.

No generic `NtCreateUserProcess` hook is added. An injection or child
initialization failure retains the existing fail-closed launcher behavior;
running the NESYS process under the host locale would reintroduce the same data
parsing risk this design removes.

## Temporary Filesystem Diagnostics

Filesystem diagnostics answer one question: does a relevant ANSI filesystem
call fail or lose a CP932 path where the equivalent Unicode path is usable?
They never substitute a Unicode result.

The observed ANSI API set is:

- `CreateFileA`;
- `GetFileAttributesA`;
- `FindFirstFileA` and `FindNextFileA`;
- `CreateDirectoryA`;
- `DeleteFileA`;
- `MoveFileA`; and
- `CopyFileA`.

In the game process, existing `Kernel32Hooks` handlers retain ownership and all
RFID/JVS, test-mode storage, and system-path behavior. The observer sees the
caller's raw path and the result of only an otherwise-unowned ANSI pass-through
branch. Purely diagnostic exports not already owned by that layer are added to
the same game hook set rather than installed a second time. In the NESYS
process, equivalent pass-through detours live in the temporary diagnostic
transaction.

For every observed API, the detour first calls the original API with byte-for-
byte identical arguments and captures its result and `GetLastError()` value.
Only then may it classify or log the event. Before returning, it restores the
captured last error and returns the exact original result. Exceptions are
caught inside the hook boundary.

On a selected failure, the observer may decode the raw path with an explicit
code page 932 `MultiByteToWideChar` call and perform a read-only wide probe:

- attribute and open failures use `GetFileAttributesW` to classify the decoded
  target as present, absent, or inaccessible;
- a failed `FindFirstFileA` may use `FindFirstFileW` and immediately close a
  successful probe handle; and
- mutation APIs may inspect source and destination attributes but never retry
  the mutation.

`FindNextFileA` has no path input, so it observes only returned filenames and
errors and performs no probe. `ERROR_NO_MORE_FILES` is expected enumeration
completion and is never logged. A probe is diagnostic I/O only: it cannot
replace an ANSI handle, result, output structure, or error.

### Spam and Reentrancy Controls

Normal successful ASCII filesystem calls never produce log lines. An event is
eligible only when:

1. a raw input or returned filename contains a byte at or above `0x80`; or
2. an unowned regular-file call fails, excluding expected enumeration
   completion.

Named pipes, device paths, COM ports, `loader-log.txt`, and
`loader-service-log.txt` are excluded. Paths are inspected to a fixed maximum,
and output includes escaped raw bytes plus an explicit CP932 decoding. Each
rendering is capped at 192 input bytes and records `truncated=true` when needed;
arbitrary ANSI bytes are never sent directly to the logger.

Each process has two fixed-capacity deduplication tables: 32 unique non-ASCII
events and 32 unique failure events. The key covers process role, API, event
class, every bounded input or returned filename, result, error, and probe
outcome. A non-ASCII failure is classified as non-ASCII and consumes only that
table; otherwise an eligible failure consumes the failure table. Duplicates do
not consume capacity. When a category cannot accept another unique event, the
process emits one total `filesystem diagnostic category cap reached; additional
events in capped categories suppressed` line and never emits another cap
warning. The other category may use its remaining slots, so the maximum is 64
event lines plus one startup line and one cap line per process.

A thread-local reentrancy guard surrounds classification, probes, and logging.
Any nested filesystem call caused by path conversion or the log appender calls
the original behavior without observation. The uninteresting hot path performs
only bounded inspection and hash lookup; it does not allocate or log.

The startup line names the enabled API set and both capacities. There are no
per-call summaries, periodic reports, or unbounded counters in the log.

## Removal of the Font Experiment

Implementation removes the `CreateFontIndirectW` and `AddFontResourceExA`
detours, the `win32u!NtGdiHfontCreate` byte trace, their counters and tests, and
the `gc_font_compatibility` target. Nothing replaces them at the GDI boundary.

The historical specs remain as investigation records. Their implementation is
not retained because it would add noise and, in the charset-rewrite form,
reproduce the wrong MS PGothic selection.

## Error Handling and Logging

Required locale compatibility is transactional and fail-closed. Its success
path produces one startup line per process containing the role, ACP, LCID, and
Tokyo offset. Hook bodies do not log normal calls. The only time-hook call-site
diagnostic is the once-only suppressed `SetLocalTime` line.

Filesystem observation is temporary and bounded as specified above. A
filesystem diagnostic setup failure is logged once and must not undo an
already-committed locale transaction. Existing game hook-layer failures retain
their existing startup-fatal policy.

No exception may cross `DllMain`, a Win32 detour, or the remote child
initialization boundary. All forwarded parameters, output buffers, return
values, and last-error semantics remain unchanged except for the explicitly
documented locale/time policy.

## Alternatives Rejected

### Keep Locale Emulator and counter-hook its GDI rewrite

This would depend on private hook order and undocumented `win32u` behavior. It
would also retain the generic loader, PEB/TEB edits, NLS registry redirection,
and unrelated USER/GDI/clipboard hooks whose necessity has not been shown.

### Port a reduced Locale Emulator Core

Most of the core is injection and generic hook infrastructure already supplied
more narrowly by GCLoader, MinHook, and the existing NESYS launcher. Forking it
would preserve a large private-API maintenance surface without evidence that
the game needs it.

### Add complete ANSI-to-Unicode filesystem rewriting now

This might help CP932 filenames, but it can also alter wildcard enumeration,
sharing, creation, short-name, and last-error behavior. The current evidence
shows risk, not an actual failing API/path pair. Observation first keeps any
future fix exact.

### Reproduce Locale Emulator's `Nt*`, PEB, or registry hooks

The analyzed binaries reach the needed state through public APIs before their
CRT and parsing work. Starting at the documented boundary is simpler, stable
across Windows releases, and easier to test.

## Automated Verification

Focused locale tests use capturing original functions and independently prove:

- default ANSI/thread code-page tokens become 932 while explicit code pages
  remain unchanged;
- conversion flags, pointers, lengths, default-character parameters, output
  pointers, returns, and last error are forwarded correctly;
- ACP, OEMCP, thread locale, and user locale constants are exact;
- the Tokyo descriptor has no DST and the correct `-540` bias;
- UTC+09:00 conversion crosses day, month, leap-day, and year boundaries
  correctly; and
- `SetLocalTime` returns success without invoking an operating-system setter
  and notifies its observer at most once.

Focused filesystem tests prove event classification, device/log exclusions,
CP932 path rendering, truncation, deduplication, independent 32-event budgets,
the single cap marker, reentrancy suppression, read-only probe reporting, and
exact result/last-error restoration. Tests use injected observers and original
functions rather than reading source text or duplicating implementation tables.

NESYS plan and launcher tests prove that the targeted launcher remains active
when network and registry virtualization are both disabled, that unrelated
children pass through, and that caller-requested suspension semantics remain
unchanged.

Static completion requires focused tests, full x86 Debug and Release builds and
CTest suites, `git diff --check`, clean source-tree inspection, and inspection
of the produced `iDmacDrv32.dll`. These checks establish build and hook-policy
behavior only, not gameplay acceptance.

## Runtime Acceptance and Diagnostic Cleanup

After explicit deployment approval, launch the game directly as administrator
without Locale Emulator. Acceptance requires:

- the game log reports one committed Japanese locale transaction;
- the NESYS launcher intercepts and injects `NesysService.exe -app`;
- the NESYS log reports the same committed locale and Tokyo-time policy;
- the result screen shows the Infinity-font upward triangle instead of a
  literal `$`;
- game data and downloaded entries that were previously sensitive to locale
  remain present and parse correctly;
- NESYS startup, update/download behavior, and date-sensitive game behavior
  remain correct; and
- filesystem diagnostics remain within the documented bounds.

The operator's run is the only evidence for those runtime claims. If a logged
ANSI failure has a usable CP932-decoded Unicode counterpart, it becomes input
to a separate, API- and path-specific filesystem design. If no such mismatch
appears, no filesystem conversion is added. In either case, the temporary
filesystem diagnostics and their extra hooks are removed after the evidence is
captured; the locale and time compatibility layer remains.
