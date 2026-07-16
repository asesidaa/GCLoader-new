#include "Rfid/ComPortState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::rfid::ComPortState;
using gc::rfid::jvs::Address;
using gc::rfid::jvs::EncodedFrame;

int expect(bool condition, std::string_view name)
{
    if (condition) {
        return 0;
    }
    std::cerr << name << " failed\n";
    return 1;
}

EncodedFrame encode_request(
    Address address,
    std::initializer_list<std::uint8_t> payload)
{
    const auto encoded = gc::rfid::jvs::EncodePacket(
        address,
        std::span<const std::uint8_t>{payload.begin(), payload.size()});
    if (!encoded) {
        std::terminate();
    }
    return *encoded;
}

std::vector<std::byte> drain(
    ComPortState& port,
    std::size_t fragment_size = 64)
{
    std::vector<std::byte> bytes;
    std::array<std::byte, 64> storage{};
    while (port.PendingByteCount() != 0) {
        const auto requested = std::min(fragment_size, storage.size());
        const auto result = port.Read(
            std::span<std::byte>{storage}.first(requested), false);
        if (!result) {
            std::terminate();
        }
        bytes.insert(bytes.end(), storage.begin(), storage.begin() + *result);
    }
    return bytes;
}

int expect_frame(
    std::span<const std::byte> actual,
    const EncodedFrame& expected,
    std::string_view name)
{
    return expect(std::ranges::equal(actual, expected.bytes()), name);
}

int assign_and_drain(ComPortState& port, std::uint8_t address = 0x01)
{
    const auto request = encode_request(
        gc::rfid::jvs::address::broadcast,
        {gc::rfid::jvs::command::set_address.value, address});
    const auto write = port.Write(request.bytes(), false);
    int failures = expect(write && *write == request.size,
                          "address write count");
    const auto response = drain(port);
    const auto expected = encode_request(
        gc::rfid::jvs::address::master, {0x01, 0x01});
    failures += expect_frame(response, expected, "address response");
    return failures;
}

int test_serial_configuration()
{
    int failures = 0;
    ComPortState port;

    failures += expect(
        port.SetupComm(0x204, 0x204).has_value(),
        "SetupComm 0x204 queues");
    failures += expect(
        port.QueueSizes() == std::pair<DWORD, DWORD>{0x204, 0x204},
        "queue sizes round trip");

    auto dcb = port.GetCommState();
    failures += expect(
        dcb.DCBlength == sizeof(DCB) &&
            dcb.BaudRate == CBR_115200 && dcb.fBinary == TRUE &&
            dcb.ByteSize == 8 && dcb.Parity == NOPARITY &&
            dcb.StopBits == ONESTOPBIT,
        "default DCB is initialized 115200 8N1");
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.XonChar = 0x22;
    failures += expect(port.SetCommState(dcb).has_value(),
                       "SetCommState 8N1");
    const auto stored_dcb = port.GetCommState();
    failures += expect(
        std::memcmp(&stored_dcb, &dcb, sizeof(DCB)) == 0,
        "GetCommState returns exact stored DCB");

    failures += expect(port.SetCommMask(1).has_value(),
                       "SetCommMask one");
    failures += expect(port.GetCommMask() == 1,
                       "GetCommMask round trip");

    auto timeouts = port.GetCommTimeouts();
    timeouts.ReadIntervalTimeout = 7;
    timeouts.ReadTotalTimeoutMultiplier = 3;
    timeouts.ReadTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 5;
    timeouts.WriteTotalTimeoutConstant = 11;
    failures += expect(port.SetCommTimeouts(timeouts).has_value(),
                       "SetCommTimeouts 20 ms");
    const auto stored_timeouts = port.GetCommTimeouts();
    failures += expect(
        std::memcmp(
            &stored_timeouts, &timeouts, sizeof(COMMTIMEOUTS)) == 0,
        "GetCommTimeouts returns exact stored timeouts");

    failures += expect(port.SetupComm(123, 456).has_value(),
                       "SetupComm accepts arbitrary queue sizes");
    failures += expect(
        port.QueueSizes() == std::pair<DWORD, DWORD>{123, 456},
        "arbitrary queue sizes round trip");

    const auto before_invalid = port.GetCommState();
    auto invalid = before_invalid;
    invalid.DCBlength = 0;
    const auto invalid_length = port.SetCommState(invalid);
    failures += expect(
        !invalid_length && invalid_length.error() == ERROR_INVALID_PARAMETER,
        "invalid DCB length is deterministic");

    invalid = before_invalid;
    invalid.ByteSize = 7;
    failures += expect(!port.SetCommState(invalid), "reject seven data bits");
    invalid = before_invalid;
    invalid.Parity = EVENPARITY;
    failures += expect(!port.SetCommState(invalid), "reject parity");
    invalid = before_invalid;
    invalid.StopBits = TWOSTOPBITS;
    failures += expect(!port.SetCommState(invalid), "reject two stop bits");
    const auto after_invalid = port.GetCommState();
    failures += expect(
        std::memcmp(&before_invalid, &after_invalid, sizeof(DCB)) == 0,
        "invalid DCB writes do not mutate state");
    return failures;
}

int test_fragmented_requests()
{
    using namespace gc::rfid::jvs;

    int failures = 0;
    const auto request = encode_request(
        address::broadcast, {command::set_address.value, 0x01});
    const auto expected = encode_request(address::master, {0x01, 0x01});

    for (std::size_t split = 0; split <= request.bytes().size(); ++split) {
        ComPortState port;
        const auto first = port.Write(request.bytes().first(split), false);
        failures += expect(first && *first == split,
                           "first fragmented write count");
        failures += expect(
            split == request.bytes().size()
                ? port.PendingByteCount() == expected.size
                : port.PendingByteCount() == 0,
            "reply appears only after complete packet");

        const auto second = port.Write(request.bytes().subspan(split), false);
        failures += expect(
            second && *second == request.bytes().size() - split,
            "second fragmented write count");
        failures += expect(port.PendingByteCount() == expected.size,
                           "complete fragmented request produces reply");
        failures += expect_frame(
            drain(port), expected, "fragmented request exact response");
    }
    return failures;
}

int test_host_board_address_dispatch()
{
    using namespace gc::rfid::jvs;

    int failures = 0;
    ComPortState port;
    const auto request = encode_request(
        address::master, {0x01, 0x02});
    const auto write = port.Write(request.bytes(), false);
    failures += expect(
        write && *write == request.size,
        "host-board Taito request write count");

    constexpr std::array expected{
        std::byte{0xE0}, // SYNC
        std::byte{0x00}, // destination: master
        std::byte{0x04}, // status + report + data + SUM
        std::byte{0x01}, // packet status: normal
        std::byte{0x01}, // command report: normal
        std::byte{0x01}, // Taito command 0x01 response data
        std::byte{0x07}}; // SUM
    failures += expect(
        std::ranges::equal(drain(port), expected),
        "host-board Taito request preserves original reply frame");
    return failures;
}

int test_combined_rfid_poll_preserves_original_reply()
{
    using namespace gc::rfid::jvs;

    int failures = 0;
    ComPortState port;
    failures += assign_and_drain(port);

    const auto request = encode_request(
        Address{0x01}, {0x26, 0x01, 0x61, 0x32, 0x01, 0x00});
    const auto write = port.Write(request.bytes(), false);
    failures += expect(
        write && *write == request.size,
        "combined RFID poll write count");

    std::array<std::byte, 33> expected{};
    expected[0] = std::byte{0xE0};  // SYNC
    expected[1] = std::byte{0x00};  // destination: master
    expected[2] = std::byte{0x1E};  // bytes through SUM
    expected[3] = std::byte{0x01};  // packet status: normal
    expected[4] = std::byte{0x01};  // command 0x26 report: normal
    expected[5] = std::byte{0x00};  // misc. input: card absent
    expected[6] = std::byte{0x01};  // command 0x32 report: normal
    // expected[7..30] is the empty 24-byte RFID card payload.
    expected[31] = std::byte{0x01}; // RFID transfer terminal value
    expected[32] = std::byte{0x22}; // SUM
    failures += expect(
        std::ranges::equal(drain(port), expected),
        "combined RFID poll preserves original reply bytes");
    return failures;
}

int test_pending_reply_and_reads()
{
    using namespace gc::rfid::jvs;

    int failures = 0;
    ComPortState port;
    failures += assign_and_drain(port);

    const auto revision_request = encode_request(Address{0x01}, {0x11});
    const auto revision_response =
        encode_request(address::master, {0x01, 0x01, 0x13});
    const auto write = port.Write(revision_request.bytes(), false);
    failures += expect(write && *write == revision_request.size,
                       "revision write count");
    failures += expect(port.PendingByteCount() == revision_response.size,
                       "pending count after reply");
    const auto pending_status = port.CommStatus();
    COMSTAT expected_pending_status{};
    expected_pending_status.cbInQue = revision_response.size;
    failures += expect(
        std::memcmp(
            &pending_status, &expected_pending_status, sizeof(COMSTAT)) == 0,
        "pending COMSTAT is fully initialized");

    std::vector<std::byte> bytewise;
    std::array<std::byte, 1> one{};
    while (port.PendingByteCount() != 0) {
        const auto before = port.PendingByteCount();
        const auto read = port.Read(one, false);
        failures += expect(read && *read == 1, "one-byte read count");
        bytewise.push_back(one.front());
        failures += expect(port.PendingByteCount() == before - 1,
                           "pending count follows read cursor");
    }
    failures += expect_frame(
        bytewise, revision_response, "one-byte reads preserve reply");

    std::array<std::byte, 8> empty_destination{};
    const auto empty_read = port.Read(empty_destination, false);
    failures += expect(empty_read && *empty_read == 0,
                       "empty port read returns immediately");

    const auto status = port.CommStatus();
    const COMSTAT expected_status{};
    failures += expect(
        std::memcmp(&status, &expected_status, sizeof(COMSTAT)) == 0,
        "empty COMSTAT is fully initialized");
    return failures;
}

int test_pipeline_rejection()
{
    using namespace gc::rfid::jvs;

    int failures = 0;
    ComPortState port;
    failures += assign_and_drain(port);

    const auto first = encode_request(Address{0x01}, {0x11});
    const auto second = encode_request(Address{0x01}, {0x12});
    std::vector<std::byte> pipelined;
    pipelined.insert(pipelined.end(), first.bytes().begin(), first.bytes().end());
    pipelined.insert(
        pipelined.end(), second.bytes().begin(), second.bytes().end());

    const auto write = port.Write(pipelined, false);
    failures += expect(write && *write == pipelined.size(),
                       "pipelined write accepts all raw bytes");
    failures += expect(port.SequencingViolationCount() == 1,
                       "second pending reply is diagnosed");

    const auto expected_first =
        encode_request(address::master, {0x01, 0x01, 0x13});
    failures += expect_frame(
        drain(port), expected_first,
        "pipelined request cannot alter existing reply");

    const auto later_write = port.Write(second.bytes(), false);
    failures += expect(later_write && *later_write == second.size,
                       "later request accepted after drain");
    const auto expected_second =
        encode_request(address::master, {0x01, 0x01, 0x30});
    failures += expect_frame(
        drain(port), expected_second,
        "later reply is separate and not coalesced");
    return failures;
}

int test_retransmission_and_checksum()
{
    using namespace gc::rfid::jvs;

    int failures = 0;
    ComPortState port;
    failures += assign_and_drain(port);

    const auto revision = encode_request(Address{0x01}, {0x11});
    static_cast<void>(port.Write(revision.bytes(), false));
    const auto original = drain(port);

    const auto retransmit = encode_request(Address{0x01}, {0x2F});
    const auto retransmit_write = port.Write(retransmit.bytes(), false);
    failures += expect(
        retransmit_write && *retransmit_write == retransmit.size,
        "retransmission request write count");
    failures += expect(std::ranges::equal(drain(port), original),
                       "retransmission is byte-identical");

    ComPortState no_history;
    no_history.device_state().assigned_address = Address{0x01};
    static_cast<void>(no_history.Write(retransmit.bytes(), false));
    failures += expect(no_history.PendingByteCount() == 0,
                       "retransmission without history is silent");

    auto corrupt = encode_request(Address{0x01}, {0x11});
    corrupt.storage[corrupt.size - 1] ^= std::byte{0x01};
    const auto corrupt_write = port.Write(corrupt.bytes(), false);
    failures += expect(corrupt_write && *corrupt_write == corrupt.size,
                       "checksum failure write count");
    const auto checksum_error = encode_request(address::master, {0x03});
    failures += expect_frame(
        drain(port), checksum_error, "checksum failure acknowledgement");
    return failures;
}

int test_modem_escape_overlap_and_close()
{
    using namespace gc::rfid::jvs;

    int failures = 0;
    ComPortState port;
    failures += expect(port.ModemStatus() == 0,
                       "modem status before address assignment");
    failures += assign_and_drain(port);
    failures += expect(port.ModemStatus() == MS_CTS_ON,
                       "modem status after address assignment");

    failures += expect(port.EscapeCommFunction(SETDTR).has_value(),
                       "SETDTR");
    failures += expect(port.EscapeCommFunction(SETRTS).has_value(),
                       "SETRTS");
    failures += expect(port.EscapeCommFunction(SETXOFF).has_value(),
                       "SETXOFF");
    failures += expect(port.EscapeCommFunction(SETBREAK).has_value(),
                       "SETBREAK");
    failures += expect(
        port.GetLineState() == gc::rfid::LineState{
            .dtr = true, .rts = true, .xoff = true,
            .break_active = true},
        "line state set");
    failures += expect(port.EscapeCommFunction(CLRDTR).has_value(),
                       "CLRDTR");
    failures += expect(port.EscapeCommFunction(CLRRTS).has_value(),
                       "CLRRTS");
    failures += expect(port.EscapeCommFunction(SETXON).has_value(),
                       "SETXON");
    failures += expect(port.EscapeCommFunction(CLRBREAK).has_value(),
                       "CLRBREAK");
    failures += expect(port.GetLineState() == gc::rfid::LineState{},
                       "line state cleared");
    const auto invalid_escape = port.EscapeCommFunction(0xFFFF);
    failures += expect(
        !invalid_escape && invalid_escape.error() == ERROR_INVALID_FUNCTION,
        "invalid escape function is deterministic");

    const auto request = encode_request(Address{0x01}, {0x11});
    const auto overlap_write = port.Write(request.bytes(), true);
    failures += expect(
        !overlap_write && overlap_write.error() == ERROR_INVALID_PARAMETER,
        "overlapped write is rejected");
    failures += expect(port.PendingByteCount() == 0,
                       "rejected overlapped write does not mutate decoder");

    static_cast<void>(port.Write(request.bytes(), false));
    const auto pending_before_overlap = port.PendingByteCount();
    std::array<std::byte, 8> destination{};
    const auto overlap_read = port.Read(destination, true);
    failures += expect(
        !overlap_read && overlap_read.error() == ERROR_INVALID_PARAMETER,
        "overlapped read is rejected");
    failures += expect(port.PendingByteCount() == pending_before_overlap,
                       "rejected overlapped read preserves reply");

    port.device_state().card_scan.Arm();
    port.Open();
    failures += expect(port.IsOpen(), "port opens");
    port.Close();
    failures += expect(!port.IsOpen(), "port closes");
    failures += expect(port.PendingByteCount() == 0,
                       "close clears pending reply");
    failures += expect(!port.device_state().assigned_address,
                       "close clears JVS address");
    failures += expect(port.device_state().card_scan.IsPresent(),
                       "close preserves process-lifetime card state");
    failures += expect(
        port.QueueSizes() == std::pair<DWORD, DWORD>{} &&
            port.GetCommMask() == 0 &&
            port.GetLineState() == gc::rfid::LineState{},
        "close resets serial configuration state");

    port.Open();
    port.device_state().assigned_address = Address{0x01};
    const auto retransmit = encode_request(Address{0x01}, {0x2F});
    static_cast<void>(port.Write(retransmit.bytes(), false));
    failures += expect(port.PendingByteCount() == 0,
                       "close clears retransmission history");

    ComPortState partial;
    const auto assignment = encode_request(
        address::broadcast, {command::set_address.value, 0x01});
    const auto split = assignment.bytes().size() / 2;
    static_cast<void>(partial.Write(assignment.bytes().first(split), false));
    partial.Close();
    partial.Open();
    static_cast<void>(partial.Write(assignment.bytes().subspan(split), false));
    failures += expect(
        partial.PendingByteCount() == 0 &&
            !partial.device_state().assigned_address,
        "close clears an incomplete decoder frame");
    return failures;
}

} // namespace

int main()
{
    const int failures =
        test_serial_configuration() +
        test_fragmented_requests() +
        test_host_board_address_dispatch() +
        test_combined_rfid_poll_preserves_original_reply() +
        test_pending_reply_and_reads() +
        test_pipeline_rejection() +
        test_retransmission_and_checksum() +
        test_modem_escape_overlap_and_close();
    return failures == 0 ? 0 : 1;
}
