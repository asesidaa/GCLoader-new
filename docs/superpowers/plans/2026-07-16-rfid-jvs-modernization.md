# RFID/JVS Modernization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `RfidEmu.cpp` with a bounded, stateful, standard-backed JVS/RFID device whose valid game-visible behavior is preserved, whose malformed-input bugs are fixed, and whose complete owned hook set either installs successfully or aborts DLL attachment.

**Architecture:** Platform-free JVS types, decoder, encoder, and device logic feed one stateful virtual COM port. A process-lifetime RFID runtime starts its detached card-key worker lazily on the first `COM2` open. One Kernel32 adapter owns each imported hook, routes `COM2` before test-mode storage, and installs its exact requested set through an owned MinHook transaction. The final cutover updates `dllmain.cpp` directly and deletes the root-level RFID facade and monolith.

**Tech Stack:** C++23 (the current MSVC x86 build emits `/std:c++latest`), `std::array`, `std::span`, `std::expected`, `std::optional`, `std::variant`, `std::byte`, `std::thread`, `std::once_flag`, `std::chrono`, Win32 Kernel32 APIs, MinHook, CMake/Ninja, and CTest.

**Design:** [RFID/JVS Modernization Design](../specs/2026-07-16-rfid-jvs-modernization-design.md)

**Normative Reference:** [JAMMA VIDEO Standard, Third Edition](../../references/JVST_VER3.pdf), SHA-256 `E1D4128B21A896C7C299AE5DBC1009B51E11D2E2AEDCC46AA5DD090EA7AB7A88`.

**Informative English Reference:** [JAMMA Video Standard English Draft](../../references/jvs_wip.pdf), SHA-256 `76BF75A66ADD2A86EC889E4AD28906D9C97BC36EF76D9EC1A31309173BD41139`.

## Global Constraints

- Treat `H:\gc\artifacts\GCLoader` as the source and commit tree. Treat `H:\gc` as runtime/deployment state.
- Preserve all valid standard and Taito request/response behavior currently used by `game471.exe`. Apply safe standard-defined behavior to malformed or unsupported traffic.
- Preserve the RFID controller's observed `26 count selector[count]` request extension. Consume at most the remaining packet bytes for the selectors before parsing the next concatenated command.
- Preserve arbitrary `std::uint8_t` address, command, packet-status, and report values. Classification helpers must not reject custom values.
- Model the complete host-board connection: dispatch every complete command packet and checksum failure regardless of destination address. Use `0xFF` classification only inside commands with broadcast-specific semantics.
- Use any modern facility supported by the active x86 MSVC/STL when it strengthens an invariant; do not use unavailable `<scope>` facilities merely because they are associated with newer standards.
- Decode escaping before byte-count and checksum processing. A raw `0xE0` resynchronizes the stream; a trailing `0xD0` remains pending across chunks.
- Derive storage from the wire limits: 255 decoded bytes after the count and a conservative 515-byte encoded-frame capacity. Do not add arbitrary 1 KiB protocol buffers.
- Keep `gc::rfid::jvs::Decoder` and `Encoder` free of Win32, MinHook, logging, clocks, threads, and dynamic packet allocation.
- Keep standard JVS dispatch in `Rfid/Jvs/Device.*` and Taito-specific dispatch in `Rfid/TaitoCommands.*`.
- Store and return complete `DCB`, `COMMTIMEOUTS`, event-mask, modem, queue, and line state. Every synchronous `ReadFile`/`WriteFile` path initializes its byte-count output.
- Never expose bytes from two acknowledgements in one emulated `ReadFile`. Reject a new acknowledgement-requiring request while one is pending without damaging the existing reply.
- Keep the virtual device timing-free. No JVS/COM sleeps or simulated turnaround delays are added.
- Start the card worker once, lazily on the first `COM2` open and therefore outside loader lock. Detach it deliberately; the process owns its state until termination.
- If the worker cannot be created, fail the `COM2` open. If any requested hook cannot be resolved, created, or enabled, fail DLL attachment.
- Preserve the existing `CreateDirectoryA("OpenParrot", nullptr)` compatibility side effect, but do not treat its already-exists result as feature failure.
- Install only the RFID hooks plus storage hooks required by configuration. Do not call `MH_EnableHook(MH_ALL_HOOKS)` or modify NESYS hook ownership.
- Route `CreateFileA/W` in this order: exact `COM2`, enabled test-mode storage redirection, original API with unchanged arguments.
- No exception may cross a hooked Win32 function or `DllMain`.
- Automated completion requires focused tests, full CTest, an x86 production build, clean diff checks, and absence of legacy RFID files. Gameplay remains a separate user acceptance gate.
- Preserve the user-owned modifications currently present in `NesysServicePatch.cpp`, `NesysServiceProcess.cpp`, `RegistryConfigOverride.cpp`, `RegistryConfigOverride.h`, `tests/NesysServicePatchTests.cpp`, and `tests/RegistryConfigOverrideTests.cpp`. Do not stage or edit them as part of this plan.

---

## File Structure

| File | Responsibility |
|---|---|
| `Rfid/Jvs/Types.h` | Open protocol value types, bounded packets, typed decode events, statuses, reports, and frame capacities |
| `Rfid/Jvs/Decoder.h/.cpp` | Incremental raw-byte synchronization, unescaping, byte-count, checksum, and event production |
| `Rfid/Jvs/Encoder.h/.cpp` | Bounded checksum calculation and structural escaping |
| `Rfid/Jvs/Device.h/.cpp` | Host-board dispatch at every address, standard JVS commands, multi-command semantics, and acknowledgement construction |
| `Rfid/State.h/.cpp` | Address, coin, card-presence, card-payload, and device state |
| `Rfid/TaitoCommands.h/.cpp` | Taito-only command recognition and responses |
| `Rfid/ComPortState.h/.cpp` | Stateful serial configuration, incremental writes, pending reply, reads, and modem state |
| `Rfid/Runtime.h/.cpp` | Process-lifetime virtual port plus lazy detached card-key worker |
| `TestModeStorage/Redirector.h/.cpp` | Pure storage-root path policy |
| `TestModeStorage/Hooks.h/.cpp` | Enabled/disabled storage wrapper policy and safe routed-path ownership |
| `Win32Hooks/MinHookTransaction.h/.cpp` | Exact-hook resolution, create/enable, error detail, and reverse rollback |
| `Win32Hooks/Kernel32Hooks.h/.cpp` | Sole Kernel32 detour owner and RFID/storage routing adapter |
| `Rfid/Feature.h/.cpp` | Process-lifetime composition root and fail-closed installation |
| `dllmain.cpp` | Direct feature initialization and DLL-attach failure propagation |
| `tests/Rfid/JvsCodecTests.cpp` | Standard framing, escaping, capacity, fragmentation, resynchronization, and round trips |
| `tests/Rfid/JvsDeviceTests.cpp` | Golden standard/Taito behavior, multi-command parsing, error status, and overflow |
| `tests/Rfid/ComPortStateTests.cpp` | Stateful COM configuration and acknowledgement delivery |
| `tests/Rfid/RfidRuntimeTests.cpp` | Card one-shot state and lazy worker success/failure |
| `tests/TestModeStorage/TestModeStorageRedirectTests.cpp` | Pure redirect and wrapper-policy behavior |
| `tests/Win32Hooks/Kernel32HookTests.cpp` | Hook transaction failure matrix, rollback, routing, and forwarding |
| `CMakeLists.txt` | Production sources and six focused test targets |

### Task 1: Add Open JVS Types and the Incremental Decoder

**Files:**
- Create: `Rfid/Jvs/Types.h`
- Create: `Rfid/Jvs/Decoder.h`
- Create: `Rfid/Jvs/Decoder.cpp`
- Create: `tests/Rfid/JvsCodecTests.cpp`
- Modify: `CMakeLists.txt:415-565`

**Interfaces:**
- Consumes: Raw `std::span<const std::byte>` chunks whose boundaries are unrelated to JVS packets.
- Produces: `gc::rfid::jvs::Address`, `CommandId`, `DecodedPacket`, `ChecksumFailure`, `FramingError`, `DecodeEvent`, and `Decoder::Consume()`.

- [ ] **Step 1: Add the failing decoder conformance test target**

Add `JvsCodecTests` to `CMakeLists.txt`:

~~~cmake
add_executable(JvsCodecTests
        Rfid/Jvs/Decoder.cpp
        tests/Rfid/JvsCodecTests.cpp
)
target_include_directories(JvsCodecTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME JvsCodecTests COMMAND JvsCodecTests)
~~~

Create `tests/Rfid/JvsCodecTests.cpp` with a small local `expect` helper and these exact test groups:

~~~cpp
constexpr std::array kPage16Decoded{
    std::byte{0xE0}, std::byte{0xFF}, std::byte{0x05},
    std::byte{0xD0}, std::byte{0x00}, std::byte{0xE0},
    std::byte{0x00}, std::byte{0xB4}};
constexpr std::array kPage16Encoded{
    std::byte{0xE0}, std::byte{0xFF}, std::byte{0x05},
    std::byte{0xD0}, std::byte{0xCF}, std::byte{0x00},
    std::byte{0xD0}, std::byte{0xDF}, std::byte{0x00},
    std::byte{0xB4}};

static_assert(gc::rfid::jvs::Address{0x80}.value == 0x80);
static_assert(gc::rfid::jvs::CommandId{0xFE}.value == 0xFE);
static_assert(gc::rfid::jvs::kMaxDecodedAfterCount == 255);
static_assert(gc::rfid::jvs::kMaxEncodedFrameSize == 515);
~~~

The executable must assert:

1. Noise before sync produces no event.
2. `kPage16Encoded` produces one valid packet whose payload is `D0 00 E0 00`.
3. Every split position from zero through the encoded size produces the same packet.
4. A marker split between chunks remains pending.
5. Two frames in one chunk produce two ordered events.
6. A raw sync in a partial frame abandons it and starts the next frame.
7. A bad marker continuation emits one `FramingError` and recovers at the next sync.
8. A zero byte-count emits one `FramingError`.
9. A wrong checksum emits `ChecksumFailure` with the decoded address.
10. Addresses `0x00`, `0x01`, `0x20`, `0x80`, and `0xFF` remain representable; only helpers classify them.

- [ ] **Step 2: Run the test to prove the new contract is initially absent**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsCodecTests'
~~~

Expected: compilation fails because the JVS types and decoder API do not exist.

- [ ] **Step 3: Define bounded open protocol types**

Create `Rfid/Jvs/Types.h` around these exact public shapes:

~~~cpp
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace gc::rfid::jvs {

inline constexpr std::size_t kMaxDecodedAfterCount = 255;
inline constexpr std::size_t kMaxPayloadSize = 254;
inline constexpr std::size_t kMaxEncodedFrameSize = 515;
inline constexpr std::byte kSync{0xE0};
inline constexpr std::byte kMarker{0xD0};

struct Address {
    std::uint8_t value{};
    constexpr auto operator<=>(const Address&) const = default;
    [[nodiscard]] constexpr bool is_master() const noexcept {
        return value == 0x00;
    }
    [[nodiscard]] constexpr bool is_standard_slave() const noexcept {
        return value >= 0x01 && value <= 0x1F;
    }
    [[nodiscard]] constexpr bool is_broadcast() const noexcept {
        return value == 0xFF;
    }
};

struct CommandId {
    std::uint8_t value{};
    constexpr auto operator<=>(const CommandId&) const = default;
};

namespace address {
inline constexpr Address master{0x00};
inline constexpr Address broadcast{0xFF};
}

namespace command {
inline constexpr CommandId reset{0xF0};
inline constexpr CommandId set_address{0xF1};
inline constexpr CommandId read_id{0x10};
inline constexpr CommandId command_format_revision{0x11};
inline constexpr CommandId jvs_revision{0x12};
inline constexpr CommandId communication_revision{0x13};
inline constexpr CommandId capabilities{0x14};
inline constexpr CommandId read_switches{0x20};
inline constexpr CommandId read_coins{0x21};
inline constexpr CommandId read_general_input{0x26};
inline constexpr CommandId retransmit{0x2F};
inline constexpr CommandId decrease_coins{0x30};
inline constexpr CommandId increase_coins{0x31};
inline constexpr CommandId write_general_output{0x32};
}

struct Status {
    std::uint8_t value{};
    constexpr auto operator<=>(const Status&) const = default;
};

struct Report {
    std::uint8_t value{};
    constexpr auto operator<=>(const Report&) const = default;
};

namespace status {
inline constexpr Status ok{0x01};
inline constexpr Status unknown_command{0x02};
inline constexpr Status checksum_error{0x03};
inline constexpr Status acknowledgement_overflow{0x04};
}

namespace report {
inline constexpr Report ok{0x01};
inline constexpr Report invalid_input_parameter{0x02};
inline constexpr Report invalid_output_parameter{0x03};
inline constexpr Report busy{0x04};
}

struct DecodedPacket {
    Address address{};
    std::uint8_t byte_count{};
    std::array<std::uint8_t, kMaxDecodedAfterCount> after_count{};

    [[nodiscard]] std::span<const std::uint8_t> payload() const noexcept {
        const auto size = byte_count == 0
            ? std::size_t{0}
            : static_cast<std::size_t>(byte_count - 1);
        return {after_count.data(), size};
    }
    [[nodiscard]] std::optional<std::uint8_t> checksum() const noexcept {
        if (byte_count == 0) {
            return std::nullopt;
        }
        return after_count[byte_count - 1];
    }
};

struct ChecksumFailure {
    Address address{};
    std::uint8_t byte_count{};
};

enum class FramingError {
    InvalidEscape,
    ZeroByteCount,
};

using DecodeEvent =
    std::variant<DecodedPacket, ChecksumFailure, FramingError>;

struct EncodedFrame {
    std::array<std::byte, kMaxEncodedFrameSize> storage{};
    std::uint16_t size{};

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {storage.data(), size};
    }
};

}
~~~

Construction remains open. Do not add validating constructors or enums that make custom values unrepresentable.

- [ ] **Step 4: Implement the incremental decoder state machine**

Expose one-byte and arbitrary-span APIs in `Rfid/Jvs/Decoder.h`:

~~~cpp
#pragma once

#include "Rfid/Jvs/Types.h"

#include <functional>
#include <optional>
#include <utility>

namespace gc::rfid::jvs {

class Decoder {
public:
    [[nodiscard]] std::optional<DecodeEvent> Push(std::byte raw) noexcept;

    template <typename Sink>
    void Consume(std::span<const std::byte> input, Sink&& sink) noexcept {
        for (const auto raw : input) {
            if (auto event = Push(raw)) {
                std::invoke(sink, std::move(*event));
            }
        }
    }

    void Reset() noexcept;

private:
    enum class Phase { seeking_sync, address, byte_count, after_count };
    [[nodiscard]] std::optional<DecodeEvent>
        PushDecoded(std::uint8_t value) noexcept;

    Phase phase_{Phase::seeking_sync};
    bool marker_pending_{};
    DecodedPacket packet_{};
    std::uint16_t checksum_sum_{};
    std::uint16_t received_after_count_{};
};

}
~~~

Implement `Decoder.cpp` with these transitions:

~~~cpp
if (raw == kSync) {
    packet_ = {};
    checksum_sum_ = 0;
    received_after_count_ = 0;
    marker_pending_ = false;
    phase_ = Phase::address;
    return std::nullopt;
}
if (phase_ == Phase::seeking_sync) {
    return std::nullopt;
}
if (marker_pending_) {
    marker_pending_ = false;
    if (raw == std::byte{0xDF}) {
        return PushDecoded(0xE0);
    }
    if (raw == std::byte{0xCF}) {
        return PushDecoded(0xD0);
    }
    Reset();
    return DecodeEvent{FramingError::InvalidEscape};
}
if (raw == kMarker) {
    marker_pending_ = true;
    return std::nullopt;
}
return PushDecoded(std::to_integer<std::uint8_t>(raw));
~~~

`PushDecoded` stores address, rejects count zero, stores at most `byte_count` bytes, and compares the final stored byte with the low eight bits of the sum of address, count, and all preceding payload bytes. It emits exactly one event and returns to `seeking_sync` after a complete frame.

- [ ] **Step 5: Run the focused decoder tests**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsCodecTests && build-msvc32-latest\JvsCodecTests.exe'
~~~

Expected: `JvsCodecTests` builds and exits with code zero.

- [ ] **Step 6: Commit the decoder slice**

~~~powershell
git add -- CMakeLists.txt Rfid/Jvs/Types.h Rfid/Jvs/Decoder.h Rfid/Jvs/Decoder.cpp tests/Rfid/JvsCodecTests.cpp
git commit -m "refactor: add bounded incremental JVS decoder"
~~~

### Task 2: Add the Bounded JVS Encoder and Round-Trip Coverage

**Files:**
- Create: `Rfid/Jvs/Encoder.h`
- Create: `Rfid/Jvs/Encoder.cpp`
- Modify: `tests/Rfid/JvsCodecTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Any open `Address` and zero through 254 decoded payload bytes.
- Produces: `std::expected<EncodedFrame, EncodeError> EncodePacket(Address, std::span<const std::uint8_t>)`.

- [ ] **Step 1: Add failing encoder, capacity, and generated round-trip tests**

Add `Rfid/Jvs/Encoder.cpp` to `JvsCodecTests`. Extend the test with:

~~~cpp
const auto page16 = gc::rfid::jvs::EncodePacket(
    gc::rfid::jvs::Address{0xFF},
    std::array<std::uint8_t, 4>{0xD0, 0x00, 0xE0, 0x00});
failures += expect(page16.has_value(), "PDF page 16 frame encodes");
failures += expect(
    std::ranges::equal(page16->bytes(), kPage16Encoded),
    "PDF page 16 encoded bytes");

std::array<std::uint8_t, gc::rfid::jvs::kMaxPayloadSize + 1>
    too_large{};
failures += expect(
    !gc::rfid::jvs::EncodePacket(
        gc::rfid::jvs::Address{0x01}, too_large).has_value(),
    "payload over 254 bytes is rejected");
~~~

Add deterministic round trips for payload lengths `0, 1, 2, 0xCF, 0xD0, 0xDF, 0xE0, 0xFE`. Fill bytes using `(index * 73 + length * 19) & 0xFF`, then force `0xD0` and `0xE0` into every structural position that exists. For each encoded frame, feed every possible two-chunk split into a fresh decoder and require one equal packet. Also assert `frame.size <= kMaxEncodedFrameSize` and `EncodedFrame::storage.size() == 515`.

- [ ] **Step 2: Run the encoder test and observe the missing API**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsCodecTests'
~~~

Expected: compilation or linking fails because `EncodePacket` is absent.

- [ ] **Step 3: Implement checksum-first bounded encoding**

Create `Rfid/Jvs/Encoder.h`:

~~~cpp
#pragma once

#include "Rfid/Jvs/Types.h"

#include <expected>

namespace gc::rfid::jvs {

enum class EncodeError {
    payload_too_large,
    capacity_invariant,
};

[[nodiscard]] std::expected<EncodedFrame, EncodeError> EncodePacket(
    Address address,
    std::span<const std::uint8_t> payload) noexcept;

}
~~~

In `Encoder.cpp`, compute the decoded count and checksum before escaping. Use one checked append helper:

~~~cpp
const auto append_escaped =
    [&frame](std::uint8_t value) -> bool {
        const auto append = [&frame](std::byte byte) {
            if (frame.size == frame.storage.size()) {
                return false;
            }
            frame.storage[frame.size++] = byte;
            return true;
        };

        if (value == 0xD0) {
            return append(std::byte{0xD0}) && append(std::byte{0xCF});
        }
        if (value == 0xE0) {
            return append(std::byte{0xD0}) && append(std::byte{0xDF});
        }
        return append(std::byte{value});
    };
~~~

Write sync unescaped, then address, count, payload, and checksum through `append_escaped`. Reject `payload.size() > 254` before narrowing. Sum through a `std::uint32_t` and narrow only the final low byte.

- [ ] **Step 4: Run the complete codec matrix**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsCodecTests && build-msvc32-latest\JvsCodecTests.exe'
~~~

Expected: all framing, fragmentation, checksum, maximum-size, custom-value, and round-trip cases pass.

- [ ] **Step 5: Commit the encoder slice**

~~~powershell
git add -- CMakeLists.txt Rfid/Jvs/Encoder.h Rfid/Jvs/Encoder.cpp tests/Rfid/JvsCodecTests.cpp
git commit -m "refactor: add bounded JVS encoder"
~~~

### Task 3: Move Card, Address, and Coin State Under RFID Ownership

**Files:**
- Create: `Rfid/State.h`
- Create: `Rfid/State.cpp`
- Move: `tests/CardScanStateTests.cpp` to `tests/Rfid/RfidRuntimeTests.cpp`
- Modify: `RfidEmu.cpp:1-50`
- Modify: `CMakeLists.txt:153-170, 415-421`
- Delete: `CardScanState.h`

**Interfaces:**
- Consumes: Card-key edges and device command mutations.
- Produces: `gc::rfid::CardScanState`, `gc::rfid::State`, `kCardData`, `State::ResetBus()`, and one-shot card consumption.

- [ ] **Step 1: Move and expand the existing state test**

Use `git mv tests/CardScanStateTests.cpp tests/Rfid/RfidRuntimeTests.cpp`, retain every current assertion, and add address and coin reset assertions:

~~~cpp
gc::rfid::State state;
state.assigned_address = gc::rfid::jvs::Address{0x7F};
state.coins = {12, 34};
state.ResetBus();

failures += expect(
    !state.assigned_address.has_value(),
    true,
    "bus reset clears assigned address");
failures += expect(state.coins[0] == 0, true, "bus reset clears P1 coins");
failures += expect(state.coins[1] == 0, true, "bus reset clears P2 coins");
~~~

Rename the CMake target to `RfidRuntimeTests` and make it compile `Rfid/State.cpp` plus `tests/Rfid/RfidRuntimeTests.cpp`.

- [ ] **Step 2: Run the moved test before adding the new state**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target RfidRuntimeTests'
~~~

Expected: compilation fails because `Rfid/State.h` is absent.

- [ ] **Step 3: Define the process-lifetime RFID state**

Create `Rfid/State.h`:

~~~cpp
#pragma once

#include "Rfid/Jvs/Types.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace gc::rfid {

inline constexpr std::array<std::uint8_t, 0x18> kCardData{
    0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00,
    0x37, 0x30, 0x32, 0x30, 0x33, 0x39, 0x32, 0x30,
    0x31, 0x30, 0x32, 0x38, 0x31, 0x35, 0x30, 0x32};

class CardScanState {
public:
    void Arm() noexcept { present_.store(true); }
    [[nodiscard]] bool IsPresent() const noexcept {
        return present_.load();
    }
    [[nodiscard]] bool Consume() noexcept {
        return present_.exchange(false);
    }

private:
    std::atomic_bool present_{};
};

struct State {
    std::optional<jvs::Address> assigned_address;
    std::array<std::uint16_t, 2> coins{};
    CardScanState card_scan;

    void ResetBus() noexcept;
};

}
~~~

`State::ResetBus()` clears the address and both coin counters. It does not clear an already armed physical card scan; reset and card presence are independent observable states.

- [ ] **Step 4: Update the legacy caller directly and remove the old header**

Change only the include in `RfidEmu.cpp` from `CardScanState.h` to `Rfid/State.h` and remove its local card-payload array in favor of `gc::rfid::kCardData`. Delete the old owner explicitly:

~~~powershell
git rm -- CardScanState.h
~~~

This is a direct caller migration, not a facade.

- [ ] **Step 5: Build both the migrated test and unchanged production DLL**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target RfidRuntimeTests iDmacDrv32 && build-msvc32-latest\RfidRuntimeTests.exe'
~~~

Expected: the focused state test passes and the still-legacy production DLL builds with the new state owner.

- [ ] **Step 6: Commit the state migration**

~~~powershell
git add -- CMakeLists.txt Rfid/State.h Rfid/State.cpp RfidEmu.cpp tests/Rfid/RfidRuntimeTests.cpp
git commit -m "refactor: move RFID state under feature ownership"
~~~

### Task 4: Implement Standard JVS Device Semantics from Golden Behavior

**Files:**
- Create: `Rfid/Jvs/Device.h`
- Create: `Rfid/Jvs/Device.cpp`
- Create: `tests/Rfid/JvsDeviceTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Valid `DecodedPacket` or addressed `ChecksumFailure` plus `gc::rfid::State`.
- Produces: `Acknowledgement`, `ReplyWriter`, `Device::HandlePacket()`, and `Device::HandleChecksumFailure()` without encoding or Win32 dependencies.

- [ ] **Step 1: Add a failing golden device test target**

Add the target:

~~~cmake
add_executable(JvsDeviceTests
        Rfid/Jvs/Device.cpp
        Rfid/State.cpp
        tests/Rfid/JvsDeviceTests.cpp
)
target_include_directories(JvsDeviceTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(NAME JvsDeviceTests COMMAND JvsDeviceTests)
~~~

Build decoded packets with a checked local helper and assert the exact acknowledgement payloads below. The first byte shown is packet status; each later `01` is a command report.

| Request payload | Required acknowledgement payload/state |
|---|---|
| broadcast `F0 D9` | no acknowledgement; address and both coin counts reset |
| broadcast `F1 01` | `01 01`; assigned address becomes `01` |
| `10` | `01 01` + `TAITO CORP.;RFID CTRL P.C.B.;Ver1.00; 00` |
| `11` | `01 01 13` |
| `12` | `01 01 30` |
| `13` | `01 01 10` |
| `14` | `01 01 01 07 00 08 00 12 08 00 00 00` |
| `20 02 02` | `01 01 00 00 00 00 00` |
| `21 02` | `01 01 00 00 00 00` |
| `26 03` with no card | `01 01 00 00 00` |
| `26 03` with armed card | `01 01 19 19 19` without consuming the card |
| `26 01 61 32 01 00` with no card | `01 01 00 01` + 24 zero bytes + `01`; `61` is consumed as the RFID selector byte before command `32` |
| `31 01 00 05` then `21 02` | P1 coin count becomes five and is returned big-endian |
| `30 01 00 02` then `21 02` | P1 coin count becomes three |
| `30 01 00 09` | P1 coin count saturates at zero |
| `32 01 00` with armed card | `01 01` + exact 24-byte `kCardData` + `01`; card is consumed |
| `32 01 00` after consumption | `01 01` + 24 zero bytes + `01` |
| `11 12 13` | `01 01 13 01 30 01 10` in command order |
| `2F` | no acknowledgement, preserving the current retransmission-request behavior |

Also assert:

- The exact acknowledgement payloads above are returned unchanged at host address `0x00`, the assigned address, arbitrary standard/custom addresses, and broadcast address `0xFF`.
- Reset and address assignment are accepted only through their broadcast semantics.
- A checksum failure at any address returns payload `03`.
- `20` with impossible player/byte parameters returns status `01` and report `02` without input data.
- `30`, `31`, or `32` with invalid output parameters returns status `01` and report `03` while discarding output bytes.
- An unknown command changes packet status to `02`, stops parsing later commands, and retains reports already produced earlier in the packet.
- A reply exceeding 254 payload bytes collapses to the single status byte `04`.

- [ ] **Step 2: Run the device target before implementing it**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsDeviceTests'
~~~

Expected: compilation fails because `Device` and `Acknowledgement` are absent.

- [ ] **Step 3: Define acknowledgement construction without dynamic buffers**

Create `Rfid/Jvs/Device.h` with these public types:

~~~cpp
#pragma once

#include "Rfid/Jvs/Types.h"
#include "Rfid/State.h"

#include <array>
#include <optional>
#include <span>

namespace gc::rfid::jvs {

struct Acknowledgement {
    std::array<std::uint8_t, kMaxPayloadSize> payload{
        status::ok.value};
    std::uint8_t size{1};

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
        return {payload.data(), size};
    }
};

class ReplyWriter {
public:
    explicit ReplyWriter(Acknowledgement& reply) noexcept;
    [[nodiscard]] bool Append(std::uint8_t value) noexcept;
    [[nodiscard]] bool Append(
        std::span<const std::uint8_t> values) noexcept;
    void SetStatus(Status value) noexcept;
    void SetOverflow() noexcept;

private:
    Acknowledgement& reply_;
};

class Device {
public:
    explicit Device(gc::rfid::State& state) noexcept;

    [[nodiscard]] std::optional<Acknowledgement> HandlePacket(
        const DecodedPacket& packet) noexcept;
    [[nodiscard]] std::optional<Acknowledgement> HandleChecksumFailure(
        const ChecksumFailure& failure) noexcept;

private:
    gc::rfid::State& state_;
};

}
~~~

`ReplyWriter::Append` checks remaining capacity before every write. `SetOverflow` resets the acknowledgement to exactly one byte, `status::acknowledgement_overflow.value`.

- [ ] **Step 4: Implement bounded cursor-based standard dispatch**

In `Device.cpp`, parse through a span and never index before proving the required parameter count:

~~~cpp
struct RequestCursor {
    std::span<const std::uint8_t> remaining;

    [[nodiscard]] std::optional<std::span<const std::uint8_t>>
    Take(std::size_t count) noexcept {
        if (remaining.size() < count) {
            return std::nullopt;
        }
        const auto taken = remaining.first(count);
        remaining = remaining.subspan(count);
        return taken;
    }
};
~~~

Use the named `command::*` values from `Types.h`. Implement every row in the golden table directly. For each successful command append one report followed by its data. On insufficient input, append `report::invalid_input_parameter` and stop because no complete later command can be located safely. On invalid output values, consume the declared command extent, append `report::invalid_output_parameter`, and do not mutate state.

Do not add a top-level address-admission predicate. This emulator represents the host board for the complete connection and therefore dispatches standard and Taito commands at every `std::uint8_t` address. Preserve the original implementation's acknowledgement payload bytes independently of the request address.

Use `packet.address.is_broadcast()` only inside reset and address-assignment handling, whose command semantics require broadcast. Do not constrain the value assigned by `F1`; a custom address remains valid device state but never becomes a routing filter. `HandleChecksumFailure` likewise returns status `03` without filtering on the assigned address.

- [ ] **Step 5: Run the standard device characterization matrix**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsDeviceTests && build-msvc32-latest\JvsDeviceTests.exe'
~~~

Expected: every standard, address, parameter, multi-command, checksum, and overflow assertion passes.

- [ ] **Step 6: Commit the standard device slice**

~~~powershell
git add -- CMakeLists.txt Rfid/Jvs/Device.h Rfid/Jvs/Device.cpp tests/Rfid/JvsDeviceTests.cpp
git commit -m "refactor: add bounded standard JVS device"
~~~

### Task 5: Isolate and Characterize Taito-Specific Commands

**Files:**
- Create: `Rfid/TaitoCommands.h`
- Create: `Rfid/TaitoCommands.cpp`
- Modify: `Rfid/Jvs/Device.cpp`
- Modify: `tests/Rfid/JvsDeviceTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: An open `CommandId` and remaining decoded request bytes.
- Produces: `std::optional<TaitoCommandResult> HandleTaitoCommand()`; `std::nullopt` means the command is not Taito-owned.

- [ ] **Step 1: Add failing Taito golden cases**

Add `Rfid/TaitoCommands.cpp` to `JvsDeviceTests` and add these exact cases:

| Request payload | Consumed bytes | Appended report/data |
|---|---:|---|
| `01 00` | 2 | `01 01` |
| `03 00` | 2 | `01 01` |
| `04` | 1 | `01` |
| `05 00 00` | 3 | `01` |

For each command, also test one byte fewer than its required extent. The result must append `report::invalid_input_parameter` without reading beyond the request. Add a mixed packet `11 01 00 12` and require `01 01 13 01 01 01 30` so standard and Taito reports remain ordered.

- [ ] **Step 2: Run the test before adding Taito dispatch**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsDeviceTests && build-msvc32-latest\JvsDeviceTests.exe'
~~~

Expected: the new Taito cases fail while the standard cases remain green.

- [ ] **Step 3: Define and implement the vendor result**

Create `Rfid/TaitoCommands.h`:

~~~cpp
#pragma once

#include "Rfid/Jvs/Types.h"

#include <array>
#include <optional>
#include <span>

namespace gc::rfid {

namespace taito_command {
inline constexpr jvs::CommandId query_01{0x01};
inline constexpr jvs::CommandId query_03{0x03};
inline constexpr jvs::CommandId notify_04{0x04};
inline constexpr jvs::CommandId notify_05{0x05};
}

struct TaitoCommandResult {
    std::size_t consumed{};
    jvs::Report report{jvs::report::ok};
    std::array<std::uint8_t, 1> data{};
    std::uint8_t data_size{};
};

[[nodiscard]] std::optional<TaitoCommandResult> HandleTaitoCommand(
    jvs::CommandId command,
    std::span<const std::uint8_t> bytes_after_command) noexcept;

}
~~~

Implement the four rows in `TaitoCommands.cpp` with a `switch (command.value)` over the named `taito_command::*` values. Return `std::nullopt` for all other byte values; do not reject or reinterpret them. Return `invalid_input_parameter` when the selected Taito command lacks its fixed bytes, and consume all safely available bytes so `Device` terminates that malformed packet.

- [ ] **Step 4: Integrate vendor dispatch without merging namespaces**

At each command in `Device.cpp`, ask `HandleTaitoCommand` first. If it returns a value, append its report/data and advance by its declared consumed count. Otherwise continue through standard dispatch. Keep Taito constants and response data out of `Rfid/Jvs/Device.h`.

- [ ] **Step 5: Run all device tests**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsDeviceTests && build-msvc32-latest\JvsDeviceTests.exe'
~~~

Expected: all standard, Taito, mixed, truncation, and custom-command tests pass.

- [ ] **Step 6: Commit the vendor slice**

~~~powershell
git add -- CMakeLists.txt Rfid/TaitoCommands.h Rfid/TaitoCommands.cpp Rfid/Jvs/Device.cpp tests/Rfid/JvsDeviceTests.cpp
git commit -m "refactor: isolate Taito JVS commands"
~~~

### Task 6: Build the Stateful Virtual COM Port

**Files:**
- Create: `Rfid/ComPortState.h`
- Create: `Rfid/ComPortState.cpp`
- Create: `tests/Rfid/ComPortStateTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Complete or fragmented raw writes plus serial configuration operations.
- Produces: `ComPortState` methods returning `std::expected<T, DWORD>`, one pending `EncodedFrame`, deterministic serial getters, and timing-free reads.

- [ ] **Step 1: Add the failing COM-port test target**

Add:

~~~cmake
add_executable(ComPortStateTests
        Rfid/ComPortState.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Device.cpp
        Rfid/Jvs/Encoder.cpp
        tests/Rfid/ComPortStateTests.cpp
)
target_include_directories(ComPortStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
add_test(NAME ComPortStateTests COMMAND ComPortStateTests)
~~~

The test must replay the binary-observed sequence:

~~~cpp
gc::rfid::ComPortState port;
failures += expect(port.SetupComm(0x204, 0x204).has_value(),
                   "SetupComm 0x204 queues");

auto dcb = port.GetCommState();
dcb.BaudRate = CBR_115200;
dcb.ByteSize = 8;
dcb.Parity = NOPARITY;
dcb.StopBits = ONESTOPBIT;
failures += expect(port.SetCommState(dcb).has_value(), "SetCommState 8N1");
failures += expect(port.SetCommMask(1).has_value(), "SetCommMask one");

auto timeouts = port.GetCommTimeouts();
timeouts.ReadTotalTimeoutConstant = 20;
failures += expect(
    port.SetCommTimeouts(timeouts).has_value(),
    "SetCommTimeouts 20 ms");
~~~

Then assert:

1. Every getter returns all fields initialized and exactly stores the preceding setter.
   `QueueSizes()` and `GetLineState()` make the non-Win32 stored values directly testable.
2. `SetupComm` stores arbitrary `DWORD` queue sizes. Invalid `DCBlength`, non-8N1 settings, and overlapped operations return deterministic Win32 errors without mutation.
3. A request split at every raw byte boundary produces one reply only after completion.
4. A write containing two packets emits both decoder events, but a second reply-requiring packet while the first reply is pending is discarded and diagnosed.
5. `Read` can drain one reply through one-byte fragments.
6. `Read` never coalesces replies and an empty read returns zero immediately.
7. `PendingByteCount()` exactly follows the unread cursor.
8. An existing reply remains byte-identical after a pipelined request is rejected.
9. The write/read result counts equal bytes accepted/copied.
10. `ModemStatus()` is zero before address assignment and `MS_CTS_ON` afterward.
11. `EscapeCommFunction` records `SETDTR`, `CLRDTR`, `SETRTS`, `CLRRTS`, `SETXOFF`, `SETXON`, `SETBREAK`, and `CLRBREAK` state and rejects values outside the Win32 control vocabulary.
12. Closing resets the serial session and pending decoder/reply state without destroying process-lifetime RFID card state.

- [ ] **Step 2: Run the target before the port exists**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target ComPortStateTests'
~~~

Expected: compilation fails because `ComPortState` is absent.

- [ ] **Step 3: Define a value-returning serial interface**

Create `Rfid/ComPortState.h` with this public surface:

~~~cpp
#pragma once

#include "Rfid/Jvs/Decoder.h"
#include "Rfid/Jvs/Device.h"
#include "Rfid/Jvs/Encoder.h"
#include "Rfid/State.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>

namespace gc::rfid {

struct LineState {
    bool dtr{};
    bool rts{};
    bool xoff{};
    bool break_active{};
};

class ComPortState {
public:
    ComPortState() noexcept;

    void Open() noexcept;
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;

    [[nodiscard]] std::expected<void, DWORD> SetupComm(
        DWORD input_queue, DWORD output_queue) noexcept;
    [[nodiscard]] DCB GetCommState() const noexcept;
    [[nodiscard]] std::expected<void, DWORD> SetCommState(
        const DCB& value) noexcept;
    [[nodiscard]] DWORD GetCommMask() const noexcept;
    [[nodiscard]] std::expected<void, DWORD> SetCommMask(
        DWORD value) noexcept;
    [[nodiscard]] COMMTIMEOUTS GetCommTimeouts() const noexcept;
    [[nodiscard]] std::expected<void, DWORD> SetCommTimeouts(
        const COMMTIMEOUTS& value) noexcept;
    [[nodiscard]] DWORD ModemStatus() const noexcept;
    [[nodiscard]] std::expected<void, DWORD> EscapeCommFunction(
        DWORD function) noexcept;

    [[nodiscard]] std::expected<std::size_t, DWORD> Write(
        std::span<const std::byte> bytes,
        bool overlapped) noexcept;
    [[nodiscard]] std::expected<std::size_t, DWORD> Read(
        std::span<std::byte> destination,
        bool overlapped) noexcept;
    [[nodiscard]] DWORD PendingByteCount() const noexcept;
    [[nodiscard]] COMSTAT CommStatus() const noexcept;
    [[nodiscard]] std::pair<DWORD, DWORD> QueueSizes() const noexcept;
    [[nodiscard]] LineState GetLineState() const noexcept;
    [[nodiscard]] std::uint64_t SequencingViolationCount() const noexcept;

    [[nodiscard]] State& device_state() noexcept;

private:
    State state_{};
    jvs::Device device_{state_};
    jvs::Decoder decoder_{};
    std::optional<jvs::EncodedFrame> pending_reply_;
    std::size_t read_cursor_{};
    DCB dcb_{};
    COMMTIMEOUTS timeouts_{};
    DWORD event_mask_{};
    DWORD input_queue_size_{};
    DWORD output_queue_size_{};
    bool open_{};
    bool dtr_{};
    bool rts_{};
    bool xoff_{};
    bool break_active_{};
    std::uint64_t sequencing_violation_count_{};
};

}
~~~

- [ ] **Step 4: Implement complete deterministic state and bounded I/O**

Initialize `dcb_` with `DCBlength = sizeof(DCB)`, `CBR_115200`, `fBinary = TRUE`, disabled parity/flow-control flags, eight data bits, no parity, and one stop bit. Zero-initialize all other fields and all timeout fields.

`Write` feeds every byte to `decoder_`. For each event:

~~~cpp
std::optional<jvs::Acknowledgement> acknowledgement;
std::visit(
    [&acknowledgement, this](const auto& event) {
        using Event = std::remove_cvref_t<decltype(event)>;
        if constexpr (std::same_as<Event, jvs::DecodedPacket>) {
            acknowledgement = device_.HandlePacket(event);
        } else if constexpr (std::same_as<Event, jvs::ChecksumFailure>) {
            acknowledgement = device_.HandleChecksumFailure(event);
        }
    },
    decode_event);
~~~

Ignore framing diagnostics at the protocol layer. When an acknowledgement exists and no reply is pending, encode it to master address `0x00` and install it as the sole pending frame. When one is already pending, retain it, increment `sequencing_violation_count_`, and log one packet-level diagnostic. `Read` copies only `min(destination.size(), PendingByteCount())` bytes, clears the optional frame when its cursor reaches the end, and never sleeps.

`CommStatus()` starts from `COMSTAT{}` and sets only defined current fields, including `cbInQue = PendingByteCount()`. No uninitialized bitfield reaches the hook layer.

- [ ] **Step 5: Run the COM state suite and production-independent protocol suites**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target JvsCodecTests JvsDeviceTests ComPortStateTests && build-msvc32-latest\JvsCodecTests.exe && build-msvc32-latest\JvsDeviceTests.exe && build-msvc32-latest\ComPortStateTests.exe'
~~~

Expected: all three focused executables pass without starting threads or installing hooks.

- [ ] **Step 6: Commit the COM-port slice**

~~~powershell
git add -- CMakeLists.txt Rfid/ComPortState.h Rfid/ComPortState.cpp tests/Rfid/ComPortStateTests.cpp
git commit -m "refactor: add stateful JVS COM port"
~~~

### Task 7: Add the Lazy Process-Lifetime RFID Runtime

**Files:**
- Create: `Rfid/Runtime.h`
- Create: `Rfid/Runtime.cpp`
- Modify: `tests/Rfid/RfidRuntimeTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Configured Win32 virtual key and injectable worker/key/sleep operations.
- Produces: `Runtime::OpenCom2()`, `Runtime::CloseCom2()`, `Runtime::port()`, `EmulatedComHandle()`, and `ProductionCardWorkerApi()`.

- [ ] **Step 1: Add failing lazy-start and failure tests**

Add `Rfid/Runtime.cpp`, `Rfid/ComPortState.cpp`, and their protocol dependencies to `RfidRuntimeTests`. Add the logging include used by the COM diagnostic:

~~~cmake
target_include_directories(RfidRuntimeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
~~~

Add a fake `CardWorkerApi` whose `start_detached` only records its entry/context and returns a selected result.

Assert:

~~~cpp
auto first = runtime.OpenCom2();
auto second = runtime.OpenCom2();
failures += expect(first.has_value(), "first COM2 open");
failures += expect(second.has_value(), "second COM2 open");
failures += expect(*first == gc::rfid::EmulatedComHandle(),
                   "stable emulated handle");
failures += expect(fake.start_calls == 1, "worker starts exactly once");

runtime.CloseCom2();
auto reopened = runtime.OpenCom2();
failures += expect(reopened.has_value(), "COM2 reopens");
failures += expect(fake.start_calls == 1, "worker remains process-lifetime");
~~~

For a launcher returning `std::unexpected(ERROR_NOT_ENOUGH_MEMORY)`, require every open to return that error, require the port to remain closed, and require only one launch attempt. Retain all one-shot card-state tests from Task 3.

- [ ] **Step 2: Run the runtime test before adding the lifecycle**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target RfidRuntimeTests'
~~~

Expected: compilation fails because `Runtime` and `CardWorkerApi` are absent.

- [ ] **Step 3: Define an injectable but process-lifetime worker boundary**

Create `Rfid/Runtime.h`:

~~~cpp
#pragma once

#include "Rfid/ComPortState.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <expected>
#include <mutex>

namespace gc::rfid {

using CardWorkerEntry = void (*)(void*) noexcept;

struct CardWorkerApi {
    std::expected<void, DWORD> (*start_detached)(
        CardWorkerEntry entry, void* context) noexcept;
    SHORT (*get_async_key_state)(int virtual_key) noexcept;
    void (*sleep_for)(std::chrono::milliseconds duration) noexcept;
};

[[nodiscard]] CardWorkerApi ProductionCardWorkerApi() noexcept;
[[nodiscard]] HANDLE EmulatedComHandle() noexcept;

class Runtime {
public:
    explicit Runtime(
        int card_virtual_key,
        CardWorkerApi worker_api = ProductionCardWorkerApi()) noexcept;

    [[nodiscard]] std::expected<HANDLE, DWORD> OpenCom2() noexcept;
    void CloseCom2() noexcept;
    [[nodiscard]] ComPortState& port() noexcept;

private:
    static void CardWorkerMain(void* context) noexcept;
    void RunCardWorker() noexcept;

    int card_virtual_key_{};
    CardWorkerApi worker_api_{};
    ComPortState port_{};
    std::once_flag worker_once_;
    std::atomic_bool worker_started_{};
    std::atomic<DWORD> worker_error_{ERROR_SUCCESS};
};

}
~~~

- [ ] **Step 4: Implement explicit detached ownership and 100 ms edge polling**

Production launch catches `std::system_error` around thread creation:

~~~cpp
std::expected<void, DWORD> StartDetached(
    CardWorkerEntry entry,
    void* context) noexcept {
    try {
        std::thread{[entry, context] { entry(context); }}.detach();
        return {};
    } catch (const std::system_error&) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    }
}
~~~

`OpenCom2` uses `std::call_once` to record either permanent worker success or its exact failure. It opens `port_` only after success. `RunCardWorker` keeps `key_was_down` worker-local:

~~~cpp
bool key_was_down = false;
for (;;) {
    const bool key_is_down =
        card_virtual_key_ != 0 &&
        (worker_api_.get_async_key_state(card_virtual_key_) & 0x8000) != 0;
    if (key_is_down && !key_was_down) {
        port_.device_state().card_scan.Arm();
    }
    key_was_down = key_is_down;
    worker_api_.sleep_for(std::chrono::milliseconds{100});
}
~~~

Do not add a stop token, destructor join, unload callback, or JVS timing delay. `CloseCom2` closes only the logical serial session; it does not stop the process-lifetime worker.

- [ ] **Step 5: Run the runtime and COM suites**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target RfidRuntimeTests ComPortStateTests && build-msvc32-latest\RfidRuntimeTests.exe && build-msvc32-latest\ComPortStateTests.exe'
~~~

Expected: worker start/failure, reopening, one-shot card, and COM tests pass without a live test thread.

- [ ] **Step 6: Commit the runtime slice**

~~~powershell
git add -- CMakeLists.txt Rfid/Runtime.h Rfid/Runtime.cpp tests/Rfid/RfidRuntimeTests.cpp
git commit -m "refactor: add lazy RFID runtime"
~~~

### Task 8: Move Test-Mode Storage into Its Own Directory and Policy

**Files:**
- Move: `TestModeStorageRedirect.h` to `TestModeStorage/Redirector.h`
- Move: `TestModeStorageRedirect.cpp` to `TestModeStorage/Redirector.cpp`
- Create: `TestModeStorage/Hooks.h`
- Create: `TestModeStorage/Hooks.cpp`
- Move: `tests/TestModeStorageRedirectTests.cpp` to `tests/TestModeStorage/TestModeStorageRedirectTests.cpp`
- Modify: `RfidEmu.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Enabled flag, ANSI/wide paths, and the current directory.
- Produces: Existing `RedirectPathA/W` plus owned `RoutedPathA/W` values from `gc::testmode_storage::Hooks`.

- [ ] **Step 1: Move existing tests and add wrapper-policy failures**

Move the existing test intact, update its include, and add enabled/disabled cases:

~~~cpp
gc::testmode_storage::Hooks disabled{false};
auto unchanged = disabled.RoutePathA(
    "D:\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\file",
    "C:\\GC");
failures += expect(
    std::string_view{unchanged.get()} ==
        "D:\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\file",
    "disabled storage route is unchanged");

gc::testmode_storage::Hooks enabled{true};
auto redirected = enabled.RoutePathA(
    "D:\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\file",
    "C:\\GC");
failures += expect(
    std::string_view{redirected.get()} ==
        "C:\\GC\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\file",
    "enabled storage route");
failures += expect(
    enabled.DiskSpaceDirectoryA("D:\\source") == nullptr,
    "enabled disk-space query uses current volume");
~~~

Repeat the policy cases for wide paths, null input, nonmatching pass-through, and a simulated current-directory lookup failure.

- [ ] **Step 2: Move the pure redirector and observe the absent wrapper policy**

Move the three existing files exactly:

~~~powershell
git mv TestModeStorageRedirect.h TestModeStorage/Redirector.h
git mv TestModeStorageRedirect.cpp TestModeStorage/Redirector.cpp
git mv tests/TestModeStorageRedirectTests.cpp tests/TestModeStorage/TestModeStorageRedirectTests.cpp
~~~

Update the production/test includes and replace the existing target with:

~~~cmake
add_executable(TestModeStorageRedirectTests
        TestModeStorage/Redirector.cpp
        TestModeStorage/Hooks.cpp
        tests/TestModeStorage/TestModeStorageRedirectTests.cpp
)
target_include_directories(TestModeStorageRedirectTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${plog_SOURCE_DIR}/include
)
add_test(NAME TestModeStorageRedirectTests COMMAND TestModeStorageRedirectTests)
~~~

Then run:

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target TestModeStorageRedirectTests'
~~~

Expected: the target fails because `TestModeStorage/Hooks.h` does not exist.

- [ ] **Step 3: Define routed values whose pointer lifetime covers the original call**

Create `TestModeStorage/Hooks.h`:

~~~cpp
#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace gc::testmode_storage {

struct RoutedPathA {
    LPCSTR original{};
    std::optional<std::string> redirected;
    [[nodiscard]] LPCSTR get() const noexcept {
        return redirected ? redirected->c_str() : original;
    }
};

struct RoutedPathW {
    LPCWSTR original{};
    std::optional<std::wstring> redirected;
    [[nodiscard]] LPCWSTR get() const noexcept {
        return redirected ? redirected->c_str() : original;
    }
};

class Hooks {
public:
    explicit Hooks(bool enabled) noexcept;

    [[nodiscard]] RoutedPathA RoutePathA(LPCSTR path) const noexcept;
    [[nodiscard]] RoutedPathW RoutePathW(LPCWSTR path) const noexcept;
    [[nodiscard]] RoutedPathA RoutePathA(
        LPCSTR path, std::string_view current_directory) const noexcept;
    [[nodiscard]] RoutedPathW RoutePathW(
        LPCWSTR path, std::wstring_view current_directory) const noexcept;
    [[nodiscard]] LPCSTR DiskSpaceDirectoryA(LPCSTR path) const noexcept;
    [[nodiscard]] LPCWSTR DiskSpaceDirectoryW(LPCWSTR path) const noexcept;
    [[nodiscard]] bool enabled() const noexcept;

private:
    bool enabled_{};
};

}
~~~

- [ ] **Step 4: Implement exception-contained production routing**

The explicit-current-directory overloads call `RedirectPathA/W` only when enabled and input is non-null. Every overload contains allocation and filesystem exceptions and returns the original path on failure. The production overloads obtain `std::filesystem::current_path()`, catch `filesystem_error` and all other exceptions, log once per failure call, and return the original path. `RoutedPathA/W` owns redirected storage through the synchronous original call, replacing the old thread-local pointer scratch state.

`DiskSpaceDirectoryA/W` returns `nullptr` when enabled and the original value when disabled. Storage-only detours are not requested at all when disabled in Task 10.

- [ ] **Step 5: Build moved storage tests and the still-legacy DLL**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target TestModeStorageRedirectTests iDmacDrv32 && build-msvc32-latest\TestModeStorageRedirectTests.exe'
~~~

Expected: storage policy tests pass and `RfidEmu.cpp` still builds against the moved pure redirector.

- [ ] **Step 6: Commit the storage move**

~~~powershell
git add -- CMakeLists.txt RfidEmu.cpp TestModeStorage tests/TestModeStorage
git commit -m "refactor: separate test-mode storage policy"
~~~

### Task 9: Add a Fixed-Capacity Owned MinHook Transaction

**Files:**
- Create: `Win32Hooks/MinHookTransaction.h`
- Create: `Win32Hooks/MinHookTransaction.cpp`
- Create: `tests/Win32Hooks/Kernel32HookTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Up to 24 fixed `HookRequest` values, resolver functions, and a target-only `MinHookApi`.
- Produces: `std::expected<void, HookInstallError> MinHookTransaction::Install()` with exact failure stage and reverse rollback.

- [ ] **Step 1: Add a failing fake-backend transaction matrix**

Add:

~~~cmake
add_executable(Kernel32HookTests
        Win32Hooks/MinHookTransaction.cpp
        tests/Win32Hooks/Kernel32HookTests.cpp
)
target_include_directories(Kernel32HookTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
)
target_link_libraries(Kernel32HookTests PRIVATE minhook)
add_test(NAME Kernel32HookTests COMMAND Kernel32HookTests)
~~~

The fake records ordered calls and injects failure at every module resolution, export resolution, initialize, create, and enable position. Require:

- `MH_OK` and `MH_ERROR_ALREADY_INITIALIZED` both continue.
- Resolution failure performs no MinHook operation.
- Create failure removes every earlier created target in reverse order.
- Enable failure disables only earlier enabled targets in reverse order, then removes every created target in reverse order.
- The error contains stage, export name, target when known, Win32 error, and MinHook status.
- Success touches each requested target exactly once and no unrelated target.
- No API in the abstraction can express `MH_ALL_HOOKS`.

- [ ] **Step 2: Run the missing transaction test**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target Kernel32HookTests'
~~~

Expected: compilation fails because the owned transaction API is absent.

- [ ] **Step 3: Define fixed request, backend, and error types**

Create `Win32Hooks/MinHookTransaction.h`:

~~~cpp
#pragma once

#include <Windows.h>
#include <MinHook.h>

#include <array>
#include <expected>
#include <span>

namespace gc::win32_hooks {

inline constexpr std::size_t kMaxOwnedKernel32Hooks = 24;

struct HookRequest {
    LPCWSTR module_name{};
    LPCSTR export_name{};
    LPVOID detour{};
    LPVOID* original{};
};

struct ResolverApi {
    HMODULE (WINAPI* get_module_handle_w)(LPCWSTR);
    FARPROC (WINAPI* get_proc_address)(HMODULE, LPCSTR);
};

struct MinHookApi {
    decltype(&MH_Initialize) initialize;
    decltype(&MH_CreateHook) create;
    decltype(&MH_EnableHook) enable;
    decltype(&MH_DisableHook) disable;
    decltype(&MH_RemoveHook) remove;
};

enum class HookInstallStage {
    none,
    too_many_hooks,
    resolve_module,
    resolve_export,
    initialize,
    create,
    enable,
};

struct HookInstallError {
    HookInstallStage stage{};
    LPCSTR export_name{};
    LPVOID target{};
    DWORD win32_error{ERROR_SUCCESS};
    MH_STATUS minhook_status{MH_OK};
};

[[nodiscard]] ResolverApi ProductionResolverApi() noexcept;
[[nodiscard]] MinHookApi ProductionMinHookApi() noexcept;

class MinHookTransaction {
public:
    MinHookTransaction(
        ResolverApi resolver = ProductionResolverApi(),
        MinHookApi minhook = ProductionMinHookApi()) noexcept;

    [[nodiscard]] std::expected<void, HookInstallError> Install(
        std::span<const HookRequest> requests) noexcept;
    void Rollback() noexcept;

private:
    ResolverApi resolver_;
    MinHookApi minhook_;
    std::array<LPVOID, kMaxOwnedKernel32Hooks> created_{};
    std::array<LPVOID, kMaxOwnedKernel32Hooks> enabled_{};
    std::size_t created_count_{};
    std::size_t enabled_count_{};
    bool committed_{};
};

}
~~~

- [ ] **Step 4: Implement resolve-all, create-all, enable-all, or rollback**

Use a local fixed `std::array<ResolvedHook, 24>`. Resolve every export before initializing MinHook. Treat an already-initialized status as success:

~~~cpp
const MH_STATUS init_status = minhook_.initialize();
if (init_status != MH_OK &&
    init_status != MH_ERROR_ALREADY_INITIALIZED) {
    return std::unexpected(HookInstallError{
        .stage = HookInstallStage::initialize,
        .minhook_status = init_status});
}
~~~

Create targets in request order, then enable each exact target in request order. On any failure call `Rollback()` before returning the error. `Rollback()` iterates enabled and created counts backward. On success set `committed_ = true`; process lifetime intentionally owns the enabled hooks until process termination.

- [ ] **Step 5: Run every transaction failure point**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target Kernel32HookTests && build-msvc32-latest\Kernel32HookTests.exe'
~~~

Expected: all resolution, initialization, create, enable, reverse-rollback, already-initialized, and success assertions pass.

- [ ] **Step 6: Commit the transaction slice**

~~~powershell
git add -- CMakeLists.txt Win32Hooks/MinHookTransaction.h Win32Hooks/MinHookTransaction.cpp tests/Win32Hooks/Kernel32HookTests.cpp
git commit -m "refactor: add owned MinHook transaction"
~~~

### Task 10: Add the Sole Kernel32 Hook Adapter

**Files:**
- Create: `Win32Hooks/Kernel32Hooks.h`
- Create: `Win32Hooks/Kernel32Hooks.cpp`
- Modify: `tests/Win32Hooks/Kernel32HookTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `gc::rfid::Runtime`, `gc::testmode_storage::Hooks`, and original Kernel32 function pointers.
- Produces: `Kernel32Hooks::BuildRequests()` plus the only detours for the 14 always-required COM hooks and 10 conditional storage hooks.

- [ ] **Step 1: Add failing request-set, routing, output, and forwarding tests**

Replace the target source list with the complete adapter test closure:

~~~cmake
add_executable(Kernel32HookTests
        Rfid/ComPortState.cpp
        Rfid/Runtime.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Device.cpp
        Rfid/Jvs/Encoder.cpp
        TestModeStorage/Redirector.cpp
        TestModeStorage/Hooks.cpp
        Win32Hooks/Kernel32Hooks.cpp
        Win32Hooks/MinHookTransaction.cpp
        tests/Win32Hooks/Kernel32HookTests.cpp
)
target_include_directories(Kernel32HookTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
target_link_libraries(Kernel32HookTests PRIVATE minhook)
add_test(NAME Kernel32HookTests COMMAND Kernel32HookTests)
~~~

Supply fake original functions that record every argument and return sentinel values. Require:

1. Storage disabled builds exactly these 14 unique exports: `CreateFileA`, `CreateFileW`, `WriteFile`, `ReadFile`, `CloseHandle`, `GetCommModemStatus`, `EscapeCommFunction`, `ClearCommError`, `SetCommMask`, `SetupComm`, `GetCommState`, `SetCommState`, `SetCommTimeouts`, and `GetCommTimeouts`.
2. Storage enabled adds exactly `FindFirstFileA/W`, `CreateDirectoryA/W`, `DeleteFileA/W`, `GetFileAttributesA/W`, and `GetDiskFreeSpaceExA/W` for 24 total requests.
3. `CreateFileA/W("COM2")` starts the worker once and returns only `EmulatedComHandle()` without invoking storage or the original API.
4. Worker failure returns `INVALID_HANDLE_VALUE` and publishes its Win32 error through `GetLastError()`.
5. Enabled matching storage paths are redirected; disabled and nonmatching paths reach the original API unchanged.
6. Null paths are forwarded without dereference.
7. Every non-emulated handle forwards the original pointer values, sizes, flags, and output pointers unchanged and leaves the original function's last error intact.
8. Every emulated synchronous read/write initializes its count to zero before validation and to the exact transfer count on success.
9. Emulated `GetCommState` and `GetCommTimeouts` return complete stored structures; setters are observable through later getters.
10. `ClearCommError` accepts either optional output as null; when provided it writes `*lpErrors = 0` and a fully zero-initialized `COMSTAT` except for exact `cbInQue`.
11. `GetCommModemStatus` exposes `MS_CTS_ON` only after address assignment.
12. Fragmented reads never expose a second frame.
13. Invalid synchronous pointers and overlapped calls fail without exceptions.
14. ANSI and wide storage wrappers keep the routed string alive through the fake original call.

- [ ] **Step 2: Run the adapter tests before its API exists**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target Kernel32HookTests'
~~~

Expected: compilation fails because `Kernel32Hooks` is absent.

- [ ] **Step 3: Define the exact original API and fixed request set**

Create `Win32Hooks/Kernel32Hooks.h`. Use `decltype(&::FunctionName)` for every original function pointer so signatures cannot drift:

~~~cpp
struct OriginalKernel32Api {
    decltype(&::CreateFileA) create_file_a{};
    decltype(&::CreateFileW) create_file_w{};
    decltype(&::WriteFile) write_file{};
    decltype(&::ReadFile) read_file{};
    decltype(&::CloseHandle) close_handle{};
    decltype(&::GetCommModemStatus) get_comm_modem_status{};
    decltype(&::EscapeCommFunction) escape_comm_function{};
    decltype(&::ClearCommError) clear_comm_error{};
    decltype(&::SetCommMask) set_comm_mask{};
    decltype(&::SetupComm) setup_comm{};
    decltype(&::GetCommState) get_comm_state{};
    decltype(&::SetCommState) set_comm_state{};
    decltype(&::SetCommTimeouts) set_comm_timeouts{};
    decltype(&::GetCommTimeouts) get_comm_timeouts{};
    decltype(&::FindFirstFileA) find_first_file_a{};
    decltype(&::FindFirstFileW) find_first_file_w{};
    decltype(&::CreateDirectoryA) create_directory_a{};
    decltype(&::CreateDirectoryW) create_directory_w{};
    decltype(&::DeleteFileA) delete_file_a{};
    decltype(&::DeleteFileW) delete_file_w{};
    decltype(&::GetFileAttributesA) get_file_attributes_a{};
    decltype(&::GetFileAttributesW) get_file_attributes_w{};
    decltype(&::GetDiskFreeSpaceExA) get_disk_free_space_ex_a{};
    decltype(&::GetDiskFreeSpaceExW) get_disk_free_space_ex_w{};
};

struct HookRequestSet {
    std::array<HookRequest, kMaxOwnedKernel32Hooks> storage{};
    std::size_t size{};
    [[nodiscard]] std::span<const HookRequest> requests() const noexcept {
        return {storage.data(), size};
    }
};
~~~

Expose:

~~~cpp
class Kernel32Hooks {
public:
    Kernel32Hooks(
        gc::rfid::Runtime& rfid,
        gc::testmode_storage::Hooks& storage,
        OriginalKernel32Api originals = {}) noexcept;

    void Activate() noexcept;
    void Deactivate() noexcept;
    [[nodiscard]] HookRequestSet BuildRequests(
        bool storage_enabled) noexcept;

    // Instance entry points have the exact Win32 argument lists and are
    // directly callable by tests. Static WINAPI detours delegate to them.
};
~~~

The active adapter pointer is process-lifetime and is set before any detour is enabled. `Deactivate()` is used only when installation rolls back.

- [ ] **Step 4: Implement exact COM2-first routing and synchronous output rules**

For both create APIs:

~~~cpp
if (file_name != nullptr && std::string_view{file_name} == "COM2") {
    const auto opened = rfid_.OpenCom2();
    if (!opened) {
        SetLastError(opened.error());
        return INVALID_HANDLE_VALUE;
    }
    return *opened;
}
const auto routed = storage_.RoutePathA(file_name);
return originals_.create_file_a(
    routed.get(),
    desired_access,
    share_mode,
    security_attributes,
    creation_disposition,
    flags_and_attributes,
    template_file);
~~~

Use the wide equivalent without lossy conversion. For calls whose handle differs from `EmulatedComHandle()`, immediately call the original and perform no operation afterward.

For emulated reads/writes, set the supplied count to zero first. Reject a null count on the synchronous path, a non-null `OVERLAPPED`, or a null buffer with nonzero size using `ERROR_INVALID_PARAMETER`. Translate `ComPortState` expected errors to `SetLastError`/`FALSE`. On success narrow only after proving the count is at most the caller's `DWORD` size.

`GetCommState`, `GetCommTimeouts`, and `GetCommModemStatus` validate their required output pointers, copy complete values, and return `TRUE`. The two `ClearCommError` outputs are independently optional under the Win32 contract; initialize each one when it is non-null:

~~~cpp
if (errors != nullptr) {
    *errors = 0;
}
if (status != nullptr) {
    *status = rfid_.port().CommStatus();
}
return TRUE;
~~~

`CloseHandle(EmulatedComHandle())` calls `Runtime::CloseCom2()` and returns `TRUE`. Storage wrapper methods construct one `RoutedPathA/W` local and invoke the original before that local leaves scope. Correct the old wide find signature by using `LPWIN32_FIND_DATAW` through `decltype(&::FindFirstFileW)`.

- [ ] **Step 5: Build the precise owned request list**

`BuildRequests(false)` emits the 14 COM/shared requests in the order listed in Step 1. `BuildRequests(true)` appends the 10 storage-only requests. Every request uses module `L"kernel32.dll"`, the exact export name, its static detour, and the address of the corresponding field in `OriginalKernel32Api`. Assert the fixed capacity before append; return no partial set if the invariant is violated.

Do not include `MH_ALL_HOOKS`, `MH_QueueEnableHook`, or `MH_ApplyQueued` anywhere in the new adapter.

- [ ] **Step 6: Run hook transaction and adapter tests together**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target Kernel32HookTests && build-msvc32-latest\Kernel32HookTests.exe'
~~~

Expected: the full hook failure matrix, request-set counts, routing order, forwarding, output initialization, COM fragmentation, and storage adapter cases pass.

- [ ] **Step 7: Commit the Kernel32 adapter**

~~~powershell
git add -- CMakeLists.txt Win32Hooks/Kernel32Hooks.h Win32Hooks/Kernel32Hooks.cpp tests/Win32Hooks/Kernel32HookTests.cpp
git commit -m "refactor: centralize RFID Kernel32 hooks"
~~~

### Task 11: Compose the Feature, Cut Over DllMain, and Delete the Monolith

**Files:**
- Create: `Rfid/Feature.h`
- Create: `Rfid/Feature.cpp`
- Modify: `dllmain.cpp`
- Modify: `CMakeLists.txt:142-176`
- Delete: `RfidEmu.h`
- Delete: `RfidEmu.cpp`

**Interfaces:**
- Consumes: `ConfigManager::GetCardReadKey()`, `GetEnableTestModeStorageRedirect()`, `SdlKeycodeToVirtualKey()`, the RFID runtime, storage policy, Kernel32 adapter, and transaction.
- Produces: `std::expected<void, FeatureError> gc::rfid::InitializeFeature()` and direct fail-closed use from `DllMain`.

- [ ] **Step 1: Make the caller expect a fail-closed feature result**

Replace the root RFID include in `dllmain.cpp` with:

~~~cpp
#include "Rfid/Feature.h"
~~~

Replace `RfidEmuInit()` with:

~~~cpp
const auto rfid_result = gc::rfid::InitializeFeature();
if (!rfid_result) {
    PLOG_ERROR
        << "RFID/JVS feature initialization failed at stage "
        << static_cast<int>(rfid_result.error().stage);
    return FALSE;
}
PLOG_DEBUG << "RFID/JVS feature init complete!";
~~~

- [ ] **Step 2: Run the production target and observe the missing composition root**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32'
~~~

Expected: compilation fails because `Rfid/Feature.h` is absent.

- [ ] **Step 3: Define a fixed diagnostic result that cannot throw through DllMain**

Create `Rfid/Feature.h`:

~~~cpp
#pragma once

#include "Win32Hooks/MinHookTransaction.h"

#include <Windows.h>

#include <expected>

namespace gc::rfid {

enum class FeatureFailureStage {
    configuration,
    allocation,
    hook_installation,
};

struct FeatureError {
    FeatureFailureStage stage{};
    DWORD win32_error{ERROR_SUCCESS};
    gc::win32_hooks::HookInstallError hook{};
};

[[nodiscard]] std::expected<void, FeatureError>
InitializeFeature() noexcept;

}
~~~

- [ ] **Step 4: Compose one deliberately process-lifetime feature state**

In `Feature.cpp`, define an internal aggregate in dependency order:

~~~cpp
struct FeatureState {
    FeatureState(int virtual_key, bool storage_enabled)
        : rfid(virtual_key),
          storage(storage_enabled),
          kernel32(rfid, storage) {
    }

    Runtime rfid;
    gc::testmode_storage::Hooks storage;
    gc::win32_hooks::Kernel32Hooks kernel32;
    gc::win32_hooks::MinHookTransaction transaction;
};

FeatureState* g_feature_state = nullptr;
~~~

`InitializeFeature()`:

1. Reads both configuration values and translates the key.
2. Logs an unmappable key as card-scan-disabled while keeping a valid virtual reader.
3. Calls `CreateDirectoryA("OpenParrot", nullptr)`; an already-existing directory is normal, while another error is logged and remains nonfatal to preserve the legacy side effect.
4. Allocates `FeatureState` inside a catch-all boundary.
5. Calls `kernel32.Activate()`.
6. Builds the exact request set from the storage flag.
7. Calls `transaction.Install(requests.requests())`.
8. On failure, deactivates the adapter, returns `hook_installation`, and lets the temporary state roll back.
9. On success, releases ownership into `g_feature_state` so state, originals, and hooks remain valid until process termination.

Do not start the card worker here. Do not call `MH_Initialize`, `MH_CreateHookApi`, or `MH_EnableHook` directly from `Feature.cpp`.

- [ ] **Step 5: Replace the production source list and remove the old files**

In `SOURCES`, remove `RfidEmu.cpp` and the old storage path. Ensure these exact feature sources are present:

~~~cmake
        Rfid/Feature.cpp
        Rfid/ComPortState.cpp
        Rfid/Runtime.cpp
        Rfid/State.cpp
        Rfid/TaitoCommands.cpp
        Rfid/Jvs/Decoder.cpp
        Rfid/Jvs/Device.cpp
        Rfid/Jvs/Encoder.cpp
        TestModeStorage/Redirector.cpp
        TestModeStorage/Hooks.cpp
        Win32Hooks/Kernel32Hooks.cpp
        Win32Hooks/MinHookTransaction.cpp
~~~

Delete the old owner:

~~~powershell
git rm -- RfidEmu.cpp RfidEmu.h
~~~

Do not leave a forwarding declaration, wrapper function, facade source, or CMake alias.

- [ ] **Step 6: Build the direct cutover and run every focused test**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 JvsCodecTests JvsDeviceTests ComPortStateTests RfidRuntimeTests TestModeStorageRedirectTests Kernel32HookTests && build-msvc32-latest\JvsCodecTests.exe && build-msvc32-latest\JvsDeviceTests.exe && build-msvc32-latest\ComPortStateTests.exe && build-msvc32-latest\RfidRuntimeTests.exe && build-msvc32-latest\TestModeStorageRedirectTests.exe && build-msvc32-latest\Kernel32HookTests.exe'
~~~

Expected: the x86 DLL links without `RfidEmu` and all six focused tests exit with code zero.

- [ ] **Step 7: Prove no compatibility facade or legacy owner remains**

~~~powershell
Test-Path RfidEmu.cpp,RfidEmu.h,CardScanState.h,TestModeStorageRedirect.cpp,TestModeStorageRedirect.h
rg -n "RfidEmuInit|jprot_encoder|process_stream|replyBuffer|JVS_STREAM_SIZE|CreateThread|Sleep\(100\)|MH_EnableHook\(MH_ALL_HOOKS\)" --glob "*.cpp" --glob "*.h"
~~~

Expected: every `Test-Path` result is `False` and `rg` emits no match. `std::thread` and `std::chrono::milliseconds{100}` exist only in `Rfid/Runtime.cpp`.

- [ ] **Step 8: Commit the direct feature cutover**

~~~powershell
git add -- CMakeLists.txt dllmain.cpp Rfid/Feature.h Rfid/Feature.cpp
git commit -m "refactor: replace monolithic RFID emulator"
~~~

Before committing, inspect `git diff --cached --name-only` and unstage any file outside the paths named in this task. In particular, do not stage the six pre-existing NESYS/registry modifications listed under Global Constraints.

### Task 12: Verify the Complete Refactor and Hand Off Gameplay Acceptance

**Files:**
- Verify only: all files changed by Tasks 1-11
- Runtime deployment after verification: `H:\gc\iDmacDrv32.dll`

**Interfaces:**
- Consumes: The completed direct-cutover implementation and committed JVS PDF.
- Produces: Focused, full-suite, binary-format, source-hygiene, and deployment evidence followed by user-owned gameplay acceptance.

- [ ] **Step 1: Rebuild production and all focused targets from the x86 environment**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 JvsCodecTests JvsDeviceTests ComPortStateTests RfidRuntimeTests TestModeStorageRedirectTests Kernel32HookTests --clean-first'
~~~

Expected: every target builds successfully with no reference to a removed root RFID file.

- [ ] **Step 2: Run focused tests and the full CTest suite**

~~~powershell
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && build-msvc32-latest\JvsCodecTests.exe && build-msvc32-latest\JvsDeviceTests.exe && build-msvc32-latest\ComPortStateTests.exe && build-msvc32-latest\RfidRuntimeTests.exe && build-msvc32-latest\TestModeStorageRedirectTests.exe && build-msvc32-latest\Kernel32HookTests.exe && ctest --test-dir build-msvc32-latest --output-on-failure'
~~~

Expected: all focused executables return zero and CTest reports 100% passing. If an unrelated dirty-worktree test fails, report its exact target/output and do not claim the suite passed.

- [ ] **Step 3: Verify the normative artifact and x86 DLL**

~~~powershell
(Get-FileHash docs\references\JVST_VER3.pdf -Algorithm SHA256).Hash
& $env:ComSpec /d /s /c '"C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Auxiliary/Build/vcvars32.bat" && dumpbin /headers build-msvc32-latest\iDmacDrv32.dll' | Select-String 'machine \(x86\)'
~~~

Expected: the hash is `E1D4128B21A896C7C299AE5DBC1009B51E11D2E2AEDCC46AA5DD090EA7AB7A88` and `dumpbin` prints one x86 machine line.

- [ ] **Step 4: Audit ownership, capacities, forbidden hooks, and the final diff**

~~~powershell
git diff --check origin/main...HEAD
rg -n "kMaxDecodedAfterCount|kMaxEncodedFrameSize|std::expected|std::span|std::byte" Rfid
rg -n "MH_ALL_HOOKS|MH_CreateHookApi|RfidEmuInit|jprot_encoder|replyBuffer|process_stream" Rfid TestModeStorage Win32Hooks dllmain.cpp CMakeLists.txt
rg -n "Sleep\(|std::this_thread::sleep_for" Rfid/Jvs Rfid/ComPortState.cpp
git status --short
~~~

Expected:

- `git diff --check` emits nothing.
- Modern bounded types appear in their intended modules.
- Forbidden/legacy hook and protocol symbols emit no matches.
- Codec, device, and COM code contain no sleep.
- `git status` contains no implementation residue from this plan; the six pre-existing NESYS/registry modifications may still appear and remain untouched.

- [ ] **Step 5: Review the final commits without including unrelated changes**

~~~powershell
git log --oneline --decorate -12
git diff --stat 9358d88..HEAD
git diff --name-only 9358d88..HEAD
~~~

Expected: commits are divided by the task slices above, all planned modules/tests are present, legacy files are deleted, and no NESYS/registry file appears in the implementation commit range.

- [ ] **Step 6: Ensure the game is not running before deployment**

~~~powershell
Get-Process game471 -ErrorAction SilentlyContinue
~~~

Expected: no output. If the game is running, stop and ask the user to exit it; do not terminate it automatically.

- [ ] **Step 7: Deploy only the verified loader and compare hashes**

~~~powershell
Copy-Item -LiteralPath build-msvc32-latest\iDmacDrv32.dll -Destination H:\gc\iDmacDrv32.dll -Force
Get-FileHash build-msvc32-latest\iDmacDrv32.dll,H:\gc\iDmacDrv32.dll -Algorithm SHA256
~~~

Expected: the build and deployed SHA-256 values match.

- [ ] **Step 8: Hand off manual gameplay acceptance without claiming it**

Report automated evidence separately, then ask the user to launch `game471.exe` and confirm:

1. The game boots through the emulated `COM2` device; any required hook failure aborts attachment instead of producing a partial reader.
2. Reset and address discovery complete, including the `MS_CTS_ON` transition.
3. The observed `GetCommState`, `SetCommState`, mask, timeout, `ClearCommError`, and one-byte empty-read sequence remains accepted.
4. Standard and Taito commands remain accepted with no serial timeout or startup hang.
5. One card-key press is observed exactly once; holding does not repeat; a later press works again.
6. Coin, switch, service, reader, and general-purpose I/O behavior is unchanged.
7. Test-mode storage works when enabled and ordinary filesystem paths remain unchanged.
8. Storage-disabled boot does not install or exercise storage-only hooks.
9. The currently bypassed custom dongle check remains bypassed; this acceptance does not claim dongle emulation.

The refactor is complete only after automated evidence passes and the user reports this gameplay acceptance.
