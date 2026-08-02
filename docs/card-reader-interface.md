# External Card Reader Interface

GCLoader exposes a small local interface for card-reader adapters. The
adapter owns all physical-reader discovery, communication, UID mapping, and
retry policy. Once it has the final GCLoader card number, it submits that
number as one scan.

GCLoader does not load reader plugins or communicate with reader hardware.

## Endpoint and Availability

The fixed Windows named pipe is:

```text
\\.\pipe\GCLoader.CardReader
```

It is a local-only, duplex, message-mode pipe. Remote clients are rejected.
There is one server instance and no request queue.

The pipe's Windows security descriptor grants read/write access to
authenticated local users and uses a low mandatory-integrity label. A normal
unelevated client can therefore connect when the game runs as administrator.
No application-level authentication is performed: any authenticated process
on the local machine can submit a scan. `PIPE_REJECT_REMOTE_CLIENTS` still
prevents access from another machine.

The listener starts after the game first opens GCLoader's emulated COM2 RFID
device. `ERROR_FILE_NOT_FOUND` means the listener is not available yet.
`ERROR_PIPE_BUSY` means its single instance is occupied. An adapter may wait
and retry by opening a new connection; each connection still carries only one
request. The local GUI test client does not retry automatically.

`ERROR_ACCESS_DENIED` is not an elevation requirement in GCLoader's contract.
If it occurs with the current build, check local Windows security policy and
confirm that the client is authenticated locally rather than attempting a
remote pipe connection.

## Request

One connection submits one message containing exactly 16 bytes:

- Every byte must be ASCII `0` through `9`.
- Do not include whitespace, a newline, a byte-order mark, separators, a NUL
  terminator, or bytes from another encoding.
- Send all 16 bytes with one `WriteFile` call and verify that the returned
  byte count is 16.

Example request bytes:

```text
1234567890123456
```

The displayed line ending is not part of the message.

## Response

GCLoader replies with one exact ASCII message:

| Bytes | Meaning |
|---|---|
| `OK` | The supplied payload was published and the one-shot scan was armed. |
| `INVALID` | The request framing or card-number format was rejected. No pending scan was changed. |

Verify both the `ReadFile` result and complete byte count. Only exact `OK`
means accepted; a short, terminated, extended, or unknown response does not.
GCLoader disconnects after the response. Open a new connection for every
later card, including a repeated scan of the same number.

## Pending-Scan Semantics

GCLoader has one pending RFID scan slot, not a queue:

- A valid pipe request stores its submitted number and arms a scan without
  reading or changing `card.txt`.
- The existing keyboard trigger remains available. It arms a scan that loads
  `card.txt` when the game consumes the JVS card payload.
- The newest valid keyboard or pipe trigger before consumption wins.
- Invalid or incomplete pipe input leaves the current pending scan unchanged.
- Repeated identical valid requests are independent scans.
- A completed game-side card payload consumes the matching pending generation
  once. A newer concurrent trigger remains pending.

GCLoader accepts 16 decimal digits without applying a checksum or interpreting
the identifier. A NESYS server may impose separate semantic policy.

## Minimal C++ Client

This example performs one complete transaction. It intentionally has no
physical-reader logic and treats only exact `OK` as acceptance.

```cpp
#include <Windows.h>

#include <array>
#include <string_view>

enum class SubmitResult {
    accepted,
    invalid,
    unavailable,
    failed,
};

SubmitResult SubmitCard(std::string_view card_number)
{
    constexpr wchar_t pipe_name[] =
        LR"(\\.\pipe\GCLoader.CardReader)";

    const HANDLE pipe = CreateFileW(
        pipe_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ||
                error == ERROR_PIPE_BUSY
            ? SubmitResult::unavailable
            : SubmitResult::failed;
    }

    SubmitResult result = SubmitResult::failed;
    do {
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(
                pipe, &mode, nullptr, nullptr)) {
            break;
        }

        DWORD bytes_written = 0;
        if (!WriteFile(
                pipe,
                card_number.data(),
                static_cast<DWORD>(card_number.size()),
                &bytes_written,
                nullptr) ||
            bytes_written != card_number.size()) {
            break;
        }

        std::array<char, 8> response{};
        DWORD bytes_read = 0;
        if (!ReadFile(
                pipe,
                response.data(),
                static_cast<DWORD>(response.size()),
                &bytes_read,
                nullptr)) {
            break;
        }

        const std::string_view message{
            response.data(), bytes_read};
        if (message == "OK") {
            result = SubmitResult::accepted;
        } else if (message == "INVALID") {
            result = SubmitResult::invalid;
        }
    } while (false);

    CloseHandle(pipe);
    return result;
}
```

Callers must ensure `card_number` contains exactly 16 ASCII digits before
calling this example. Do not pass a C-string terminator in the write length.

## Minimal Python Client

The complete runnable [Python example](../tools/CardReaderTestClient/send_card.py)
uses only Python's standard-library `ctypes` module; it does not require
`pywin32` or administrator elevation. From the source repository, submit one
card with:

```powershell
python tools/CardReaderTestClient/send_card.py 1234567890123456
```

The script writes `OK` and exits with status `0` only when the request is
accepted. A server `INVALID` response exits with status `2`. Local validation,
transport, short-I/O, and unexpected-response failures are written to stderr
and exit with status `1`. It performs no retry and sends no trailing newline or
NUL byte in the pipe message.

## Local Runtime Probe

`CardReaderTestClient.exe` is a repository-local manual contract probe, not an
adapter template, card editor, or distribution artifact. It is emitted in the
target's build-tree directory:

```text
build-msvc32-debug/tools/CardReaderTestClient/CardReaderTestClient.exe
build-msvc32-release/tools/CardReaderTestClient/CardReaderTestClient.exe
```

The fixed window shows GCLoader's built-in test card
`7020392010281502`, a `Send Test Card` button, and a status label. After the
game has opened emulated COM2:

1. Run the client unelevated and without pressing the configured `card_read`
   key.
2. Click `Send Test Card` once.
3. Require the status to become `OK`.
4. Require the game to enter its normal card-read flow exactly once.

`OK` proves that the game-process pipe accepted and armed the request. The
game observation separately proves that its JVS path consumed the pending
scan. Static builds and automated tests do not replace this in-game check.
