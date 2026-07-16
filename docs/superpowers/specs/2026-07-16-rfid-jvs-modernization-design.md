# RFID/JVS Modernization Design

Date: 2026-07-16

## Context

`RfidEmu.cpp` currently combines several independent responsibilities:

- JVS framing, escaping, response encoding, and command dispatch.
- RFID, coin, switch, keypad, and reader state.
- A fake Win32 COM-port device implemented through Kernel32 hooks.
- Process-lifetime card-key polling.
- Test-mode filesystem redirection and disk-space hooks.
- MinHook initialization and installation for every affected API.

The implementation works for the game's current traffic, but much of it predates the modern C++ used elsewhere in GCLoader. It relies on raw fixed buffers, pointer/length pairs, macros, unchecked indexing, global mutable state, an unbounded byte queue, manual output parameters, and an unowned thread. Hook installation ignores individual failures and enables every MinHook hook in the process.

The refactor covers the entire current file. Internal callers will be updated directly; no compatibility facade for `RfidEmuInit()` or the root-level `RfidEmu` files will remain. The stable compatibility surface is the behavior visible to `game471.exe` through its hooked COM-port and filesystem calls.

## Evidence and Authority

### Normative JVS reference

The repository copy of the normative reference is:

- [JAMMA VIDEO Standard, Third Edition (`JVST_VER3.pdf`)](../../references/JVST_VER3.pdf)
- SHA-256: `E1D4128B21A896C7C299AE5DBC1009B51E11D2E2AEDCC46AA5DD090EA7AB7A88`

The relevant sections are:

- PDF pages 15-16, printed pages 13-14: communication protocol, packet structure, escaping, checksum, size, and timing.
- PDF pages 17-18, printed pages 15-16: automatic address assignment.
- PDF pages 19-21, printed pages 17-19: command format, multiple commands, packet status, and command reports.
- PDF pages 22-32, printed pages 20-30: command-specific request and acknowledgement formats.

The standard is authoritative for JVS framing and standard command semantics.

### Informative English JVS reference

The repository also includes an English draft for readable field-level descriptions:

- [JAMMA Video Standard English Draft (`jvs_wip.pdf`)](../../references/jvs_wip.pdf)
- SHA-256: `76BF75A66ADD2A86EC889E4AD28906D9C97BC36EF76D9EC1A31309173BD41139`

Useful printed pages are page 7 for framing and request/acknowledge fields, page 11 for packet status and command report codes, page 18 for miscellaneous-switch input `0x26`, page 19 for generic output `0x32`, and pages 21-22 for Taito Type X commands. The official Third Edition remains normative. Game traffic and the original implementation remain authoritative for the RFID controller's board-specific extensions.

### Game binary evidence

The analyzed database is `H:\gc\game471.exe.i64`. Read-only daemon-backed IDA analysis established:

- `sub_503530` at `0x503530` constructs one complete escaped request and passes it to `sub_503960`, the game's `WriteFile` wrapper.
- Retry and retransmission paths in `sub_503220` also write complete frames.
- `sub_503220` parses one acknowledgement across repeated `ReadFile` chunks, retaining decode state until one complete frame is available.
- Once `sub_503220` completes one frame, it returns immediately. Bytes belonging to a second frame in the same `ReadFile` result would be discarded.
- `sub_5039A0` uses `ClearCommError().cbInQue` to choose its read size and performs a one-byte read when the queue is empty.
- `sub_503A60` opens the serial device, calls `SetupComm(0x204, 0x204)`, configures 115200 baud and 8N1 operation through `GetCommState`/`SetCommState`, sets event mask `1`, and configures a 20 ms total read timeout.
- `sub_503910` consumes `MS_CTS_ON` from `GetCommModemStatus` as part of address discovery.
- Custom dongle traffic uses values outside the normal standard slave-address vocabulary. The current game patch bypasses that dongle check, but the protocol representation must not prevent future emulation.

The binary is authoritative for the game's Win32 call sequence and delivery behavior.

### Compatibility rule

For valid traffic currently used by the game, the refactor must produce the same game-visible responses and state changes. The refactor may and should fix:

- Out-of-bounds reads and writes.
- Integer and capacity overflows.
- Uninitialized Win32 output structures.
- Missing byte-count outputs.
- Invalid checksum, framing, and escaping behavior.
- Partial hook installation.
- Resource-handle leaks.

Where the current implementation contradicts the JVS standard only for malformed or previously unsupported traffic, the standard-compliant safe behavior wins.

## Goals

- Replace the monolithic file with focused directories and independently testable units.
- Implement JVS framing, escaping, checksum, packet size, and multi-command behavior from the supplied standard.
- Preserve all current standard and Taito-specific behavior used by `game471.exe`.
- Allow arbitrary address, command, status, and report byte values for custom devices.
- Emulate the game's COM port with coherent state rather than canned success returns.
- Derive fixed buffer capacities from the JVS wire format.
- Eliminate unchecked pointer/length interfaces from project-owned C++.
- Make all requested hook installation atomic and fail closed.
- Preserve process-lifetime RFID/card-reader ownership while starting its worker outside loader lock.
- Separate test-mode storage behavior from RFID/JVS behavior.
- Preserve automated proof and gameplay acceptance as separate verification layers.

## Non-Goals

- Re-enabling or emulating the currently bypassed custom dongle.
- Restricting packets to only standard JVS addresses or command codes.
- Changing card contents, coin behavior, switch behavior, or Taito command semantics.
- Simulating physical JVS bus delays or transceiver turnaround.
- Building a general-purpose physical serial-port emulator.
- Moving RFID card input into the FastIO input worker.
- Changing test-mode storage paths or on-disk behavior.
- Keeping source compatibility with `RfidEmu.h`, `RfidEmu.cpp`, or `RfidEmuInit()`.
- Refactoring NESYS or any other feature's existing MinHook ownership.
- Replacing source-faithful Win32 and MinHook seams merely to use newer syntax.

## Directory and Module Architecture

The target layout is:

```text
Rfid/
  Feature.h
  Feature.cpp
  ComPortState.h
  ComPortState.cpp
  Runtime.h
  Runtime.cpp
  State.h
  State.cpp
  TaitoCommands.h
  TaitoCommands.cpp
  Jvs/
    Types.h
    Decoder.h
    Decoder.cpp
    Encoder.h
    Encoder.cpp
    Device.h
    Device.cpp

TestModeStorage/
  Redirector.h
  Redirector.cpp
  Hooks.h
  Hooks.cpp

Win32Hooks/
  Kernel32Hooks.h
  Kernel32Hooks.cpp
  MinHookTransaction.h
  MinHookTransaction.cpp

tests/
  Rfid/
    JvsCodecTests.cpp
    JvsDeviceTests.cpp
    ComPortStateTests.cpp
    RfidRuntimeTests.cpp
  TestModeStorage/
    TestModeStorageRedirectTests.cpp
  Win32Hooks/
    Kernel32HookTests.cpp
```

The listed module boundaries and ownership are required. Private translation-unit helpers may be added, but responsibilities must not move across these boundaries. The dependency direction is:

- `gc::rfid::jvs::Decoder` and `gc::rfid::jvs::Encoder` depend only on platform-free JVS types.
- `gc::rfid::jvs::Device` depends on decoded JVS types and RFID state, not on Win32 or MinHook.
- `gc::rfid::Runtime` owns process-lifetime card input and the stateful emulated port.
- `gc::win32_hooks::Kernel32Hooks` is the only owner of the hooked Kernel32 entry points.
- `TestModeStorage` owns redirection policy and never depends on RFID/JVS protocol code.
- `gc::rfid::Feature` composes the RFID units and registers them with the shared Win32 hook adapter.
- `dllmain.cpp` calls the new initialization functions directly.

`Feature` is a composition root, not a compatibility facade. The old root-level RFID source and header are deleted after all callers and build files are updated.

Loader initialization occurs in this order:

1. Read and validate configuration.
2. Allocate the process-lifetime RFID and COM-port state.
3. Register RFID and enabled test-mode storage routing policies with the shared Kernel32 adapter.
4. Build the exact requested hook set.
5. Install and enable that hook set atomically.

The card worker is not started during this sequence.

## JVS Packet Model

### Open value types

The wire format is standard, but the values carried by it are extensible:

- `gc::rfid::jvs::Address` stores any `std::uint8_t`.
- `gc::rfid::jvs::CommandId` stores any `std::uint8_t`.
- Packet status and command report values also preserve any byte value.

These types expose named helpers and comparisons without rejecting custom values. Standard values are `inline constexpr` constants in a standard-value namespace. Helpers such as `is_master()`, `is_standard_slave()`, and `is_broadcast()` classify an address but do not validate construction.

The decoder never rejects a packet merely because its address or command is not standard. The emulation represents the host board for the complete JVS connection, so the device layer dispatches every complete packet regardless of its destination address. The address remains available for diagnostics and command-specific semantics; command interpretation belongs to the selected device.

### Decoded packets

A request packet contains:

- One address.
- A decoded byte-count field.
- Zero or more command/data bytes.
- One checksum byte.

An acknowledgement contains:

- Address `0` for the master.
- A decoded byte-count field.
- One packet status.
- Ordered command reports and report data.
- One checksum byte.

The byte-count value covers every decoded byte following the count through and including the checksum. It is computed before escaping. The checksum is the low eight bits of the sum from the address through the final byte before the checksum.

One request may contain multiple commands. The device executes them in order and returns one packet status plus one ordered report for each command that was executed.

### Escaping and synchronization

- Raw `0xE0` is the synchronization byte.
- Raw `0xD0` is the marker byte.
- After the synchronization byte, decoded `0xE0` is encoded as `0xD0 0xDF`.
- After the synchronization byte, decoded `0xD0` is encoded as `0xD0 0xCF`.
- Other bytes are unchanged.
- Decoding occurs before byte-count and checksum interpretation.
- An unescaped `0xE0` always begins a new packet and resynchronizes the stream.
- A marker at the end of an input chunk remains pending until the next byte arrives.

`gc::rfid::jvs::Decoder` consumes arbitrary `std::span<const std::byte>` chunks. A chunk may contain no complete packet, one packet, or multiple packets. Win32 `WriteFile` boundaries are not protocol boundaries.

### Capacity

The standard permits a decoded byte-count value of up to 255. The maximum decoded storage after the count is therefore 255 bytes.

The worst-case encoded frame is:

```text
1 synchronization byte
+ 2 encoded bytes for the address
+ 2 encoded bytes for the byte count
+ 2 * 255 encoded bytes after the count
= 515 bytes
```

The design uses:

- `std::array<std::uint8_t, 255>` plus a logical size for decoded packet data.
- `std::array<std::byte, 515>` plus a logical size for an encoded frame.
- `std::span` for non-owning input and output views.

No arbitrary 1 KiB packet buffers remain.

### Decode and encode results

The incremental decoder emits typed events:

- A structurally valid packet with a valid checksum.
- A complete addressed packet with a checksum failure.
- A recoverable framing error used for diagnostics and resynchronization.

Incomplete input is normal state, not an error. The decoder never reads beyond the supplied span and never grows an unbounded buffer.

The encoder returns `std::expected<EncodedFrame, EncodeError>`. Encoding fails if the decoded acknowledgement cannot fit the standard byte-count limit or if an internal invariant is violated.

## Device Semantics

`gc::rfid::jvs::Device` owns standard request/acknowledgement behavior:

- Every complete packet is dispatched regardless of whether its address is `0x00`, the currently assigned address, another standard or custom address, or `0xFF`.
- The assigned address is retained as emulated bus state and is never a packet-admission filter.
- Address `0xFF` remains classified as broadcast. Broadcast-only requirements are enforced by the individual commands whose semantics require them, including reset and address assignment, rather than by global routing.
- A checksum failure at any address returns packet status `0x03`.
- The RFID controller's observed `0x26` request includes one selector byte per requested input byte. Dispatch consumes those selector bytes before locating the next concatenated command, clamped to the remaining packet so the original behavior cannot advance out of bounds.
- An unsupported command returns packet status `0x02`.
- Commands following an unsupported command in the same packet are discarded.
- Reports for commands successfully executed before the unsupported command are retained.
- Invalid input-command parameters return report `0x02` with no requested data.
- Invalid output-command parameters return report `0x03` and discard the output data.
- An acknowledgement that cannot fit the standard limit returns packet status `0x04`.

Standard JVS handlers and Taito-specific handlers remain visibly separate. Taito commands are not mislabeled as standard commands. Their successful acknowledgement payloads remain byte-for-byte identical to the original implementation at every address. Future custom devices, including a dongle implementation, may introduce their own command handlers without modifying the codec or adding a global address filter.

## Stateful COM-Port Emulation

`gc::rfid::ComPortState` represents one logical process-lifetime device. It owns:

- Open/closed state and the emulated handle identity.
- Current `DCB`.
- Current `COMMTIMEOUTS`.
- Current event mask.
- Current modem and line-control state.
- The JVS decoder.
- At most one pending encoded acknowledgement and its read cursor.
- Assigned JVS address and RFID device state.

### Initialization sequence

The adapter must support the exact binary-observed sequence:

1. `CreateFileA("COM2", ...)`
2. `SetupComm(0x204, 0x204)`
3. `GetCommState`
4. `SetCommState` with 115200 baud, 8 data bits, no parity, one stop bit, and disabled flow control
5. `SetCommMask(1)`
6. `GetCommTimeouts`
7. `SetCommTimeouts` with a 20 ms total read timeout

Setters validate and store coherent state. Getters return the complete stored structure, including required size fields and deterministic flag values. They do not return success with uninitialized memory.

### I/O behavior

- `WriteFile` always initializes `lpNumberOfBytesWritten` on the synchronous game path.
- Accepted bytes are fed to the incremental JVS decoder.
- Each applicable request produces at most one acknowledgement.
- If another acknowledgement-requiring request arrives before the current acknowledgement is fully read, it is a protocol-sequencing violation. The new request is discarded and diagnosed; the existing acknowledgement remains intact.
- `ReadFile` always initializes `lpNumberOfBytesRead`.
- A read drains only the current acknowledgement and may return a fragment.
- Bytes from two acknowledgement frames are never coalesced into one game-visible `ReadFile`, because the binary returns after its first completed frame.
- `ClearCommError` initializes both the error output and the full `COMSTAT`, then reports only the bytes remaining in the current acknowledgement through `cbInQue`.
- `GetCommModemStatus` preserves the binary-required `MS_CTS_ON` relationship to JVS address state.
- `EscapeCommFunction` records supported line-state changes and returns a compatible result.
- Calls using any handle other than the emulated COM handle go directly to the original Win32 function without altered arguments or error state.

Invalid synchronous arguments fail with a matching Win32 error rather than dereferencing null pointers. No C++ exception crosses a hooked Win32 interface.

## Timing Model

The virtual device is deliberately timing-free:

- A complete request is decoded and handled synchronously.
- Its acknowledgement is available before the hooked `WriteFile` returns.
- `ReadFile` drains available data immediately.
- Empty reads preserve the current nonblocking behavior.
- Configured timeout values are stored and returned but do not introduce sleeps.
- The JVS codec and device contain no clock dependency.

Physical bus turnaround, packet dead time, and acknowledgement delay limits remain documented standard constraints but are not simulated for an in-process virtual device.

## Process Lifetime and Card Input

The RFID/card-reader lifetime is the game-process lifetime. No ordinary shutdown or join path is added.

Hook installation occurs during loader initialization. The card-key worker is started lazily on the first intercepted `COM2` open, outside loader lock. Initialization is guarded so only one worker is ever started.

The worker:

- Uses `std::thread` and detaches explicitly after successful creation.
- Uses `std::chrono` for the existing 100 ms polling interval.
- Preserves edge-triggered one-shot card scans.
- Writes only to process-lifetime state with explicit atomic or locked synchronization.

The state is intentionally process-lifetime and remains valid until Windows terminates the process. A `std::jthread` is not used because there is no stop/join lifecycle to own.

If the worker cannot start, the intercepted `COM2` open fails. The game must never observe a nominally valid reader that cannot accept card scans.

## Test-Mode Storage Separation

The path-policy code currently in `TestModeStorageRedirect.*` moves under `TestModeStorage/`. The filesystem and disk-space wrappers currently embedded in `RfidEmu.cpp` move with it.

`gc::win32_hooks::Kernel32Hooks` owns each imported function hook exactly once. For `CreateFileA/W`, routing order is:

1. Recognize the exact emulated COM-port path and route it to RFID.
2. Apply enabled test-mode storage redirection.
3. Call the original Win32 function unchanged.

Storage-only hooks are installed only when the feature requires them. Their path and return-value behavior is preserved by characterization tests.

## Atomic Hook Installation

The current unchecked `MH_CreateHookApi` sequence and `MH_EnableHook(MH_ALL_HOOKS)` are removed.

`gc::win32_hooks::MinHookTransaction`:

- Describes every requested hook in fixed data.
- Initializes MinHook once and treats an already-initialized runtime as usable.
- Resolves and creates only the owned hook set.
- Enables only those hooks.
- Records the exact failure stage and Win32/MinHook detail.
- Rolls back every hook created by the transaction if any step fails.
- Never enables, disables, or removes unrelated hooks through `MH_ALL_HOOKS`.

Initialization is all-or-nothing. Failure to install any requested hook returns failure to `DllMain`, which aborts DLL attachment. Partial COM, storage, or process hooks are not allowed.

## Modern C++ Rules

The refactor uses modern facilities where they improve invariants:

- `std::array` for standard-derived fixed capacities.
- `std::span` for contiguous views.
- `std::expected` for fallible internal operations.
- `std::optional` for incomplete decode state and the pending acknowledgement.
- `std::variant` for distinct decode events.
- `std::byte` for raw transport bytes.
- `std::uint8_t` and open strong value types for decoded protocol fields.
- `inline constexpr` named standard and vendor values.
- `std::thread`, `std::once_flag`, and `std::chrono` for process-lifetime lazy polling.
- `std::mutex`, `std::scoped_lock`, and atomics only where state is genuinely shared.
- `[[nodiscard]]` on results whose failure must be handled.
- Range algorithms where they state a complete transformation more clearly than an indexed loop.

The refactor does not use unavailable C++23 `<scope>` facilities. It does not replace clear binary copies or platform calls merely for stylistic novelty.

## Error and Diagnostic Policy

Protocol-declared failures are represented as protocol values, not exceptions. Structural failures that do not form a valid packet are discarded at the next synchronization point without fabricating an acknowledgement.

Diagnostics:

- Identify framing, checksum, command, capacity, hook-installation, and worker-start failures distinctly.
- Include address, command, decoded length, and failure stage where safe.
- Emit one feature-activation summary after the complete hook transaction succeeds.
- Keep successful hook resolution, COM calls, JVS decoding, retransmission, and reply queuing silent. These paths are normal operation and may execute on every poll.
- Log hook-installation failures, rollback, Win32 emulation failures, malformed frames, checksum failures, sequencing violations, and invalid retransmission requests.
- Include a bounded byte dump only on an actual I/O failure where the bytes help diagnose that failure; never dump successful traffic.
- Never log card payloads or other sensitive data.
- Preserve the original Win32 error for calls forwarded to the real API.

There is no production trace-mode setting. Investigation-only success tracing is removed once runtime compatibility has been established, rather than retained as dormant logging code.

## Automated Verification

### Characterization

Before replacing the monolith, focused tests record current valid behavior for:

- Address assignment and reset.
- JVS revision and feature queries.
- Switch, coin, RFID/card, and general-purpose I/O commands.
- Taito-specific commands currently used by the game.
- Multiple commands in one request.
- One-shot and repeated card scans.
- Test-mode storage routing.

### Codec conformance

Tests use the committed standard and cover:

- The escaping example on PDF page 16.
- Every possible chunk split across representative encoded frames.
- Multiple packets in one chunk.
- Noise before synchronization.
- Resynchronization on a new `0xE0`.
- A marker split across chunks.
- Escaped `0xE0` and `0xD0` in every structural field after synchronization.
- Valid and invalid checksums.
- Minimum packet size.
- The 255-byte decoded maximum.
- The 515-byte worst-case encoded frame.
- Deterministic encode/decode round trips over generated byte patterns.
- Arbitrary custom addresses and command byte values.

### Device semantics

Tests cover:

- Standard reset and address assignment.
- Address-independent dispatch at `0x00`, assigned, arbitrary custom, and broadcast addresses while preserving the original acknowledgement payloads.
- Multiple commands and ordered reports.
- Unsupported-command truncation with earlier reports retained.
- Input and output parameter-error variants.
- Checksum-error status.
- Acknowledgement-overflow status.
- Standard and Taito command dispatch remaining distinct.
- An unimplemented custom device not being rejected by the codec.

### COM adapter

Tests replay the IDA-confirmed initialization and polling sequence:

- Stateful `DCB`, timeouts, mask, and line values.
- `MS_CTS_ON` transitions during address discovery.
- Correct synchronous read/write byte counts.
- Complete `ClearCommError` outputs.
- Fragmented acknowledgement reads.
- No coalescing across acknowledgement frames.
- A pipelined request being rejected while the existing acknowledgement remains readable.
- Empty reads.
- Invalid pointer and handle behavior.
- Lazy worker initialization exactly once.
- Failure to start the worker causing COM open failure.
- Unrelated handles forwarding unchanged.

### Hook and storage behavior

A fake hook backend verifies:

- Success for the complete requested hook set.
- Failure at each resolution, creation, and enable stage.
- Reverse-order rollback.
- No unrelated `MH_ALL_HOOKS` operations.
- Shared `CreateFileA/W` routing order.

Existing test-mode storage redirection cases remain and gain adapter-level coverage for enabled, disabled, ANSI, wide-character, and pass-through calls.

### Build verification

The implementation must:

- Build all affected x86 targets with the repository's active MSVC toolchain.
- Pass the focused RFID/JVS, COM, hook, and storage tests.
- Pass the complete existing CTest suite.
- Leave the source tree free of temporary probe and generated review files.

Automated verification proves structure, byte behavior, and integration. It does not prove gameplay.

## Manual Gameplay Acceptance

Gameplay acceptance is performed by the user and reported separately from build/static evidence. The manual pass covers:

- Normal game boot through the emulated COM2 device.
- JVS reset and address discovery completing.
- Existing standard and Taito command paths remaining accepted.
- A card scan being observed exactly once.
- A later card scan being observable again.
- Coin, switch, service, and reader behavior remaining unchanged.
- Test-mode storage behavior when enabled and disabled.
- Ordinary filesystem calls not intended for redirection remaining unchanged.
- No startup hang, partial-hook behavior, or unexpected serial timeout.

The currently patched-out dongle check remains patched out. Successful gameplay does not imply that a custom dongle device has been implemented.

## Completion Criteria

This modernization is complete when:

- `RfidEmu.cpp`, `RfidEmu.h`, and `RfidEmuInit()` no longer exist.
- Every responsibility from the old file has an explicit owner in the approved directories.
- Valid game traffic remains byte- and state-compatible.
- Standard-defined malformed traffic receives standard-defined safe handling.
- Arbitrary custom values remain representable.
- All requested hooks install atomically or initialization fails.
- The card worker starts lazily and exactly once or COM open fails.
- Automated verification passes.
- The user accepts the in-game behavior.
