# NesysService Adapter Stability Design

Date: 2026-06-28

## Context

Groove Coaster starts `NesysService.exe -app` during startup when the `\\.\pipe\nesys_games` pipe is absent. The service then exposes NESYS state to the game over that pipe.

IDA evidence from `NesysService.exe.i64` shows that the service is sensitive to the host network adapter stack:

- Startup calls the routine logged as `GetLowerMacAddrAdapter()`, implemented at `sub_406C00`, which walks `GetAdaptersInfo()` results and stores the linked-list index of the adapter with the numerically lowest MAC address.
- The selected adapter index is reused by adapter-info helpers to read IP address, mask, DHCP server, gateway, MAC address, and link state.
- The service uses `GetIfTable()` in `sub_406A30` for link state and treats every status except `IF_OPER_STATUS_NON_OPERATIONAL` as usable.
- The DHCP worker calls `IpReleaseAddress()` and `IpRenewAddress()` for the selected adapter before reporting DHCP completion.
- The game process only launches the service and connects to the pipe; it does not own the adapter logic.

The failure mode is therefore not just "the wrong adapter was selected". Some selected adapters can work, including virtual adapters, while others fail or hang because the service mutates the real Windows adapter state and reacts poorly to fragile link/IP states.

## Goals

- Keep the real `NesysService.exe` and named-pipe protocol.
- Prevent the service from releasing or renewing real Windows adapter leases.
- Avoid forcing a preferred adapter as the first fix.
- Keep game-only RFID, input, and frame patches out of the service process.
- Make the behavior reversible by config and visible in logs.

## Non-Goals

- Replacing `NesysService.exe` or emulating the `\\.\pipe\nesys_games` protocol.
- Patching `NesysService.exe` on disk.
- Reordering or synthesizing `GetAdaptersInfo()` results in the first implementation.
- Claiming full Wi-Fi or VPN compatibility before runtime evidence confirms it.
- Finding and patching an already-running `NesysService.exe`; the first implementation only handles services launched by the game while GCLoader is active.

## Design

Add a `NesysServicePatch` module to GCLoader.

The same `iDmacDrv32.dll` binary will support two roles:

1. Game process role.
   - Detected by process image name not being `NesysService.exe`.
   - Runs existing RFID/input/timer initialization.
   - Installs a MinHook hook for `kernel32!CreateProcessA`.

2. Service process role.
   - Detected by process image name `NesysService.exe`.
   - Skips all game-only initialization.
   - Runs only `NesysServicePatchInit()`.
   - Installs MinHook hooks for `IPHLPAPI!IpReleaseAddress` and `IPHLPAPI!IpRenewAddress`.

The game-side `CreateProcessA` wrapper only intercepts launches that resolve to `NesysService.exe` and include `-app` in the command line. For eligible launches it:

1. Adds `CREATE_SUSPENDED` to the creation flags.
2. Calls the original `CreateProcessA`.
3. Injects the current loader DLL path into the child with `VirtualAllocEx`, `WriteProcessMemory`, and `CreateRemoteThread(LoadLibraryW)`.
4. Waits up to five seconds for the injection thread to complete.
5. Resumes the service main thread.
6. Returns the original API result to the game.

The service-side hooks return `NO_ERROR` immediately and log the adapter index from the `IP_ADAPTER_INDEX_MAP` argument. This makes DHCP release/renew a no-op only inside `NesysService.exe`; metadata reads such as `GetAdaptersInfo()` stay real.

## Configuration

Add an experimental config option:

```toml
[experimental]
enable_nesys_service_adapter_patch = true
```

When disabled, the loader does not hook `CreateProcessA` for service injection, and the DLL does not install service-side IP Helper hooks.

The planned default is `true`; setting it to `false` restores the old service launch behavior without rebuilding. Logs must make it obvious whether the feature is enabled and whether the current process is running the game role or service role.

## Error Handling

The launcher hook is fail-open by default:

- If the launch is not `NesysService.exe -app`, call the original API unchanged.
- If `CreateProcessA` fails, return its result unchanged.
- If injection fails after the service process is created, log the exact failed step, resume the service thread, and return success to the game.
- If service-side MinHook setup fails, log the failure and let the service continue unpatched.

Fail-open keeps the loader from introducing a new startup blocker. A future strict/debug option can turn injection failure into process termination for controlled testing, but that is not part of the first implementation.

## Verification

Static and build verification:

- Add focused tests for config parsing and service-launch eligibility matching.
- Build `iDmacDrv32.dll` and `ConfigGUI.exe`.
- Confirm the service-role branch cannot run game-RVA patch initialization.

Runtime evidence:

- `loader-log.txt` shows game-role initialization.
- `loader-log.txt` shows interception of `NesysService.exe -app`.
- `loader-log.txt` shows child DLL injection success and resume.
- `loader-log.txt` shows service-role initialization inside `NesysService.exe`.
- `loader-log.txt` shows `IpReleaseAddress` and `IpRenewAddress` suppression when the service attempts DHCP repair.

Acceptance:

- With previously problematic adapter setups, the game starts without becoming non-responsive.
- Existing RFID/input/timer patches still initialize in the game process.
- No game-only patch attempts run inside `NesysService.exe`.

## Deferred Work

If suppressing DHCP release/renew is not enough, add a second evidence-backed hook set:

- Hook `GetIfTable()` inside `NesysService.exe`.
- Normalize the selected adapter's link state only when runtime logs show the service is misclassifying a fragile adapter as usable.
- Keep `GetAdaptersInfo()` real unless evidence shows adapter metadata itself must be virtualized.
