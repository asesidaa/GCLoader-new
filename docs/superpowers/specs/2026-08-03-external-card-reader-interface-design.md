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

## Non-Goals

- Detecting, opening, configuring, or polling physical card readers.
- Shipping adapters for specific readers or other games.
- Loading third-party reader plugins into the game process.
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
client example. The document does not prescribe any physical reader logic.

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
  malformed input with `INVALID`, and permits a later connection; and
- listener-start failure does not prevent the emulated COM2 device from
  opening.

The affected targets and full CTest suite will run under both x86 Debug and
Release presets. Automated results establish the protocol and static/runtime
unit behavior only; actual third-party reader and in-game acceptance remain
manual runtime checks.
