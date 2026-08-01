# Ttx System Path Routing Design

**Date:** 2026-08-01

**Status:** Approved design

**Related design:** `2026-07-12-registry-config-virtualization-design.md`

## Problem

A user dump, `game471.1DD2144D73361C0.crash.dmp`, records an access violation in `TtxUpdateDownloader.dll` shortly after game startup. The downloader assumes that `D:\system` and its required children can be created. When that assumption fails, `TtxUDLInit` returns failure before initializing its global critical section. `game471.exe` ignores the return value and later calls `TtxUDLGetStatus`, which enters the uninitialized critical section and crashes in `ntdll`.

GCLoader's registry override can supply a nonzero `Country` value to the game, enabling this updater path, while the existing NESYS registry path settings affect only `NesysService.exe`. The existing optional test-mode storage redirect does not match `D:\system`. The result is a configuration split: the service can be configured to use alternate storage paths, but the downloader in the game process continues to use its hardcoded root.

The fix is to make one configured system root authoritative for both the derived NESYS registry paths and the downloader's Win32 filesystem calls. Startup must validate that root before enabling the updater path, and any remaining downloader initialization failure must become an explicit fatal diagnostic rather than a delayed null-pointer crash.

## Crash and Binary Evidence

This design uses the user-provided dump and log. It does not assume that the user's GCLoader DLL was locally produced or that it was the latest loader version.

### Dump identity

The dump contains these relevant module identities:

- `game471.exe`: timestamp `0x5FA90825`, image size `0x433000`, checksum `0x38A7B0`.
- `TtxUpdateDownloader.dll`: timestamp `0x576CDF0F`, image size `0x26000`, checksum `0x20F1D`.

Those PE identities match the local analysis binaries. Their SHA-256 hashes are:

- `game471.exe`: `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`
- `TtxUpdateDownloader.dll`: `C658E77C94E35FC0D748B0791B7E1734E774E7E757D7DAEDBDC7970B5B68D8FD`

The GCLoader image in the dump does not match the current local build. Its behavior is attributed only from the dump and supplied loader log, not from a latest-version assumption.

### Exception path

WinDbg reports an invalid pointer write at:

```text
ntdll!RtlpWaitOnCriticalSection+0xfe
inc dword ptr [ecx+14h]
ecx=00000000
```

The relevant stack is:

```text
ntdll!RtlpWaitOnCriticalSection
ntdll!RtlpEnterCriticalSectionContended
ntdll!RtlEnterCriticalSection
TtxUpdateDownloader+0x15f0
game471+0x19e51f
```

Daemon-backed IDA analysis establishes the contract failure:

- `TtxUDLInit` at `0x100010F0` is exported as `?TtxUDLInit@@YAHKKKK@Z` with type `int __cdecl(unsigned int, unsigned int, unsigned int, unsigned int)`. It creates the required `D:\system` directory tree before calling `InitializeCriticalSection` on the global at `0x10020984`.
- A `CreateDirectoryW` failure other than `ERROR_ALREADY_EXISTS` returns zero before critical-section initialization.
- `TtxUDLGetStatus` at `0x100015E0` unconditionally calls `EnterCriticalSection` on that global.
- `game471.exe` calls `TtxUDLInit` at `0x59E38B` and discards its return value.
- It later calls `TtxUDLGetStatus` at `0x59E519`; the return address `0x59E51F` matches the dump.
- A nonzero Type X `Country` value gates both calls.

The null critical-section debug pointer produces the exact `RtlpWaitOnCriticalSection` write to address `0x14` seen in the dump.

### Loader-log causality

The supplied loader log shows:

- process role `game`;
- registry virtualization enabled and installed;
- the first overridden game value was `Country`;
- test-mode storage redirection disabled;
- the existing Kernel32 hook transaction therefore contained the RFID hooks but not the storage path hooks.

GCLoader did not corrupt the critical section. It supplied the nonzero country that made the game enter an unsafe vendor path without also providing the required `D:\system` compatibility. The vendor bug is the ignored failed initialization; GCLoader is the causal trigger that must make the configured environment coherent.

## Relationship to the Earlier Registry Design

The July registry design intentionally treated `NewsPath`, `EventPath`, and `LogPath` as service-only values and excluded updater path changes. The crash provides new binary and runtime evidence that the game-side downloader must share the same storage root. This design supersedes only that path ownership and non-goal. The earlier role boundaries, strict configuration, real-registry passthrough, and owned hook transaction remain in force.

## Goals

- Define one configured system root shared by downloader routing and derived NESYS registry values.
- Support absolute and relative roots, including `system` and `.\system`.
- Use Unicode-native `std::filesystem::path` handling for resolution, validation, and routed filesystem calls.
- Keep the shipped `D:\system` default when its required tree exists or can be created.
- Automatically fall back only from the unchanged shipped default to `.\system` when registry virtualization is enabled.
- Persist migration and automatic fallback before updater or service startup.
- Reuse the existing process-wide Kernel32 MinHook layer and pass through every nonmatching call.
- Make any remaining `TtxUDLInit` failure an explicit fatal startup error through a SafetyHook inline hook.
- Preserve process-role boundaries and avoid real registry writes.
- Make configuration, persistence, path-routing setup, and hook-installation failures actionable rather than latent.

## Non-goals

- Fixing or replacing the vendor downloader implementation on disk.
- Patching Ttx instruction bytes, string literals, or import tables manually.
- Redirecting arbitrary `D:` paths or the test-mode `<32 hex>_<3 digits>` storage namespace through this root.
- Changing the real Type X registry.
- Making registry-disabled mode silently virtualized.
- Claiming compatibility with unobserved downloader APIs or binary behavior.
- Treating ordinary Win32 file-operation failures as loader infrastructure failures. Matching calls retain their native return and last-error semantics; failed downloader initialization is handled at its boundary.

## Configuration Contract

The strict registry configuration becomes:

```toml
[registry]
enabled = false
system_path = 'D:\system'

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
```

`system_path` replaces the three independently configurable leaf paths:

- `news_path`
- `event_path`
- `log_path`

The new field remains required even when registry virtualization is disabled, preserving the repository's strict configuration model.

### Derived NESYS values

The service registry override derives:

| Registry value | Configured path |
|---|---|
| `NewsPath` | `<system_path>\DUA\news` |
| `EventPath` | `<system_path>\DUA\event` |
| `LogPath` | `<system_path>\CmdFile\log` |

Derivation uses path components, not string concatenation. A relative spelling remains relative for the registry-facing value. For example, `.\system` produces `.\system\DUA\news`. This avoids embedding a potentially non-ASCII absolute startup directory in the service's ANSI registry contract.

The target service reads `RegQueryValueExA`. Absolute custom paths must therefore convert losslessly to the active Windows ANSI code page for those three returned `REG_SZ` values. If they cannot, startup fails with a message explaining the target binary limitation and recommending a relative path such as `.\system`. Filesystem validation and game-side routing remain Unicode-native.

### Relative roots

`system`, `.\system`, and other relative paths are valid. The loader captures the directory containing `config.toml` at startup and resolves the configured root against it exactly once. The resulting absolute `std::filesystem::path` is used for validation and game-process routing, so a later working-directory change cannot alter the destination.

The automatic fallback is written as `.\system` to make its current-directory ownership explicit.

### Legacy configuration migration

Migration occurs before strict deserialization into the new schema:

1. Parse the TOML syntax into a mutable configuration representation.
2. If `system_path` is absent and all three legacy path fields are present, strip the required leaf suffixes and compare their candidate roots using Windows path semantics.
3. If all candidates describe one root, set `system_path` to that root and remove the legacy fields.
4. If the legacy fields are incomplete or do not share one structural root, stop with an actionable configuration error.
5. If both `system_path` and any legacy leaf field are present, stop instead of silently choosing one source.
6. Strictly deserialize and validate the migrated representation.

A successful legacy migration preserves the effective root; it is separate from automatic availability fallback. When registry virtualization is enabled, the game process persists the migrated schema atomically before startup continues. When registry virtualization is disabled, migration is used only as an in-memory compatibility parse and the file is not rewritten. A later ConfigGUI save or registry-enabled launch writes the canonical schema.

ConfigGUI exposes the single system-root field and no longer exposes the three derived leaf fields.

## Root Resolution and Availability

Path resolution returns both:

- the configured spelling used for serialization and derived registry strings;
- the absolute native path used for directory operations and game-process hooks.

The configured value must be nonempty and syntactically usable as a Windows filesystem path. Resolution uses `std::filesystem::path` and `std::error_code`-based operations so filesystem failures are reported rather than crossing hook or startup boundaries as exceptions.

### Required directory tree

Availability means that the following tree exists or can be created with `std::filesystem::create_directories`:

```text
<system_path>
├── CmdFile
│   └── log
└── DUA
    ├── data
    ├── decrypt
    ├── download
    ├── event
    ├── news
    ├── unpack
    └── work
```

Creating the complete tree validates the same initialization surface used by `TtxUDLInit`, plus the service-owned news and event leaves. Existing directories are accepted.

### Registry virtualization enabled

When `[registry].enabled = true`:

1. Attempt to create the required tree at the configured root.
2. If it succeeds, keep that root.
3. If it fails and the configured value is semantically the unchanged shipped default `D:\system`, try `<config directory>\system`.
4. If the fallback tree succeeds, change the configured spelling to `.\system` and atomically persist it.
5. If fallback creation or persistence fails, stop startup.
6. If any custom root fails, stop startup without rewriting it.

The fallback is therefore recovery for an unavailable platform default, not a way to hide errors in an explicit user choice.

### Registry virtualization disabled

When `[registry].enabled = false`, the vendor updater cannot consume `system_path` because no path virtualization is active. Startup still validates and creates the real `D:\system` required tree. If that fails:

- do not rewrite configuration;
- do not install system-root routing;
- show an error explaining that the game requires an available `D:\system`, or that registry virtualization must be enabled to use an alternate root;
- terminate startup.

This preserves disabled-mode semantics while preventing the known delayed crash.

Disabled mode never persists legacy-schema migration, normalizes path spelling, or performs availability fallback. Its only filesystem mutation is the vendor-compatible attempt to create the real required `D:\system` tree.

## Atomic Persistence

When registry virtualization is enabled, runtime schema migration or default fallback is a startup transaction:

1. Serialize the canonical updated configuration to a unique sibling temporary file.
2. Flush and close the stream and verify every write succeeded.
3. Replace `config.toml` with the temporary file using a same-directory Windows atomic replacement operation.
4. On any failure, retain the original configuration, remove only the temporary file created by this attempt, report the exact path and system error, and terminate startup.

The loader must not continue in enabled mode with an in-memory root that the service child would not read from disk. Rewriting occurs only for schema migration or the approved default fallback. Registry-disabled mode performs no automatic configuration write.

## Runtime Architecture

### Process roles

The game process owns:

- configuration migration and fallback persistence when registry virtualization is enabled;
- root availability validation;
- Kernel32 system-path routing;
- the `TtxUDLInit` failure guard;
- the existing `Country` override and NESYS service launcher.

The service process does not load or patch `TtxUpdateDownloader.dll`. When registry virtualization is enabled, it reads the configuration already persisted by the game and returns the three derived path values through the existing service-role registry override. Disabled mode may parse a legacy schema in memory but does not expose those derived overrides.

The game completes path resolution and any persistence before allowing the service launcher or updater-dependent game startup to proceed.

### Startup sequence

Game-role initialization is ordered as follows:

1. Detect the process role and locate `config.toml`.
2. Parse, migrate, strictly validate, and resolve `system_path`.
3. Validate/create the required tree and atomically persist any approved change.
4. Construct the system path router from the captured logical source and resolved native destination.
5. Build and commit the combined Kernel32 MinHook request set.
6. Resolve and enable the SafetyHook inline guard for `TtxUDLInit`.
7. Only after both hook layers succeed, allow normal game initialization and NESYS child launch to continue.

If the SafetyHook step fails after the Kernel32 transaction commits, roll back the owned MinHook transaction, deactivate its dispatch object, and publish the startup fatal. No partially active routing state may survive a failed setup.

## Kernel32 System Path Routing

### Reuse the existing hook layer

The RFID feature already owns `Kernel32Hooks` plus `MinHookTransaction` and globally detours Kernel32 exports while passing through inputs it does not own. System-root routing joins that layer instead of adding manual Ttx import-table edits or another set of hooks for the same exports.

`Kernel32Hooks` composes three independent policies:

1. RFID virtual `COM2` handle behavior.
2. Optional test-mode storage routing.
3. Optional `D:\system` root routing.

The request builder creates the union of exports required by active policies, with each export appearing at most once. The fixed request capacity increases from 24 to a documented value sufficient for the union. Existing create/enable rollback behavior remains mandatory.

The shared transaction and its low-level diagnostics become feature-neutral. RFID-, storage-, and system-routing-specific context is logged by their owning initialization layer rather than hardcoded as `RFID hooks` inside the reusable transaction.

### Match contract

A path matches the logical system root only when it is, case-insensitively:

- exactly `D:\system`; or
- a descendant beginning with `D:\system\`.

Matching is performed by Windows path components, so either accepted directory separator is handled and a component such as `system2` cannot match. Lexically equivalent spellings are normalized without resolving symlinks or touching the filesystem.

Examples:

| Input | Result |
|---|---|
| `D:\system` | route |
| `d:\SYSTEM\DUA\work` | route |
| `D:\system2` | pass through |
| `D:\system-file` | pass through |
| `C:\system` | pass through |
| `COM2` | existing RFID behavior |

No caller-address or module filter is required. The configured system root is authoritative throughout the game process, and the exact component-boundary match prevents unrelated paths from being captured.

If the resolved destination is equivalent to `D:\system`, routing is a no-op and the original input can pass through unchanged.

### Hooked path APIs

The combined hook layer covers the binary-observed Ttx path surface:

- `CreateFileA`
- `CreateFileW`
- `FindFirstFileW`
- `CreateDirectoryW`
- `DeleteFileA`
- `DeleteFileW`
- `MoveFileA`
- `MoveFileW`
- `GetFileAttributesA`
- `GetFileAttributesW`

Existing A/W hooks required by test-mode storage remain available through the same union. Handle-based APIs such as `ReadFile`, `WriteFile`, `FindNextFile`, and `CloseHandle` do not perform path routing.

`MoveFileA/W` evaluates source and destination independently. If neither matches, it calls the original A or W function with the original pointers. If either matches, each matching operand is rewritten and each nonmatching operand retains its original path semantics.

### ANSI and Unicode calls

Wide matching calls combine their suffix with the pre-resolved native root and call the original W trampoline.

For matching ANSI calls whose API has an equivalent W signature, the router converts the source suffix from the active ANSI code page, combines it with the Unicode root, and calls the original W trampoline. This keeps a non-ASCII configured destination usable even though the downloader supplied an ANSI logical path. Nonmatching ANSI calls always use the original A trampoline.

`FindFirstFileA` is not part of the observed Ttx system-root surface and remains governed by existing test-mode storage behavior. `FindFirstFileW` supplies the required Unicode-safe routed enumeration.

Every detour is `noexcept`. Allocation or conversion failures are caught, logged, translated to an appropriate Win32 failure, and cannot escape into vendor code. Normal original return values and `GetLastError` behavior are preserved.

## Downloader Initialization Guard

After filesystem routing is installed, locate `TtxUpdateDownloader.dll` and resolve the observed decorated export `?TtxUDLInit@@YAHKKKK@Z`, which demangles to `TtxUDLInit`. Install a SafetyHook inline hook with the verified `int __cdecl(unsigned int, unsigned int, unsigned int, unsigned int)` ABI. The hook starts disabled and is enabled only after successful construction. A binary that lacks this exact supported export fails with the explicit unsupported-downloader diagnostic rather than falling back to an RVA or ordinal guess.

The detour:

1. calls the original through the SafetyHook trampoline;
2. returns the original success result unchanged;
3. on zero, immediately captures `GetLastError`;
4. publishes one diagnostic containing the configured spelling, resolved path, error code, and corrective guidance;
5. shows an error popup;
6. terminates the process before returning to `game471.exe`, with fail-fast as a final fallback.

This guard is installed even when registry virtualization is disabled because the machine's real `Country` value may still activate the updater. It is defense-in-depth for all remaining updater initialization failures, not a substitute for root preflight.

`TtxUDLGetStatus` is not hooked. A failed `TtxUDLInit` never returns control to the game, so the unsafe status call is unreachable. Successful initialization retains the original downloader behavior.

## Failure Reporting

Every fatal setup message includes:

- the failed operation;
- the configured and resolved paths when relevant;
- the Win32 or filesystem error code and text;
- a concrete correction, such as enabling registry virtualization, creating a writable `D:\system`, selecting a writable custom root, using `.\system`, or granting permission to update `config.toml`.

Fatal reporting follows the repository's existing pattern: log, modal error, `TerminateProcess`, then fail-fast fallback. A one-shot latch prevents duplicate dialogs.

Failures that stop startup include:

- malformed, ambiguous, or invalid configuration;
- inconsistent legacy path values;
- unavailable custom root;
- unavailable real `D:\system` in registry-disabled mode;
- failure to create the approved fallback;
- failure to persist migration or fallback;
- MinHook resolution, creation, or enable failure;
- missing or unhookable `TtxUDLInit` in the supported game setup;
- any original `TtxUDLInit` failure.

## Testing Strategy

### Configuration and resolution tests

- Default `D:\system` exists: keep it without rewriting configuration.
- Default root is absent but creatable: create it and keep it.
- Default root cannot be created with registry enabled: create `.\system`, persist it, and use its absolute resolved path.
- Custom absolute root exists or is creatable: use it.
- Custom relative `system` and `.\system` roots resolve against the config directory.
- Custom root cannot be created: return a fatal error without fallback.
- Registry disabled with usable `D:\system`: continue without routing or rewriting.
- Registry disabled with unusable `D:\system`: return the disabled-mode fatal.
- Required downloader and service children are created.
- Unicode config directory and Unicode custom root use native paths correctly.
- ANSI-incompatible absolute service path produces the documented actionable error.

### Migration and persistence tests

- Exact legacy default leaves migrate to `D:\system`.
- Consistent custom legacy leaves migrate to their common root.
- Registry-enabled legacy migration is persisted; registry-disabled legacy parsing does not rewrite the file.
- Missing, inconsistent, or structurally invalid leaves fail.
- New and legacy path fields together fail as ambiguous.
- Unknown fields remain strict errors.
- Successful replacement writes one canonical new-schema file.
- Temporary write, flush, and atomic replacement failures preserve the original file and stop startup.

### Router tests

- Exact-root, descendant, separator-boundary, and ASCII case-folding behavior.
- `D:\system2`, other drives, relative inputs, null pointers, and unrelated paths pass through.
- Matching W calls route to the absolute native destination.
- Matching A calls route through their W originals where specified.
- Nonmatching A and W calls preserve original pointers and API selection.
- `MoveFileA/W` covers neither, source-only, destination-only, and both operands matching.
- Existing `COM2` behavior wins for RFID calls and is unchanged.
- Test-mode storage and system-root policies compose without double routing.
- Conversion/allocation failures do not throw across a detour.

### Hook transaction tests

- The request union contains every API required by the active policies exactly once.
- Storage disabled no longer suppresses APIs required by system routing.
- Combined RFID, test-mode storage, and system routing remains within the documented capacity.
- Resolve, initialize, create, and enable failures roll back all owned hooks.
- Failure after MinHook commit but before SafetyHook activation rolls back MinHook and deactivates dispatch.

### Ttx guard tests

- A successful original `TtxUDLInit` returns its result unchanged.
- A zero result captures the original last error and publishes one fatal diagnostic.
- Repeated failure publication does not show repeated dialogs.
- Hook creation or activation failure produces the supported-binary startup error.
- No exception crosses the hook boundary.

### Build and runtime acceptance

Automated verification includes the full configured x86 build and CTest suite, with focused tests for the new resolver, migration, router, request union, and initialization guard.

Runtime acceptance remains separate and must cover:

1. Registry virtualization enabled on a machine without a usable `D:` drive: `.\system` is created and persisted, the full tree is used, and the updater does not crash.
2. Registry virtualization disabled without usable `D:\system`: startup stops with the explicit configuration error.
3. A writable custom root: Ttx operations and derived service paths reach the same tree.
4. An intentional `TtxUDLInit` failure: the loader shows the fatal diagnostic instead of allowing the later critical-section access violation.

Automated evidence establishes build and behavior contracts; it does not overclaim successful user runtime acceptance.

## Result

One path setting becomes the storage source of truth. The service receives derived values, the game process transparently maps the downloader's hardcoded `D:\system` namespace through the already-owned Kernel32 hooks, and the unsafe vendor initialization contract is guarded directly with SafetyHook. Systems without a `D:` drive recover automatically only from the shipped default, while custom and disabled-mode errors stop clearly before the crash path can occur.
