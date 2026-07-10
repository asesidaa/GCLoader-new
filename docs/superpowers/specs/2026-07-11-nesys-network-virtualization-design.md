# NESYS Network Virtualization Design

**Date:** 2026-07-11

**Status:** Approved design

**Supersedes:** `2026-06-28-nesys-service-adapter-stability-design.md` for future NESYS network behavior. The existing process-role split, suspended service injection, and DHCP-mutation suppression remain the implementation foundation.

## Problem

The current NESYS patch injects GCLoader into `NesysService.exe` and suppresses `IpReleaseAddress` and `IpRenewAddress`. Runtime evidence shows that this is too late and too narrow:

- `game471.exe::sub_5569C0` selects the adapter whose first six MAC bytes have the lowest numeric value. It does not require Ethernet, an active address, or a six-byte nonzero MAC.
- `game471.exe::sub_55F890` contains an unbounded wait on state derived from the selected MAC. A VPN-style adapter with an empty or zero MAC can therefore stall the game before the existing service mutation hooks are reached.
- Both binaries use WinHTTP. The current binaries contain patched hostnames, but routing must not depend on a particular hostname, domain, hosts-file entry, or local DNS service.
- `NesysService.exe::sub_420CB0` also has an active legacy IPv4 TCP path. It calls `gethostbyname` for non-numeric server names before `connect`, so modern resolver hooks alone would not provide a complete single-server override.
- IPv6 availability causes large delays between requests, including requests intended for a local server. The loader must prevent the two processes from producing IPv6 resolver candidates without changing Windows globally.

The durable solution is process-local network virtualization: both binaries see one stable IPv4 adapter, and every resolver-based server request is directed to one configured IPv4 address while Windows continues to own actual sockets and routing.

## Binary Analysis Evidence

The design is based on daemon-backed IDA analysis of the deployed binaries:

### `game471.exe`

- `sub_5569C0`: enumerates `GetAdaptersInfo` and selects the lowest six-byte MAC.
- `sub_556B90`, `sub_556D80`, `sub_55ECF0`, and `sub_565330`: copy and consume the selected adapter identity.
- `sub_55F890`: waits indefinitely while MAC-derived state remains zero.
- `sub_5662F0`: uses overlapped `NotifyAddrChange`, polls its event every 200 ms, and calls `CancelIPChangeNotify` during shutdown.
- `sub_5581F0`: creates WinHTTP sessions and requests.
- Raw UDP listeners bind to `0.0.0.0`; they do not bind to the selected adapter address.

### `NesysService.exe`

- `sub_406C00`: performs the same lowest-MAC adapter selection.
- `sub_406A30`: reads selected-interface operational state through `GetIfTable`.
- `sub_405D10` through `sub_406200`: derive broadcast, IPv4, MAC, mask, DHCP, and gateway data from `GetAdaptersInfo`.
- `sub_406750` and `sub_4068C0`: release and renew the selected interface.
- `sub_4065F0`: flushes interface neighbor tables.
- `sub_407480`: runs gateway/broadcast ping and local discovery work.
- `sub_408E40`: owns the complete raw-ICMP ping operation and receives its target in `EAX`.
- `sub_421680` calls `sub_420CB0` on the service TCP retry path; `sub_420CB0` uses `gethostbyname` for non-numeric targets and then connects with `AF_INET`.
- The HTTP request routines at `0x4020C0`, `0x4041D0`, `0x404410`, `0x4048E0`, and `0x404C90` use WinHTTP.
- UDP listeners bind to `0.0.0.0`; the service does not bind to the synthetic adapter address.

The analyzed sample hashes may be recorded as provenance, but they are not compatibility checks:

- `game471.exe`: `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`
- `NesysService.exe`: `487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2`

## Goals

- Make adapter selection deterministic and independent of physical, virtual, VPN, disconnected, or changing Windows adapters.
- Make the game and service consistently report an online, IPv4-only Ethernet adapter.
- Direct all resolver-based HTTP and legacy TCP server traffic to one configured IPv4 address.
- Bypass DNS, hosts-file lookup, and IPv6 candidate generation for redirected requests.
- Preserve each request's original hostname and port above the resolver boundary.
- Permit local-server play without internet access or working DNS.
- Leave actual socket creation, HTTP, TCP, proxy behavior, and routing under Windows control.
- Fail closed when the feature is enabled but cannot be installed completely.
- Allow unrelated on-disk binary patches without requiring a whole-file hash match.

## Non-goals

- Replacing `NesysService.exe` or emulating `\\.\pipe\nesys_games`.
- Rewriting domains, URLs, request paths, ports, HTTP headers, SNI, or certificates.
- Hooking `connect`, `WSAConnect`, `WinHttpConnect`, or `WinHttpSendRequest`.
- Redirecting direct numeric raw-TCP connections that bypass all resolver APIs.
- Disabling IPv6 system-wide or modifying a real Windows adapter.
- Implementing a real virtual NIC, route, DHCP server, DNS server, or gateway.
- Fabricating the real ARP table or all LAN-discovery traffic.
- Supporting multiple configured servers or per-protocol destination rules.

## Configuration Contract

Add a required top-level NESYS table:

```toml
[nesys]
server_ip = '127.0.0.1'
```

`server_ip` has the following contract:

- It is required when reflect-cpp parses an existing configuration. It is not wrapped in `DefaultIfMissing`.
- A newly constructed ConfigGUI model initializes it to `127.0.0.1`, so a newly generated file contains the field automatically.
- It accepts only a dotted-decimal IPv4 literal. Hostnames, IPv6 literals, schemes, paths, and `host:port` forms are rejected.
- ConfigGUI exposes it as **NESYS Server IPv4** in a dedicated NESYS section and refuses to save syntactically invalid input.
- The injected runtime validates it independently before installing hooks.
- The value contains no port. Every original request retains its original port.

The existing `experimental.enable_nesys_service_adapter_patch` field gates the complete feature: adapter virtualization, resolver overrides, service injection, mutation suppression, and ping redirection. The `[nesys]` table remains part of the required schema even when the feature is disabled.

## Architecture

The existing `iDmacDrv32.dll` remains one DLL with game and service roles. The NESYS feature is divided into three owned components:

### `NesysServiceLauncher`

Runs only in `game471.exe`. It intercepts eligible `CreateProcessA` calls for `NesysService.exe -app`, creates the child suspended, injects the current DLL, verifies successful DLL initialization, and resumes the child only after every service hook is active.

### `SyntheticNetworkAdapter`

Runs in both processes. It owns the immutable adapter profile, IP Helper query results, stable adapter-change behavior, and service-side mutation suppression.

### `ServerAddressOverride`

Runs in both processes for modern resolution and in the service for legacy resolution. It owns the validated configured IPv4, normalized Winsock hints, process-lifetime asynchronous hint storage, and thread-local legacy `hostent` storage.

The game role installs all three components. The service role installs `SyntheticNetworkAdapter`, `ServerAddressOverride`, mutation suppression, and the internal ping-target redirect; it skips RFID, input, framerate, and other game-address patches.

## Synthetic Adapter Contract

Exactly one immutable adapter is returned:

| Field | Value |
|---|---|
| Adapter name | `GCLoaderNesys0` |
| Description | `GCLoader NESYS IPv4 Adapter` |
| MAC | `DE-AD-BE-EF-00-01` |
| Interface index | `0x0BADC0DE` |
| Type | Ethernet |
| IPv4 | `192.0.2.2` |
| Mask | `255.255.255.0` |
| Gateway | `192.0.2.1` |
| DHCP server | `192.0.2.1` |
| DNS server | `192.0.2.1` |
| DHCP | Enabled |
| Administrative/operational state | Up |
| MTU | 1500 |
| Link speed | 1,000,000,000 bit/s |
| IPv6 | None |

`DE-AD-BE-EF-00-01` is a locally administered unicast MAC. `0x0BADC0DE` is positive, nonzero, and recognizable in hexadecimal logs while avoiding signed-index problems.

All structures use the native Windows caller-buffer contract: size probes update the required length, undersized buffers return the appropriate IP Helper error without overrunning the caller, and successful results contain self-consistent pointers, lengths, names, and indices. `IP_ADAPTER_INFO::AddressLength` is six, `CurrentIpAddress` points to its embedded IPv4 list, WINS is disabled, the lease range is `0` through `0x7FFFFFFF`, and all other unused or reserved fields are zero.

### Game API behavior

- `GetAdaptersInfo` returns only the synthetic adapter.
- `NotifyAddrChange` sets `*Handle` to null when supplied, sets the last error to `ERROR_IO_PENDING`, returns the pending result, and leaves the caller's event unsignaled.
- `CancelIPChangeNotify` returns success.

The verified watcher continues its normal periodic work, never receives a topology-change event, and exits without blocking because its notification handle remains null.

### Service API behavior

- `GetAdaptersInfo` returns only the synthetic adapter.
- `GetIfTable` returns one matching Ethernet row with both administrative and operational state set to up.
- `GetInterfaceInfo` returns one map containing index `0x0BADC0DE` and name `GCLoaderNesys0`.
- `GetNetworkParams` returns hostname `GCLoader`, an empty domain and scope, broadcast node type, routing/proxy disabled, DNS enabled, and one DNS entry at `192.0.2.1`; `CurrentDnsServer` points to that embedded entry.
- `IpReleaseAddress`, `IpRenewAddress`, and `FlushIpNetTable` return success without mutating Windows.
- `GetIpNetTable` remains unhooked so existing real ARP/local-discovery reads are not fabricated.

No API creates a Windows adapter or changes OS routes. The reported TEST-NET address is process-visible metadata only.

## Resolver Override

### Modern Winsock resolution

Both processes detour `WS2_32!GetAddrInfoW` and `WS2_32!GetAddrInfoExW` by exported API address. For every call with a non-null node name:

1. Replace only the node passed to the original Winsock trampoline with the process-lifetime configured IPv4 string.
2. Preserve the service name, socket type, protocol, namespace, timeout, result pointer, overlapped object, completion callback, and cancellation handle.
3. Force `ai_family = AF_INET`.
4. Add `AI_NUMERICHOST`.
5. Clear `AI_ADDRCONFIG`, `AI_V4MAPPED`, and `AI_ALL`.
6. Invoke the original function through its trampoline.

`AI_NUMERICHOST` makes Winsock parse the numeric address directly. It does not query DNS, the hosts file, LLMNR, or another hostname provider. Clearing `AI_ADDRCONFIG` prevents an offline machine's real adapter state from rejecting the configured local IPv4.

Null node names describe local bind/listener queries and pass through unchanged.

Because the real Winsock implementation still allocates successful results, existing `FreeAddrInfoW` and `FreeAddrInfoExW` ownership remains valid; those free APIs are not hooked.

Synchronous calls use a local normalized hints copy. Asynchronous `GetAddrInfoExW` calls use immutable process-lifetime normalized hints objects deduplicated by the preserved flags, socket type, and protocol. No stack-owned hints pointer is passed to an operation that can return `WSA_IO_PENDING`, and the caller's callback/event/cancellation mechanism remains untouched. This follows the asynchronous completion contract documented for [`GetAddrInfoExW`](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfoexw).

The WinHTTP layer retains its original hostname and port, so HTTP metadata, SNI, certificate policy, and URL construction are not rewritten. Only the address returned beneath WinHTTP changes.

### Legacy service resolution

`NesysService.exe` also detours `WS2_32!gethostbyname`. A non-null requested name returns a thread-local synthetic `hostent`: `h_name` is a thread-local copy of the requested name, the alias list is empty, and the `AF_INET` address list contains `[nesys].server_ip`. No original resolver is called. A null name passes through to the original API because it is not an outbound server lookup.

This covers the verified `sub_420CB0` TCP path while preserving its original port and leaving `connect` untouched. The service checks numeric strings with `inet_addr` before calling `gethostbyname`; direct numeric raw-TCP destinations therefore remain untouched by design.

### Resolver failure policy

If the original numeric `GetAddrInfo*` call fails, that Winsock error is returned. Allocation failure while preparing safe asynchronous hints returns the appropriate memory error. No path retries the original hostname.

## Service Ping Redirection

The service's internal ping entry is resolved at the fixed address:

```text
NesysService.exe module base + RVA 0x8E40
```

Before installation, the hook validates this exact 32-byte function-entry signature at that address:

```text
51 53 55 56 57 50 8B D9 8D 6B 04 6A 10 55 C7 44
24 1C 00 00 00 00 E8 02 73 02 00 83 C4 0C 8D 73
```

There is no whole-file hash gate and no signature scan. ASLR is handled by adding the fixed RVA to the loaded module base. Unrelated executable patches are accepted because only the target bytes are checked. A target-byte mismatch fails closed.

The x86 hook changes the saved `EAX` target pointer to a process-lifetime `"127.0.0.1"` string and then continues through the original function. It does not skip the function or fabricate its output. The elevated service performs its normal IPv4 raw-ICMP operation against loopback, fills every native result field, and performs its normal thread/socket cleanup.

This redirect covers startup gateway/broadcast probes and game-requested ping diagnostics without requiring LAN or internet ICMP access.

## Initialization and Fail-closed Lifecycle

`NesysServicePatchInit` becomes a Boolean initialization gate and runs immediately after logging and role detection, before any game-only initialization.

When the feature is enabled, each process:

1. Parses and validates the required configuration.
2. Creates immutable ANSI/Wide server-address state.
3. Resolves required exported APIs.
4. In the service, verifies the fixed-RVA ping signature.
5. Creates the complete process-role hook set.
6. Enables only hooks owned by this feature as one transaction.
7. Returns success only when every required hook is active.

Failure rolls back only this feature's hooks. It must not use global MinHook enable/disable operations that would affect RFID or other loader features.

When enabled, a local initialization failure makes `DllMain(DLL_PROCESS_ATTACH)` return `FALSE`. For the game this prevents startup with a partial NESYS policy. For the injected service, failed DLL initialization makes the remote `LoadLibraryW` call return null, which acts as the launcher readiness handshake.

The service launcher then:

- Never resumes an unpatched child.
- Terminates the still-suspended child.
- Waits for termination, closes owned process/thread handles, and clears the caller's `PROCESS_INFORMATION` output.
- Sets `ERROR_DLL_INIT_FAILED` and returns `FALSE` from the intercepted `CreateProcessA` call.

On success, the launcher preserves the caller's original suspended-state request: it resumes the primary thread only when the caller did not request `CREATE_SUSPENDED`.

When `enable_nesys_service_adapter_patch = false`, none of these hooks or injection behaviors are installed and the original binaries run unchanged.

## Diagnostics

Logging is bounded to avoid per-request noise:

- Process role and configured server IPv4.
- Synthetic adapter name, MAC, index, IPv4, and link state.
- Successful component and hook-group installation.
- Service child interception, injection success, initialization failure, termination, and resume decision.
- First invocation of each adapter-query, adapter-notification, modern resolver, legacy resolver, mutation-suppression, and ping-redirection family.
- Exact exported API status or fixed-RVA signature mismatch on failure.

Original resolver and ping inputs may be included in first-hit debug diagnostics. They are observability data only and never affect destination selection.

## Testing Strategy

### Configuration tests

- A newly constructed ConfigGUI model uses `127.0.0.1`.
- Serialized configuration includes `[nesys].server_ip`.
- Existing files missing the table or field fail parsing.
- Loopback, private, and public dotted-decimal IPv4 values round-trip.
- Hostnames, IPv6, schemes, paths, ports, and malformed IPv4 fail validation.
- The complete NESYS feature remains gated by the existing experimental Boolean.

### Synthetic adapter tests

- Every query implements its native size-probe, undersized-buffer, and successful-buffer contract.
- Every structure exposes the exact approved name, description, MAC, index, IPv4, mask, gateway, DHCP, DNS, link state, MTU, and speed.
- Cross-API adapter names and indices agree.
- Linked lists terminate correctly and contain no IPv6 entry.
- `NotifyAddrChange` remains pending without signaling or returning a real handle; cancellation succeeds.
- Mutation wrappers return success without invoking their originals.
- `GetIpNetTable` is absent from the owned hook list.

### Resolver tests

- Non-null modern resolver nodes are replaced with the configured numeric IPv4.
- Null nodes pass through unchanged.
- `AF_INET` and `AI_NUMERICHOST` are forced; IPv6/addrconfig flags are cleared.
- Service names, ports, protocols, namespaces, callbacks, overlapped objects, and cancellation handles are preserved.
- Synchronous and asynchronous hint lifetimes are safe under concurrent calls.
- Winsock-allocated results retain normal free ownership.
- Resolver errors never cause an original-hostname retry.
- Legacy `hostent` results contain the configured network-order IPv4 and remain thread-local.

### Internal-hook and lifecycle tests

- The fixed RVA plus matching 32-byte ping signature permits hook creation.
- A changed byte at the target rejects installation.
- Arbitrary changes elsewhere in an executable image have no effect.
- No executable hash or pattern scan participates in the decision.
- The ping callback replaces only the saved `EAX` target and continues original execution.
- Partial hook creation rolls back only owned hooks.
- Service DLL initialization failure terminates rather than resumes the child and returns `ERROR_DLL_INIT_FAILED`.
- Successful injection preserves caller-requested suspension semantics.
- Disabled mode installs nothing.

### Build and runtime acceptance

- Build `iDmacDrv32`, ConfigGUI, existing tests, and new focused NESYS tests with the x86 toolchain.
- Run the complete CTest suite.
- Start with a VPN adapter connected and preferred ahead of physical adapters.
- Leave IPv6 enabled on Windows adapters.
- Disconnect internet access and make DNS unavailable or deliberately invalid.
- Run the local server at the configured IPv4.
- Verify HTTP and legacy TCP reach the configured IPv4 on their original ports.
- Verify no IPv6 destination, external DNS lookup, or original-hostname fallback occurs.
- Verify first-hit logs show the synthetic adapter and ping redirection to `127.0.0.1`.
- Verify no ICMP packet for the original gateway/broadcast target leaves a real adapter.
- Compare real adapter addresses, DHCP leases, and operational state before and after the run; they must remain unchanged.
- Verify invalid/missing configuration, fixed-RVA signature mismatch, and forced injection failure all stop startup rather than running unpatched.
- Verify disabled mode retains the original process and network behavior.

## Acceptance Criteria

The feature is complete when:

1. VPN presence, ordering, MAC shape, and topology changes cannot affect the adapter seen by either binary.
2. Both binaries consistently see the approved IPv4-only synthetic adapter.
3. Resolver-based HTTP and legacy TCP use only `[nesys].server_ip` while preserving original ports.
4. The game works with a local server while internet, DNS, and Windows IPv6 remain unavailable or unsuitable.
5. Service pings execute only against `127.0.0.1`.
6. No real adapter is released, renewed, flushed, reconfigured, or selected by the game/service logic.
7. Enabled mode never resumes a partially patched service or falls back to an original hostname.
8. Whole-file executable hashes are never compatibility gates; exported APIs and the fixed-RVA local signature guard are the only hook validation boundaries.
