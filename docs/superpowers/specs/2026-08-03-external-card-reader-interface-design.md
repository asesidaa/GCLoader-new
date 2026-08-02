# External Card Reader Interface Design

Date: 2026-08-03

Status: Approved design contract

## Context

GCLoader currently supports manual RFID scans through the configured
`card_read` key. An armed scan reads a 16-digit decimal card number from
`card.txt` when the game requests the one-shot JVS card payload.

Some operators already have card readers working with other games. GCLoader
does not need to understand those readers or their protocols. Reader-specific
adapter software only needs a small local interface through which it can
submit the final card number and trigger the existing RFID scan path.

The distribution also needs a deliberately small test client for in-game
acceptance. It gives the operator one button that submits GCLoader's built-in
default card number through the same public interface an external adapter will
use.

The source repository is `H:\gc\artifacts\GCLoader`. `H:\gc` remains the
runtime and deployment tree and is outside this implementation's mutation
scope.

## Goals

- Expose a stable local interface that a separate Windows process can use.
- Accept a complete 16-digit decimal card number as one card-read event.
- Reuse the current JVS payload format and one-shot consumption behavior.
- Preserve the configured keyboard trigger and reloadable `card.txt` path.
- Keep reader discovery, reader protocols, and card-reading logic outside
  GCLoader.
- Require no new configuration or ConfigGUI controls.
- Ship a one-button client that exercises the real named-pipe contract during
  runtime testing.

## Non-Goals

- Detecting, opening, configuring, or polling physical card readers.
- Shipping adapters for specific readers or other games.
- Loading third-party reader plugins into the game process.
- Turning the runtime test client into a reader SDK, configurable adapter, or
  general-purpose card editor.
- Queueing scans, deduplicating cards, debouncing readers, or interpreting
  card contents.
- Supporting remote clients, configurable endpoints, authentication, or
  network transports.
- Changing the fixed eight-byte RFID prefix or the JVS protocol.
- Replacing the keyboard and `card.txt` workflow.
- Deploying a built DLL or changing files under `H:\gc`.

## Public Named-Pipe Contract

GCLoader exposes this process-lifetime Windows named pipe:

```text
\\.\pipe\GCLoader.CardReader
```

The endpoint has these fixed rules:

- It is a local-only, duplex, message-mode named pipe. Remote clients are
  rejected.
- The listener becomes available after the game first opens the emulated RFID
  COM2 device. A client that starts earlier must retry its connection.
- One connection submits one card-read request.
- The request is one message containing exactly 16 ASCII bytes. Every byte
  must be `0` through `9`. Whitespace, line endings, a byte-order mark, a NUL
  terminator, separators, and other encodings are invalid.
- The response is one ASCII message: `OK` when the request was accepted or
  `INVALID` when its format was rejected.
- GCLoader disconnects that pipe instance after sending the response. The
  adapter opens a new connection for the next scan.
- An adapter must treat only `OK` as an accepted scan and must check that its
  complete request and response were transferred.

A valid example request is:

```text
1234567890123456
```

The displayed newline is not part of the request message.

This narrow framing makes every successful request one explicit scan,
including consecutive scans of the same card number.

## Binary-Backed Identifier Conditions

The card path was checked in the current `H:\gc\game471.exe.i64` database
against `game471.exe` with SHA-256
`FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`.
The image base is `0x400000`.

The game imposes no local digit, prefix, checksum, or numeric-range condition
on the 16-byte card identifier:

- `sub_4B3BE0` at RVA `0xB3BE0` handles the NESiCA reader response and copies
  eight prefix bytes followed by sixteen identifier bytes without inspecting
  their contents.
- `sub_634DD0` at RVA `0x234DD0` exposes exactly those sixteen identifier
  bytes. It has two callers in this build.
- The gameplay caller `sub_5A4A20` at RVA `0x1A4A20` passes the bytes through
  `sub_6284D0` at RVA `0x2284D0`; neither function validates the identifier's
  characters or calculates a checksum.
- `sub_59E6F0` at RVA `0x19E6F0` copies the sixteen bytes into a zero-terminated
  NESYS card-ID field without further validation.
- The other caller, the test-mode display path `sub_569CA0` at RVA `0x169CA0`,
  only checks that the resulting displayed string has length sixteen.

Therefore sixteen decimal digits are sufficient for the game, and there is no
additional game-side check-digit rule. A NESYS server may still impose its own
semantic policy, which is outside this interface. GCLoader deliberately keeps
the existing digits-only contract because it is the established `card.txt`
format and leaves any physical-reader UID mapping to the external adapter.

## Card-Scan Behavior

A valid pipe request assembles the existing 24-byte RFID payload: the fixed
prefix `04 C2 3D DA 6F 52 80 00` followed by the submitted 16 ASCII digits.
It stores that payload as the single pending scan and arms the same card-present
state used by the current keyboard path.

The two trigger sources behave as follows:

- A pipe request supplies its own payload and does not read or rewrite
  `card.txt`.
- A keyboard edge arms a scan without a supplied payload, so the JVS transfer
  continues to load `card.txt` at consumption time.
- The most recent valid trigger before JVS consumption wins. There is one
  pending slot and no queue.
- An invalid or incomplete pipe request returns `INVALID` and leaves the
  current pending scan unchanged.
- A valid request returns `OK` only after its payload has been published and
  the scan has been armed.
- The JVS device clears the scan only after successfully building the card
  response. A generation check prevents an older in-progress transfer from
  clearing a newer trigger that arrived concurrently.

The existing built-in fallback number remains limited to malformed or
unavailable `card.txt` input on keyboard-triggered scans. Invalid pipe input
does not synthesize a default-card scan.

## Source Architecture

### Card data

`src/Rfid/CardData.h/.cpp` gains a focused parser that converts an exact
16-digit view into `CardData` or reports invalid input. The file loader reuses
the same digit validation after its existing ASCII-whitespace trimming. This
is the shared production seam for file and pipe input; payload assembly and
the fixed prefix remain owned by the card-data unit.

The unit also exposes the built-in number once as
`kDefaultCardNumber = "7020392010281502"`. The default payload and runtime
test client both derive from that constant so the test tool cannot drift from
the loader's code default.

### Pending scan state

`CardScanState` owns the single pending scan under synchronization. A snapshot
contains presence, an optional externally supplied payload, and a generation.
Keyboard arming publishes no payload; pipe arming publishes its parsed payload.
Generation-aware consumption preserves a newer trigger if it races with an
older JVS response.

### Named-pipe listener

A focused RFID card-reader-interface unit owns the public pipe name, request
framing, response framing, and one-connection handling. Its production loop
creates one local pipe instance, accepts one request, submits valid data to
`CardScanState`, responds, disconnects, and listens again.

The listener uses Win32 wide-character APIs and fixed bounded buffers. It does
not touch the filesystem, configuration, input polling, or reader hardware.
Successful requests do not add per-scan diagnostic logging.

### Runtime integration

`Runtime::OpenCom2()` starts the listener once, alongside the existing
process-lifetime keyboard worker and outside `DllMain`. The listener has its
own worker because blocking pipe I/O must not delay keyboard polling or JVS
traffic.

Failure to start the optional listener is logged but does not fail COM2 open
or disable the established keyboard and `card.txt` behavior. A listener that
encounters a transient pipe-creation failure waits before retrying so it cannot
spin. Exceptions and Win32 failures remain contained inside the worker
boundary.

## Runtime Test Client

Implementation adds `CardReaderTestClient.exe` under a focused
`tools/CardReaderTestClient` target. CMake places the executable directly in
`${GC_DIST_DIR}` with the other operator-facing artifacts.

The client is a native Win32 GUI executable linked only to the Windows APIs it
uses. It does not reuse ConfigGUI's ImGui or Direct3D host and does not read
configuration or `card.txt`.

Its fixed window contains:

- a label showing `Test card: 7020392010281502`, sourced from
  `kDefaultCardNumber`;
- one `Send Test Card` button; and
- one status label initially showing `Not sent`.

Clicking the button connects to `\\.\pipe\GCLoader.CardReader`, sends the
sixteen default-number bytes as one message, reads the server response, and
updates the status label. `OK`, `INVALID`, unavailable/busy pipe, short I/O,
and other Win32 failures are visibly distinguishable. The client sends no
keyboard input and does not modify files, so an accepted in-game scan proves
that the external interface armed the RFID state.

The operation may run synchronously on the button click because this is a
single-purpose manual test tool with a short connection attempt, not a
long-running reader adapter. It does not retry automatically; the displayed
error lets the operator click again after the game has opened COM2.

## Error and Concurrency Rules

- Empty, short, long, non-decimal, truncated, and oversized requests return
  `INVALID` without arming a scan.
- A client disconnect before a complete request changes no state.
- Only one request is processed per connection. Additional messages on that
  connection are outside the contract.
- The server handles clients serially. A client that observes a busy pipe may
  wait or retry; GCLoader does not create a request queue of its own.
- Repeated identical valid requests are independent scans.
- If valid keyboard and pipe triggers overlap, last publication wins.
- Thread-start failure is permanent for that process and is reported once.
  Pipe-instance creation failures are retried with a bounded delay.
- No exception may escape the listener entry point, RFID runtime methods, JVS
  handling, or DLL boundary.

## Public Documentation

Implementation adds `docs/card-reader-interface.md` as the adapter-facing
reference. It records the stable pipe name, exact request and response bytes,
availability and retry behavior, last-trigger-wins semantics, and a minimal
client example. It also identifies `CardReaderTestClient.exe` as a manual
contract probe, not an adapter template. The document does not prescribe any
physical reader logic.

## Testing

Focused behavioral coverage will establish:

- exact 16-digit parsing succeeds and malformed pipe payloads fail;
- the existing file loader retains whitespace trimming and fallback behavior
  while sharing digit validation;
- pipe submission publishes the supplied card payload and invalid submission
  preserves any pending scan;
- repeated identical cards and last-trigger-wins replacement behave as
  specified;
- generation-aware consumption cannot clear a newer concurrent publication;
- keyboard-triggered JVS transfers still read `card.txt`, while pipe-triggered
  transfers use the submitted number exactly once;
- a real uniquely named test pipe accepts the request, returns `OK`, rejects
  malformed input with `INVALID`, and permits a later connection;
- listener-start failure does not prevent the emulated COM2 device from
  opening;
- the client-side transport reports `OK`, `INVALID`, unavailable pipe, and
  short or unexpected responses distinctly; and
- the test client builds as a native GUI target in `${GC_DIST_DIR}` and uses
  the shared built-in default number.

The affected targets and full CTest suite will run under both x86 Debug and
Release presets. Automated results establish the protocol and static/runtime
unit behavior only; actual third-party reader and in-game acceptance remain
manual runtime checks.

## Runtime Acceptance

After static verification, the operator will run the game and the generated
`dist/CardReaderTestClient.exe`. Once the game has opened the emulated COM2
device, the operator clicks `Send Test Card` without pressing the configured
card-read key.

Acceptance requires both observations:

1. The client displays `OK`, proving the game-process pipe accepted and armed
   the built-in default number.
2. The game proceeds through its normal card-read flow exactly once, proving
   the external trigger reached the existing one-shot JVS path.

This runtime result validates the bundled probe and the named-pipe integration.
It does not establish compatibility with a specific third-party physical
reader until that reader's adapter uses the same contract successfully.
