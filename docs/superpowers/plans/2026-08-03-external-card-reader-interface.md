# External Card Reader Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let any local adapter process submit one exact 16-digit card number to GCLoader, arm the existing one-shot RFID/JVS flow, and ship a one-button Win32 client for live game testing.

**Architecture:** A local-only message-mode named-pipe server parses each connection as one request and publishes a supplied `CardData` payload into a synchronized, generation-aware pending scan slot. Keyboard scans continue to publish no payload and therefore load `card.txt` at JVS consumption time. `Runtime::OpenCom2()` starts the blocking pipe listener on its own optional process-lifetime worker. A small native GUI uses a separately testable client transport and the shared built-in card-number constant.

**Tech Stack:** C++23, Win32 named pipes and GUI APIs, SRW locks, `std::expected`, CMake/Ninja, CTest, MSVC x86 Debug and RelWithDebInfo.

**Approved contract:** `docs/superpowers/specs/2026-08-03-external-card-reader-interface-design.md`

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`; do not deploy to or mutate the runtime tree at `H:\gc`.
- Preserve the unrelated working-tree edits currently present in `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`, `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`, `src/Rfid/Feature.cpp`, and `tests/Patches/RendererDeviceLossPatchTests.cpp`. Do not stage them in this work.
- Keep the endpoint fixed at `\\.\pipe\GCLoader.CardReader`; do not add configuration or ConfigGUI controls.
- Accept exactly one 16-byte ASCII-decimal message per connection. Respond with exactly `OK` or `INVALID`, then disconnect.
- Keep the pipe local-only, duplex, and message-mode, with a single server instance and no request queue.
- A valid pipe request supplies its own payload. A keyboard trigger supplies no payload and still loads `card.txt` when JVS command `0x32` is consumed.
- Keep one pending scan slot: the newest valid trigger wins. Invalid or incomplete pipe input must leave the current pending scan unchanged.
- Use generation-aware consumption so an older JVS transfer cannot clear a newer trigger.
- Start the pipe listener only after the game first opens emulated COM2, outside `DllMain`, on a worker independent from keyboard polling.
- Listener startup or pipe-creation failure must not disable the existing keyboard/`card.txt` path. Log infrastructure failures without logging every successful scan or spinning on repeated failures.
- Keep the test client native and minimal: fixed default card, one button, one status label, no ImGui, Direct3D, configuration, file writes, keyboard injection, or automatic retry.
- Add only behavioral tests with independent outcomes. Do not add source-text, regex, or CMake-grep tests.
- Run all CMake, build, CTest, and `dumpbin` commands from an x86 MSVC developer environment initialized by `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat`.
- Treat builds and CTest as static evidence. Do not claim in-game acceptance until the user runs the generated client and observes the game consume the scan.
- If final verification exposes a defect, return to the task that owns that behavior, add or strengthen its regression test, make an atomic fix commit, and rerun the failed slice before continuing.

---

### Task 1: Expose one exact card-number parser and shared default

**Files:**
- Modify: `src/Rfid/CardData.h:3-18`
- Modify: `src/Rfid/CardData.cpp:26-64`
- Modify: `tests/Rfid/JvsDeviceTests.cpp:438-497`

**Interfaces:**
- Produces: `gc::rfid::kDefaultCardNumber` and `gc::rfid::ParseCardNumber(std::string_view)`.
- Preserves: `LoadCardData(path)` ASCII-whitespace trimming and default fallback behavior.

- [ ] **Step 1: Add failing exact-parser tests**

Extend `test_card_data_file_loading()` with direct parser cases before the filesystem cases:

```cpp
const auto parsed = ParseCardNumber("1234567890123456");
failures += expect(
    parsed && *parsed == ExpectedCardData("1234567890123456"),
    "exact decimal card number parses");

for (const std::string_view invalid : {
         "",
         "123456789012345",
         "12345678901234567",
         "123456789012345X",
         " 1234567890123456",
         "1234567890123456\n",
     }) {
    failures += expect(
        !ParseCardNumber(invalid),
        "exact parser rejects malformed card number");
}

failures += expect(
    kDefaultCardNumber == "7020392010281502" &&
        ParseCardNumber(kDefaultCardNumber) ==
            std::optional<CardData>{kDefaultCardData},
    "default number and payload stay aligned");
```

Retain the existing file test that accepts surrounding whitespace. Together these cases prove that only the file loader trims, while the public pipe parser stays exact.

- [ ] **Step 2: Build the focused target and verify RED**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target JvsDeviceTests
```

Expected: compilation fails because `ParseCardNumber` and `kDefaultCardNumber` do not exist.

- [ ] **Step 3: Implement the shared constant and parser**

In `CardData.h`, add the exact public seam:

```cpp
#include <cstddef>
#include <optional>
#include <string_view>

inline constexpr std::string_view kDefaultCardNumber{
    "7020392010281502"};
static_assert(kDefaultCardNumber.size() == 16);

inline constexpr CardData kDefaultCardData = [] {
    CardData result{
        0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00};
    for (std::size_t index = 0;
         index < kDefaultCardNumber.size();
         ++index) {
        result[8 + index] = static_cast<std::uint8_t>(
            kDefaultCardNumber[index]);
    }
    return result;
}();

[[nodiscard]] std::optional<CardData> ParseCardNumber(
    std::string_view number) noexcept;
```

Move the current size/digit validation and payload assembly behind `ParseCardNumber` in `CardData.cpp`:

```cpp
std::optional<CardData> ParseCardNumber(
    std::string_view number) noexcept
{
    if (number.size() != 16 ||
        !std::ranges::all_of(number, [](char value) {
            return value >= '0' && value <= '9';
        })) {
        return std::nullopt;
    }
    return AssembleCardData(number);
}
```

After reading and trimming the file, use the parser once:

```cpp
if (const auto parsed = ParseCardNumber(
        TrimAsciiWhitespace(contents))) {
    return *parsed;
}
return kDefaultCardData;
```

Keep `std::filesystem::path` end-to-end and preserve the existing catch-all fallback.

- [ ] **Step 4: Run parser and file-loader coverage to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target JvsDeviceTests
ctest --preset msvc32-debug -R "^JvsDeviceTests$"
```

Expected: exact parsing, malformed rejection, file trimming/fallback, Unicode current-directory reload, and the existing JVS cases all pass.

- [ ] **Step 5: Commit the card-data seam**

```powershell
git add -- src/Rfid/CardData.h src/Rfid/CardData.cpp tests/Rfid/JvsDeviceTests.cpp
git commit -m "Expose exact RFID card number parser"
```

### Task 2: Carry supplied payloads through the pending scan state

**Files:**
- Modify: `src/Rfid/State.h:1-34`
- Modify: `src/Rfid/State.cpp:1-11`
- Modify: `src/Rfid/Jvs/Device.cpp:384-429`
- Modify: `tests/Rfid/RfidRuntimeTests.cpp:123-169`
- Modify: `tests/Rfid/JvsDeviceTests.cpp:373-435`

**Interfaces:**
- Produces: `CardScanSnapshot`, `CardScanState::Arm(CardData)`, `Snapshot()`, and `Consume(generation)`.
- Preserves: no-argument `Arm()` for the keyboard path and `IsPresent()` for JVS status polling.

- [ ] **Step 1: Add failing pending-slot and generation tests**

Replace the simple `Consume()` assertions in `RfidRuntimeTests.cpp` with behavior that distinguishes manual and supplied scans:

```cpp
const auto supplied = *gc::rfid::ParseCardNumber(
    "1111222233334444");
const auto replacement = *gc::rfid::ParseCardNumber(
    "9999888877776666");

state.Arm(supplied);
const auto first = state.Snapshot();
failures += expect(
    first.present && first.card_data == supplied,
    true,
    "external arm publishes supplied payload");

state.Arm();
const auto manual = state.Snapshot();
failures += expect(
    manual.present && !manual.card_data &&
        manual.generation != first.generation,
    true,
    "newer manual trigger replaces supplied payload");

state.Arm(supplied);
const auto stale = state.Snapshot();
state.Arm(replacement);
const auto newest = state.Snapshot();
failures += expect(
    !state.Consume(stale.generation) &&
        state.Snapshot().generation == newest.generation &&
        state.Snapshot().card_data == replacement,
    true,
    "stale consume preserves newer trigger");
failures += expect(
    state.Consume(newest.generation) && !state.IsPresent(),
    true,
    "matching generation consumes exactly once");
```

Keep the reset test and assert that `ResetBus()` preserves the complete pending snapshot, not only presence.

In `test_card_output_and_overflow()`, add a supplied-card case after a different value has been written to `card.txt`:

```cpp
WriteText(L"card.txt", "0000000000000000\n");
state.card_scan.Arm(
    *ParseCardNumber("2468135790246813"));

std::vector<std::uint8_t> expected_external{0x01, 0x01};
const auto external_card = ExpectedCardData("2468135790246813");
expected_external.insert(
    expected_external.end(),
    external_card.begin(),
    external_card.end());
expected_external.push_back(0x01);
failures += expect_acknowledgement(
    device.HandlePacket(packet(
        Address{0x01}, {0x32, 0x01, 0x00})),
    expected_external,
    "supplied card payload bypasses card file");
failures += expect(
    !state.card_scan.IsPresent(),
    "supplied card payload consumes once");
```

The existing two keyboard cases remain the regression proof that `card.txt` is still reloaded at consumption time.

- [ ] **Step 2: Build the two focused targets and verify RED**

```powershell
cmake --build --preset msvc32-debug --target RfidRuntimeTests JvsDeviceTests
```

Expected: compilation fails because snapshots, payload arming, and generation-aware consumption are not implemented.

- [ ] **Step 3: Implement a synchronized single pending slot**

In `State.h`, include `Rfid/CardData.h` and `Windows.h`, remove the atomic Boolean, and declare:

```cpp
struct CardScanSnapshot {
    bool present{};
    std::optional<CardData> card_data;
    std::uint64_t generation{};
};

class CardScanState {
public:
    CardScanState() noexcept = default;
    CardScanState(const CardScanState&) = delete;
    CardScanState& operator=(const CardScanState&) = delete;

    void Arm() noexcept;
    void Arm(CardData card_data) noexcept;
    [[nodiscard]] CardScanSnapshot Snapshot() const noexcept;
    [[nodiscard]] bool IsPresent() const noexcept;
    [[nodiscard]] bool Consume(std::uint64_t generation) noexcept;

private:
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    bool present_{};
    std::optional<CardData> card_data_;
    std::uint64_t generation_{};
};
```

Implement the methods in `State.cpp` with `AcquireSRWLockExclusive`/`ReleaseSRWLockExclusive` for both `Arm` overloads and `Consume`, and shared locking for `Snapshot`. Each `Arm` increments `generation_`, sets `present_ = true`, and either resets or replaces `card_data_`. `Consume` clears presence and payload only when `present_` is true and the supplied generation matches. Implement `IsPresent()` from one snapshot.

Do not hold the lock during file I/O or JVS response assembly.

- [ ] **Step 4: Use one immutable snapshot in JVS command `0x32`**

Replace the separate `IsPresent()`/`Consume()` sequence with:

```cpp
const auto scan = state_.card_scan.Snapshot();
if (scan.present) {
    const auto card_data = scan.card_data
        ? *scan.card_data
        : LoadCurrentDirectoryCardData();
    if (!AppendOrOverflow(writer, card_data)) {
        return DeviceResponse{acknowledgement};
    }
} else {
    // Retain the existing zero-response loop unchanged.
}

if (!append_report(report::ok)) {
    return DeviceResponse{acknowledgement};
}
if (scan.present) {
    static_cast<void>(
        state_.card_scan.Consume(scan.generation));
}
```

The final report must be appended before consumption. If response assembly overflows, leave the snapshot pending. If another trigger arrives during assembly, the stale generation fails to consume it.

- [ ] **Step 5: Run state and JVS tests to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target RfidRuntimeTests JvsDeviceTests ComPortStateTests
ctest --preset msvc32-debug -R "^(RfidRuntimeTests|JvsDeviceTests|ComPortStateTests)$"
```

Expected: manual reload, supplied payload, last-trigger-wins, stale-consume protection, reset preservation, one-shot consumption, and existing COM behavior pass.

- [ ] **Step 6: Commit pending-scan support**

```powershell
git add -- src/Rfid/State.h src/Rfid/State.cpp src/Rfid/Jvs/Device.cpp tests/Rfid/RfidRuntimeTests.cpp tests/Rfid/JvsDeviceTests.cpp
git commit -m "Carry submitted RFID card payloads"
```

### Task 3: Implement the local named-pipe server contract

**Files:**
- Create: `src/Rfid/CardReaderProtocol.h`
- Create: `src/Rfid/CardReaderInterface.h`
- Create: `src/Rfid/CardReaderInterface.cpp`
- Modify: `src/Rfid/CMakeLists.txt:1-14`
- Create: `tests/Rfid/CardReaderInterfaceTests.cpp`
- Modify: `tests/Rfid/CMakeLists.txt:1-14`

**Interfaces:**
- Produces: fixed pipe/request/response constants and `ServeOneCardReaderConnection(pipe_name, card_scan)`.
- Consumes: `ParseCardNumber` and the payload-aware `CardScanState` from Tasks 1 and 2.

- [ ] **Step 1: Add a real-pipe behavioral test target**

Add `CardReaderInterfaceTests` to the existing `gc_rfid_core` test loop. Build a raw Win32 test client in the test file; do not use the production test-client transport from Task 5, because the server tests need an independent peer.

Use a unique pipe name per test process:

```cpp
std::wstring UniquePipeName(std::wstring_view suffix)
{
    return L"\\\\.\\pipe\\GCLoader.CardReader.Tests." +
        std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetTickCount64()) + L"." +
        std::wstring{suffix};
}
```

The test helper must start exactly one `ServeOneCardReaderConnection` call on a `std::jthread`, retry `CreateFileW` only while the unique server is being created, select `PIPE_READMODE_MESSAGE`, write one message with one `WriteFile`, read the complete response, join the server, and close every handle.

Cover this matrix with fresh server instances:

| Request/event | Response/outcome | State assertion |
|---|---|---|
| `1234567890123456` | `OK` / accepted | matching supplied payload is pending |
| same valid card after consuming the first generation | `OK` / accepted | a new generation is pending |
| 15 bytes | `INVALID` / invalid | pre-existing pending generation and payload unchanged |
| 17 bytes | `INVALID` / invalid | unchanged |
| 32-byte message | `INVALID` / invalid | unchanged even when `ReadFile` reports `ERROR_MORE_DATA` |
| 16 bytes containing `X` | `INVALID` / invalid | unchanged |
| client connects and closes without a message | disconnected | unchanged and no response required |
| invalid connection followed by a valid connection on the same pipe name | `INVALID`, then `OK` | second server instance accepts normally |

These tests catch byte-mode framing, accidental trimming/defaulting, state clearing on rejection, pipe instances that cannot be recreated, and oversized-message truncation being accepted as valid.

- [ ] **Step 2: Configure and build the test target to verify RED**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target CardReaderInterfaceTests
```

Expected: compilation fails because the protocol and server headers do not exist.

- [ ] **Step 3: Define the fixed protocol and server result**

Put the shared wire constants in `CardReaderProtocol.h`:

```cpp
namespace gc::rfid::card_reader {

inline constexpr wchar_t kPipeName[] =
    LR"(\\.\pipe\GCLoader.CardReader)";
inline constexpr std::size_t kRequestByteCount = 16;
inline constexpr std::string_view kAcceptedResponse{"OK"};
inline constexpr std::string_view kInvalidResponse{"INVALID"};

} // namespace gc::rfid::card_reader
```

Declare the server seam in `CardReaderInterface.h`:

```cpp
enum class CardReaderConnectionOutcome {
    accepted,
    invalid,
    client_disconnected,
};

[[nodiscard]] std::expected<
    CardReaderConnectionOutcome,
    DWORD>
ServeOneCardReaderConnection(
    const wchar_t* pipe_name,
    CardScanState& card_scan) noexcept;
```

A null or empty `pipe_name` returns `std::unexpected(ERROR_INVALID_PARAMETER)`.

- [ ] **Step 4: Implement one bounded synchronous connection**

Implement `CardReaderInterface.cpp` with a file-local handle owner that calls `DisconnectNamedPipe` only for a connected instance and always calls `CloseHandle`.

Use this exact server setup:

```cpp
CreateNamedPipeW(
    pipe_name,
    PIPE_ACCESS_DUPLEX,
    PIPE_TYPE_MESSAGE |
        PIPE_READMODE_MESSAGE |
        PIPE_WAIT |
        PIPE_REJECT_REMOTE_CLIENTS,
    1,
    16,
    17,
    0,
    nullptr);
```

Then implement this lifecycle:

1. Accept `ConnectNamedPipe == TRUE` or `FALSE` with `ERROR_PIPE_CONNECTED`.
2. Treat `ERROR_BROKEN_PIPE`, `ERROR_NO_DATA`, and `ERROR_PIPE_NOT_CONNECTED` before a complete message as `client_disconnected`, not an infrastructure failure.
3. Read into a fixed 17-byte buffer. Only `ReadFile == TRUE`, `bytes_read == 16`, and a successful `ParseCardNumber` are valid. A successful shorter/longer read or `ERROR_MORE_DATA` is invalid.
4. On valid input, call `card_scan.Arm(*parsed)` before writing `OK`. On invalid input, do not mutate state and write `INVALID`.
5. Require `WriteFile == TRUE` and the exact response byte count. Call `FlushFileBuffers` before disconnecting so the waiting client receives the complete message.
6. Return the accepted/invalid outcome only after the response transfer succeeds. Return unexpected Win32 errors as `std::unexpected(error)`.
7. Catch every exception inside this `noexcept` boundary and translate allocation failure to `ERROR_NOT_ENOUGH_MEMORY` and other exceptions to `ERROR_GEN_FAILURE`.

Do not trim, append terminators, read files, retry, log successful requests, or process a second message on the connection.

Add `CardReaderInterface.cpp` to `gc_rfid_core`.

- [ ] **Step 5: Run the real named-pipe cases to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target CardReaderInterfaceTests
ctest --preset msvc32-debug -R "^CardReaderInterfaceTests$"
```

Expected: the real local message pipe returns exact response bytes, rejects every malformed boundary, preserves pending state on rejection/disconnect, and can be recreated for the next connection.

- [ ] **Step 6: Commit the pipe server**

```powershell
git add -- src/Rfid/CardReaderProtocol.h src/Rfid/CardReaderInterface.h src/Rfid/CardReaderInterface.cpp src/Rfid/CMakeLists.txt tests/Rfid/CardReaderInterfaceTests.cpp tests/Rfid/CMakeLists.txt
git commit -m "Accept RFID cards over a local named pipe"
```

### Task 4: Start the listener from the RFID runtime without weakening COM2

**Files:**
- Modify: `src/Rfid/Runtime.h:13-46`
- Modify: `src/Rfid/Runtime.cpp:1-111`
- Modify: `tests/Rfid/RfidRuntimeTests.cpp:8-121`
- Modify: `tests/Win32Hooks/Kernel32HookTests.cpp:695-729, 1025-1050, 1159-1178`

**Interfaces:**
- Consumes: `ServeOneCardReaderConnection` and `card_reader::kPipeName` from Task 3.
- Preserves: keyboard-worker startup as a required condition for `OpenCom2()`.
- Produces: an independent optional listener worker started once per `Runtime`.

- [ ] **Step 1: Make the worker fake observe two independent launches**

Change the fake to record several launch attempts and optionally fail one numbered call:

```cpp
struct WorkerLaunch {
    gc::rfid::CardWorkerEntry entry{};
    void* context{};
};

struct FakeCardWorker {
    int start_calls{};
    int fail_on_call{};
    DWORD error{ERROR_NOT_ENOUGH_MEMORY};
    std::array<WorkerLaunch, 4> launches{};
};
```

Update `StartFakeWorker` so call 1 represents the required keyboard worker and call 2 represents the optional listener. Add these cases:

- two successful `OpenCom2()` calls launch exactly two distinct worker entries total;
- close/reopen still leaves the total at two;
- failure on call 1 is returned permanently, leaves the port closed, and never attempts call 2;
- failure on call 2 still returns the stable emulated handle, opens the port, and is not retried on reopen; and
- `ProductionCardWorkerApi()` remains complete.

Update the two successful COM2-routing assertions in `Kernel32HookTests.cpp` from one worker start to two. Retain the failure assertion at one start: a required keyboard-worker failure must return before the listener is attempted. The existing hook tests then independently prove that adding the optional worker does not change COM2 interception, returned handles, or last-error behavior.

- [ ] **Step 2: Run the runtime test and verify RED**

```powershell
cmake --build --preset msvc32-debug --target RfidRuntimeTests
ctest --preset msvc32-debug -R "^RfidRuntimeTests$"
```

Expected: the successful runtime records only the current keyboard worker, and the listener-failure behavior does not exist.

- [ ] **Step 3: Add the independent listener lifecycle**

Add a second `std::once_flag`, entry point, and loop in `Runtime`:

```cpp
static void CardReaderWorkerMain(void* context) noexcept;
void RunCardReaderWorker();

std::once_flag card_worker_once_;
std::once_flag card_reader_worker_once_;
```

Retain the existing started/error state for the required keyboard worker. In `OpenCom2()`:

1. Start/check the keyboard worker exactly as today; return its permanent error before attempting the listener.
2. Call `std::call_once(card_reader_worker_once_, ...)` to start `CardReaderWorkerMain` with the same detached-worker API.
3. If listener thread creation fails, emit one `PLOG_WARNING` containing the Win32 error and continue.
4. Open the port and return `EmulatedComHandle()` regardless of listener-start success.

The listener entry point must catch all exceptions so none escape the detached worker boundary.

- [ ] **Step 4: Implement the serial listener loop and bounded error retry**

Use the production loop:

```cpp
bool infrastructure_error_logged = false;
for (;;) {
    const auto served = ServeOneCardReaderConnection(
        card_reader::kPipeName,
        port_.device_state().card_scan);
    if (served) {
        infrastructure_error_logged = false;
        continue;
    }

    if (!infrastructure_error_logged) {
        PLOG_WARNING
            << "RFID card-reader pipe unavailable error="
            << served.error();
        infrastructure_error_logged = true;
    }
    worker_api_.sleep_for(std::chrono::seconds{1});
}
```

`client_disconnected` is a successful connection outcome and therefore neither logs nor sleeps. Any infrastructure failure sleeps one second; a continuous error run logs only its first failure, and a later successful connection resets suppression. Do not add a successful-scan log.

- [ ] **Step 5: Run runtime, pipe, JVS, and hook-routing regressions**

```powershell
cmake --build --preset msvc32-debug --target RfidRuntimeTests CardReaderInterfaceTests JvsDeviceTests Kernel32HookTests iDmacDrv32
ctest --preset msvc32-debug -R "^(RfidRuntimeTests|CardReaderInterfaceTests|JvsDeviceTests|Kernel32HookTests)$"
```

Expected: both workers have correct once-only semantics, optional failure does not fail COM2, the DLL links, and existing COM2 routing/JVS behavior remains green.

- [ ] **Step 6: Commit runtime integration**

```powershell
git add -- src/Rfid/Runtime.h src/Rfid/Runtime.cpp tests/Rfid/RfidRuntimeTests.cpp tests/Win32Hooks/Kernel32HookTests.cpp
git commit -m "Start the external RFID listener with COM2"
```

### Task 5: Build a testable client-side pipe transport

**Files:**
- Create: `tools/CardReaderTestClient/CMakeLists.txt`
- Create: `tools/CardReaderTestClient/CardReaderClient.h`
- Create: `tools/CardReaderTestClient/CardReaderClient.cpp`
- Modify: `tools/CMakeLists.txt:1`
- Create: `tests/Rfid/CardReaderClientTests.cpp`
- Modify: `tests/Rfid/CMakeLists.txt:1-14`

**Interfaces:**
- Produces: `SendCardNumber(pipe_name, number)` and `FormatStatus(result)` for the GUI in Task 6.
- Consumes: only the public protocol constants and Win32 pipe APIs; it does not link ConfigGUI or mutate files.

- [ ] **Step 1: Define the client result contract and failing tests**

Declare this focused tool API in `CardReaderClient.h`:

```cpp
namespace gc::rfid::card_reader_test_client {

enum class SendStatus {
    accepted,
    invalid,
    pipe_unavailable,
    pipe_busy,
    short_write,
    short_response,
    unexpected_response,
    win32_error,
};

struct SendResult {
    SendStatus status{SendStatus::win32_error};
    DWORD win32_error{ERROR_SUCCESS};
    constexpr bool operator==(const SendResult&) const = default;
};

[[nodiscard]] SendResult SendCardNumber(
    const wchar_t* pipe_name,
    std::string_view card_number) noexcept;
[[nodiscard]] std::wstring FormatStatus(
    const SendResult& result);

} // namespace gc::rfid::card_reader_test_client
```

Create an initially empty `CardReaderClient.cpp` that includes this header, and define a `gc_card_reader_test_client_transport` static target with public include paths `${PROJECT_SOURCE_DIR}/tools` and `${PROJECT_SOURCE_DIR}/src`. Add `add_subdirectory(CardReaderTestClient)` to `tools/CMakeLists.txt`.

Add `CardReaderClientTests` as a separate executable linked to both `gc_card_reader_test_client_transport` and `gc_rfid_core`. Use the real Task 3 server for accepted and invalid responses, and file-local raw pipe responders for malformed server responses. Cover:

- a valid card produces `SendStatus::accepted`, text `OK`, and the matching pending payload;
- a 15-byte request produces `SendStatus::invalid`, text `INVALID`, and no scan;
- a unique pipe with no server produces `pipe_unavailable`;
- a one-instance pipe already occupied by a holding client produces `pipe_busy`;
- a responder that closes without returning bytes produces `short_response`;
- a responder returning one byte (`O`) produces `short_response`;
- a responder returning a complete but unknown message (`NO`) produces `unexpected_response`; and
- a null pipe name produces `win32_error` with `ERROR_INVALID_PARAMETER`.

Assert the fixed UI strings `Pipe unavailable`, `Pipe busy`, `Short response`, `Unexpected response`, and `Windows error 87` through `FormatStatus`.

- [ ] **Step 2: Configure and build the client test to verify RED**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target CardReaderClientTests
```

Expected: linking fails because `SendCardNumber` and `FormatStatus` are declared but not defined.

- [ ] **Step 3: Implement one connection and one exact transaction**

In `CardReaderClient.cpp`, use a file-local RAII handle and this sequence:

1. Reject a null/empty name with `{win32_error, ERROR_INVALID_PARAMETER}`.
2. Open the pipe with `CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)`.
3. Map `ERROR_FILE_NOT_FOUND` to `pipe_unavailable` and `ERROR_PIPE_BUSY` to `pipe_busy`; preserve every other `GetLastError()` as `win32_error`.
4. Select `PIPE_READMODE_MESSAGE` with `SetNamedPipeHandleState`.
5. Write exactly `card_number.size()` bytes in one `WriteFile`; a successful partial count is `short_write`.
6. Read one response into a fixed eight-byte buffer. Map peer disconnect before any byte, or a successful read shorter than two bytes, to `short_response`. Map `ERROR_MORE_DATA` or any other complete unknown byte sequence to `unexpected_response`.
7. Return `accepted` only for exact bytes `OK`, and `invalid` only for exact bytes `INVALID`. A terminator or newline makes the response unexpected.
8. Never wait, retry, trim, append a terminator, or synthesize a card number.

Implement `FormatStatus` with these exact labels:

```text
OK
INVALID
Pipe unavailable
Pipe busy
Short write
Short response
Unexpected response
Windows error <decimal-code>
```

- [ ] **Step 4: Run the client/server transaction tests to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target CardReaderClientTests CardReaderInterfaceTests
ctest --preset msvc32-debug -R "^(CardReaderClientTests|CardReaderInterfaceTests)$"
```

Expected: the client recognizes both production responses, visibly distinguishes connection/protocol failures, and arms the real server only on valid input.

- [ ] **Step 5: Commit the client transport**

```powershell
git add -- tools/CMakeLists.txt tools/CardReaderTestClient/CMakeLists.txt tools/CardReaderTestClient/CardReaderClient.h tools/CardReaderTestClient/CardReaderClient.cpp tests/Rfid/CardReaderClientTests.cpp tests/Rfid/CMakeLists.txt
git commit -m "Add RFID card reader client transport"
```

### Task 6: Add the one-button native runtime test client

**Files:**
- Create: `tools/CardReaderTestClient/Main.cpp`
- Modify: `tools/CardReaderTestClient/CMakeLists.txt`

**Interfaces:**
- Consumes: `card_reader::kPipeName`, `kDefaultCardNumber`, `SendCardNumber`, and `FormatStatus`.
- Produces: `${GC_DIST_DIR}/CardReaderTestClient.exe` as a Windows GUI executable.

- [ ] **Step 1: Implement the fixed Win32 window**

Add a `wWinMain` application with a fixed-size, non-resizable top-level window titled `GCLoader Card Reader Test`. Create three child controls:

```text
Test card: 7020392010281502
[ Send Test Card ]
Not sent
```

Build the card label from `kDefaultCardNumber`; do not repeat the 16-digit literal in `Main.cpp`. A simple digit-to-wide conversion is sufficient because the shared constant is guaranteed ASCII decimal.

Use one fixed button command ID. On `BN_CLICKED`, run exactly:

```cpp
const auto result =
    gc::rfid::card_reader_test_client::SendCardNumber(
        gc::rfid::card_reader::kPipeName,
        gc::rfid::kDefaultCardNumber);
const auto status =
    gc::rfid::card_reader_test_client::FormatStatus(result);
SetWindowTextW(status_label, status.c_str());
```

Keep the synchronous call on the UI thread as approved. Catch all exceptions inside the Win32 window procedure and replace the status with `Unexpected client error`; do not let an exception cross the callback boundary. Handle `WM_DESTROY` with `PostQuitMessage(0)`.

- [ ] **Step 2: Define and stage the native GUI target**

Extend the tool CMake file:

```cmake
add_executable(CardReaderTestClient WIN32 Main.cpp)
target_include_directories(CardReaderTestClient PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tools
)
target_link_libraries(CardReaderTestClient PRIVATE
        gc_card_reader_test_client_transport
        user32
)
set_target_properties(CardReaderTestClient PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GC_DIST_DIR}"
        PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
if(MSVC)
    target_link_options(CardReaderTestClient PRIVATE
            "/ILK:${CMAKE_CURRENT_BINARY_DIR}/CardReaderTestClient.ilk")
endif()
```

Do not link `gc_config`, ConfigGUI, ImGui, or Direct3D.

- [ ] **Step 3: Build and inspect the Debug artifact**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target CardReaderTestClient
Get-Item build-msvc32-debug\dist\CardReaderTestClient.exe
dumpbin /headers build-msvc32-debug\dist\CardReaderTestClient.exe | Select-String "machine|subsystem"
```

Expected: the file exists; `dumpbin` reports machine `14C (x86)` and subsystem `2 (Windows GUI)`.

- [ ] **Step 4: Build and inspect the RelWithDebInfo artifact**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target CardReaderTestClient
Get-Item build-msvc32-release\dist\CardReaderTestClient.exe
dumpbin /headers build-msvc32-release\dist\CardReaderTestClient.exe | Select-String "machine|subsystem"
```

Expected: the release-distribution executable has the same x86 Windows-GUI identity.

- [ ] **Step 5: Commit the GUI client**

```powershell
git add -- tools/CardReaderTestClient/Main.cpp tools/CardReaderTestClient/CMakeLists.txt
git commit -m "Add one-button RFID runtime test client"
```

### Task 7: Publish the adapter-facing contract

**Files:**
- Create: `docs/card-reader-interface.md`

**Interfaces:**
- Documents: the stable external adapter contract and the bundled manual probe.
- Does not expose: reader-specific discovery, mapping, or hardware logic.

- [ ] **Step 1: Write the public protocol reference**

Document these exact sections:

1. **Purpose** — external software supplies the final GCLoader card number; GCLoader does not drive the physical reader.
2. **Availability** — `\\.\pipe\GCLoader.CardReader` exists only after the game opens emulated COM2; early/unavailable/busy clients retry by opening a new connection.
3. **Request** — one message, exactly 16 ASCII `0`-`9` bytes, no whitespace, newline, BOM, NUL, separators, or alternative encoding.
4. **Response** — exact ASCII `OK` or `INVALID`; only `OK` means accepted; client verifies full transfer and reconnects for the next card.
5. **State semantics** — one pending slot, newest valid keyboard or pipe trigger wins, repeated identical valid requests are independent, invalid input does not alter a pending scan.
6. **Minimal C++ example** — use `CreateFileW`, select message read mode, perform one exact `WriteFile`, read and compare the exact response, close, and reopen for the next scan.
7. **Runtime probe** — `CardReaderTestClient.exe` always sends the shared default card and is a contract probe, not an adapter SDK or card editor.

The example must check both Boolean Win32 results and exact byte counts. It must not include a trailing terminator in the request length and must treat any response other than exact `OK` as unaccepted.

- [ ] **Step 2: Check the written contract against the approved spec**

```powershell
rg -n "GCLoader\.CardReader|16 ASCII|OK|INVALID|COM2|new connection|newest|CardReaderTestClient" docs/card-reader-interface.md
git diff --check -- docs/card-reader-interface.md
```

Expected: every public contract element is present, and the document does not claim that the pipe discovers hardware or that static tests prove in-game behavior.

- [ ] **Step 3: Commit public documentation**

```powershell
git add -- docs/card-reader-interface.md
git commit -m "Document the external card reader interface"
```

### Task 8: Verify both x86 configurations and hand off runtime acceptance

**Files:**
- Verify only; modify implementation files only if a failing behavioral test exposes a defect in Tasks 1-7.

**Interfaces:**
- Consumes: the complete server, runtime integration, client transport, GUI, and documentation.
- Produces: Debug/RelWithDebInfo static evidence and an explicit manual in-game acceptance checklist.

- [ ] **Step 1: Run the focused Debug slice**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target RfidRuntimeTests JvsDeviceTests ComPortStateTests CardReaderInterfaceTests CardReaderClientTests Kernel32HookTests iDmacDrv32 CardReaderTestClient
ctest --preset msvc32-debug -R "^(RfidRuntimeTests|JvsDeviceTests|ComPortStateTests|CardReaderInterfaceTests|CardReaderClientTests|Kernel32HookTests)$"
```

Expected: six focused tests pass, the x86 DLL links, and the GUI probe is emitted under `build-msvc32-debug/dist`.

- [ ] **Step 2: Run the complete Debug suite**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
```

Expected: the complete Debug build and CTest suite pass.

- [ ] **Step 3: Run the focused RelWithDebInfo slice**

```powershell
cmake --preset msvc32-release
cmake --build --preset msvc32-release --target RfidRuntimeTests JvsDeviceTests ComPortStateTests CardReaderInterfaceTests CardReaderClientTests Kernel32HookTests iDmacDrv32 CardReaderTestClient
ctest --preset msvc32-release -R "^(RfidRuntimeTests|JvsDeviceTests|ComPortStateTests|CardReaderInterfaceTests|CardReaderClientTests|Kernel32HookTests)$"
```

Expected: the same focused contracts pass with optimization and release layout.

- [ ] **Step 4: Run the complete RelWithDebInfo suite**

```powershell
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: the complete RelWithDebInfo build and CTest suite pass.

- [ ] **Step 5: Inspect artifacts and repository scope**

```powershell
dumpbin /headers build-msvc32-release\dist\CardReaderTestClient.exe | Select-String "machine|subsystem"
Get-ChildItem build-msvc32-release\dist\iDmacDrv32.dll,build-msvc32-release\dist\CardReaderTestClient.exe | Select-Object Name,Length,LastWriteTime
git diff --check
git status --short --branch
git log -10 --oneline --decorate
```

Expected: the release artifacts are x86, the client is a Windows GUI executable, there are no whitespace errors, all feature files are committed, and only unrelated pre-existing user changes remain unstaged. Do not copy either artifact into `H:\gc` in this plan.

- [ ] **Step 6: Perform the user-owned runtime acceptance after deployment is separately authorized**

Once the user places the verified release artifacts into the runtime setup:

1. Start the game and wait until it has opened emulated COM2.
2. Start `CardReaderTestClient.exe`; confirm it shows `Test card: 7020392010281502` and `Not sent`.
3. Without pressing the configured `card_read` key, click `Send Test Card` once.
4. Require the client status to become `OK`.
5. Require the game to proceed through its normal card-read flow exactly once.
6. Click again only if testing repeated scans; a second `OK` must represent a separate one-shot scan.
7. If the client reports `Pipe unavailable` or `Pipe busy`, wait until COM2 is open and click again; the client itself must not retry automatically.

Record the client status and game observation separately. `OK` proves the pipe published the scan; the game's one-shot flow proves the JVS path consumed it. Compatibility with a specific physical reader remains pending until that reader's adapter performs the same contract.
