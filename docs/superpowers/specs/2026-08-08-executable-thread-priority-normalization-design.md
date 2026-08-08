# Executable-Scoped Thread Priority Normalization Design

Date: 2026-08-08

## Context

A reported Groove Coaster card-load sequence advances through native NESYS
requests roughly every 3.6 seconds even though the local HTTP server completes
each request in tens of milliseconds. The same sequence can run normally, and
the problem has not reproduced reliably on the development machine.

Daemon-backed IDA analysis of the supported `game471.exe` and
`NesysService.exe` binaries found that the request path crosses several threads
whose priorities are explicitly lowered after creation. The original arcade
platform used a single-core Intel Atom 230, while supported local installations
are expected to run on substantially more capable Windows 10-class hardware.
An executable-scoped priority-normalization build will test whether the native
priority policy is starving request-transfer or reply-processing work on the
affected machine.

The implementation belongs in `H:\gc\artifacts\GCLoader`. The runtime tree at
`H:\gc` supplies binaries, IDBs, and logs as evidence and is not modified or
deployed to by this work.

## Binary Evidence

The analysis applies to these binaries:

| Image | SHA-256 |
|---|---|
| `game471.exe` | `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522` |
| `NesysService.exe` | `487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2` |

Both binaries create threads at the Windows default relative priority and then
call `SetThreadPriority` explicitly. Neither thread-creation API accepts the
final scheduling priority that produced the suspected delay, so detouring
`CreateThread`, `_beginthread`, or `_beginthreadex` would not prevent a later
priority downgrade.

IDA found six statically direct negative-priority calls in `game471.exe`:

| Call address | Requested priority | Relevant role |
|---:|---:|---|
| `0x004C8B1F` | `-1` (`BELOW_NORMAL`) | NESYS reply dispatcher |
| `0x004CB67D` | `-1` (`BELOW_NORMAL`) | NESYS pipe worker |
| `0x004CB6C6` | `-1` (`BELOW_NORMAL`) | NESYS pipe worker |
| `0x00566A85` | `-1` (`BELOW_NORMAL`) | game background worker |
| `0x0059E6CF` | `-2` (`LOWEST`) | `CNesysCommTask` update worker |
| `0x005AF397` | `-2` (`LOWEST`) | game background worker |

IDA found ten statically direct negative-priority calls in
`NesysService.exe`:

| Call address | Requested priority |
|---:|---:|
| `0x00401758` | `-2` (`LOWEST`) |
| `0x00405C4F` | `-2` (`LOWEST`) |
| `0x00406FE2` | `-2` (`LOWEST`) |
| `0x00409A31` | `-1` (`BELOW_NORMAL`) |
| `0x00409A76` | `-1` (`BELOW_NORMAL`) |
| `0x0040B3BC` | `-1` (`BELOW_NORMAL`) |
| `0x0041866A` | `-2` (`LOWEST`) |
| `0x0041B5CB` | `-2` (`LOWEST`) |
| `0x00420BFB` | `-2` (`LOWEST`) |
| `0x00420C2B` | `-2` (`LOWEST`) |

The game also contains normal, above-normal, highest, time-critical, and
save-and-restore priority calls. Those are not part of the suspected failure
and must retain their requested values.

## Goals

- Normalize every negative `SetThreadPriority` request made directly by
  `game471.exe` or `NesysService.exe` to `THREAD_PRIORITY_NORMAL`.
- Preserve normal and every higher requested priority exactly.
- Leave priority decisions made by GCLoader and third-party DLLs untouched.
- Install the policy before the NESYS process begins its normal execution.
- Use one guarded MinHook API detour rather than binary-specific instruction
  writes at all 16 current call sites.
- Produce a statically verified build that the affected operator can use for
  runtime acceptance.

## Non-Goals

- Hooking thread-creation APIs.
- Changing process priority classes, CPU affinity, MMCSS policy, input-worker
  priority, or audio priority.
- Reprioritizing threads that already existed before the hook was installed.
- Changing calls made by GPU, audio, Windows, CRT, or other loaded DLLs.
- Adding a configuration or ConfigGUI option for this diagnostic build.
- Removing pipe flushing, changing request serialization, or reusing WinHTTP
  sessions in the same change.
- Instrumenting the nondeterministic request pipeline.
- Modifying or incorporating the untracked failed `RequestDelayFix.cpp/.h`
  experiment.
- Treating build or automated-test success as proof that card loading is fixed.

## Architecture

Add a focused NESYS-owned component under `src/Nesys/` that contributes a
single `kernel32.dll!SetThreadPriority` request to the existing transactional
MinHook installation. The component is active in both process roles when the
existing NESYS network-virtualization feature is enabled.

The component owns:

- immutable bounds for the current process's main executable image;
- the original `SetThreadPriority` trampoline;
- the caller and priority policy;
- one activation log and one first-clamp diagnostic per process.

The main-image bounds are computed once before hook activation from
`GetModuleHandleW(nullptr)` and validated PE headers. No module enumeration or
address lookup occurs in the detour's hot path.

`NesysFeaturePlan` gains an explicit `thread_priority_override` component. It
is enabled when `network_virtualization` is enabled, adds one API hook to both
the game and NESYS process plans, and is installed in the same all-or-nothing
MinHook transaction as the existing NESYS API hooks.

## Priority Policy

The detour captures its immediate return address with `_ReturnAddress()` and
applies this policy:

| Caller | Requested value | Forwarded value |
|---|---:|---:|
| Inside current main executable | `< THREAD_PRIORITY_NORMAL` | `THREAD_PRIORITY_NORMAL` |
| Inside current main executable | `>= THREAD_PRIORITY_NORMAL` | unchanged |
| Outside current main executable | any value | unchanged |

This normalizes `THREAD_PRIORITY_IDLE`, `THREAD_PRIORITY_LOWEST`, and
`THREAD_PRIORITY_BELOW_NORMAL`. Positive special requests such as
`THREAD_MODE_BACKGROUND_BEGIN` and `THREAD_MODE_BACKGROUND_END` are passed
through unchanged because they are not negative priority values. Normal,
above-normal, highest, and time-critical requests are also unchanged.

The executable-origin guard deliberately uses the actual loaded image base and
`SizeOfImage`; it does not compare filenames or assume the preferred base. The
IDA-proven call sites are direct executable imports, so their return addresses
fall inside this range. Calls originating in `iDmacDrv32.dll` or any other DLL
do not.

## Win32 Contract Preservation

The detour forwards the original thread handle unchanged and returns the
original API result unchanged. It captures `GetLastError()` immediately after
the original call and restores it after any bounded first-hit diagnostic, so a
failed `SetThreadPriority` retains its Windows error contract.

No exception may cross the detour. Initialization rejects an unavailable main
module, invalid DOS or NT signature, overflowing image range, missing export,
or MinHook transaction failure through the existing NESYS initialization error
path.

## Logging

Successful initialization adds one component line per process:

```text
NesysServicePatch: component active name=thread_priority_override
```

The first normalized call in each process emits one bounded line containing
the process role, caller RVA, requested priority, and effective priority. An
atomic one-shot guard prevents further call logging. Pass-through calls do not
log.

This supplies operator evidence that both process-role hooks activated and
that at least one executable request was actually normalized without adding
per-request or per-frame logging.

## Testing

Add a focused `ThreadPriorityOverrideTests` executable under `tests/Nesys/`.
Tests exercise production policy and forwarding behavior through injected
executable bounds, caller addresses, and an original-function test seam.

Required cases are:

- executable callers map `IDLE`, `LOWEST`, and `BELOW_NORMAL` to `NORMAL`;
- boundary value `NORMAL` and all higher standard priorities pass unchanged;
- background-mode values pass unchanged;
- callers immediately below the image, at the exclusive image end, and in an
  arbitrary DLL range pass negative values unchanged;
- callers at the image base and last included byte are treated as executable
  callers;
- the original function receives the original handle and the computed
  priority exactly once;
- success and failure return values pass through;
- failure `LastError` survives the policy wrapper;
- invalid or overflowing executable ranges are rejected during initialization;
- hook export metadata identifies `kernel32.dll!SetThreadPriority`;
- game and NESYS network-enabled feature plans both own exactly one additional
  priority hook, while network-disabled plans do not.

Tests do not inspect production source text, patch live thread priorities, or
duplicate the binary call-site table as a fixture. The IDA table remains
reverse-engineering evidence rather than a source-coupled test oracle.

## Verification and Runtime Acceptance

Static implementation verification requires:

1. Observe the new focused test fail before production implementation.
2. Build and run focused Debug and Release tests.
3. Build the complete `msvc32-debug` and `msvc32-release` preset graphs and run
   both full CTest suites.
4. Run `git diff --check` and inspect final repository status.
5. Inspect the resulting DLL and logs sufficiently to confirm both role plans
   include the new API hook without changing the runtime tree.

These checks prove policy boundaries, Win32 forwarding, role-plan integration,
and build compatibility. They do not prove the nondeterministic runtime symptom
is resolved.

Runtime acceptance belongs to the affected operator and requires:

- both game and NESYS logs report the active component;
- each process reports a first normalized executable caller where expected;
- card loading is tested repeatedly across previously slow and fast starts;
- request inter-arrival timing is compared with the prior roughly 3.6-second
  cadence;
- gameplay, input, audio, and shutdown show no new regressions.

Deployment to `H:\gc` or distribution to the operator is not part of the code
change unless separately requested.
