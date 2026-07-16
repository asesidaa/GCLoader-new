#include "Rfid/Jvs/Device.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <numeric>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::rfid::jvs::Acknowledgement;
using gc::rfid::jvs::Address;
using gc::rfid::jvs::DecodedPacket;
using gc::rfid::jvs::DeviceResponse;

DecodedPacket packet(
    Address address,
    std::initializer_list<std::uint8_t> payload)
{
    DecodedPacket result;
    result.address = address;
    result.byte_count = static_cast<std::uint8_t>(payload.size() + 1);
    std::ranges::copy(payload, result.after_count.begin());

    auto checksum = static_cast<std::uint8_t>(address.value + result.byte_count);
    for (const auto value : payload) {
        checksum = static_cast<std::uint8_t>(checksum + value);
    }
    result.after_count[payload.size()] = checksum;
    return result;
}

int expect(bool condition, std::string_view name)
{
    if (condition) {
        return 0;
    }

    std::cerr << name << " failed\n";
    return 1;
}

int expect_acknowledgement(
    const std::optional<DeviceResponse>& response,
    std::span<const std::uint8_t> expected,
    std::string_view name)
{
    if (!response) {
        std::cerr << name << ": expected acknowledgement, got none\n";
        return 1;
    }

    const auto* acknowledgement = std::get_if<Acknowledgement>(&*response);
    if (acknowledgement == nullptr) {
        std::cerr << name << ": expected acknowledgement, got retransmission\n";
        return 1;
    }

    if (std::ranges::equal(acknowledgement->bytes(), expected)) {
        return 0;
    }

    std::cerr << name << ": acknowledgement bytes differ\n";
    return 1;
}

int expect_acknowledgement(
    const std::optional<DeviceResponse>& response,
    std::initializer_list<std::uint8_t> expected,
    std::string_view name)
{
    return expect_acknowledgement(
        response,
        std::span<const std::uint8_t>{expected.begin(), expected.size()},
        name);
}

int test_addressing_and_reset()
{
    using namespace gc::rfid;
    using namespace gc::rfid::jvs;

    int failures = 0;
    State state;
    Device device{state};

    failures += expect(
        !device.HandlePacket(packet(Address{0x01}, {0x11})),
        "unassigned device ignores normal packet");
    failures += expect(
        !device.HandlePacket(packet(address::broadcast, {0x11})),
        "unassigned device ignores broadcast normal command");

    auto response = device.HandlePacket(
        packet(address::broadcast, {command::set_address.value, 0x80}));
    failures += expect_acknowledgement(
        response, {status::ok.value, report::ok.value},
        "set custom address");
    failures += expect(
        state.assigned_address == Address{0x80},
        "custom address is retained");

    failures += expect(
        !device.HandlePacket(packet(Address{0x01}, {0x11})),
        "different assigned address is ignored");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x80}, {0x11})),
        {0x01, 0x01, 0x13},
        "custom assigned address routes commands");

    state.coins = {12, 34};
    state.card_scan.Arm();
    response = device.HandlePacket(
        packet(address::broadcast, {command::reset.value, 0xD9}));
    failures += expect(!response, "reset has no acknowledgement");
    failures += expect(!state.assigned_address, "reset clears address");
    failures += expect(state.coins == std::array<std::uint16_t, 2>{},
                       "reset clears coins");
    failures += expect(state.card_scan.IsPresent(),
                       "reset preserves card presence");

    state.assigned_address = Address{0x80};
    state.coins = {7, 9};
    response = device.HandlePacket(
        packet(Address{0x80}, {command::reset.value, 0xD9}));
    failures += expect_acknowledgement(
        response, {status::unknown_command.value},
        "addressed reset is not accepted");
    failures += expect(
        state.assigned_address == Address{0x80} &&
            state.coins == std::array<std::uint16_t, 2>{7, 9},
        "addressed reset does not mutate state");

    response = device.HandlePacket(
        packet(Address{0x80}, {command::set_address.value, 0x44}));
    failures += expect_acknowledgement(
        response, {status::unknown_command.value},
        "addressed assignment is not accepted");
    failures += expect(state.assigned_address == Address{0x80},
                       "addressed assignment does not mutate state");
    return failures;
}

int test_identity_and_capabilities()
{
    using namespace gc::rfid;
    using namespace gc::rfid::jvs;

    int failures = 0;
    State state;
    state.assigned_address = Address{0x01};
    Device device{state};

    constexpr std::string_view id =
        "TAITO CORP.;RFID CTRL P.C.B.;Ver1.00;";
    std::vector<std::uint8_t> expected_id{0x01, 0x01};
    std::ranges::transform(
        id, std::back_inserter(expected_id),
        [](char value) { return static_cast<std::uint8_t>(value); });
    expected_id.push_back(0x00);

    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x10})),
        expected_id,
        "RFID identifier");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x11})),
        {0x01, 0x01, 0x13},
        "command revision");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x12})),
        {0x01, 0x01, 0x30},
        "JVS revision");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x13})),
        {0x01, 0x01, 0x10},
        "communication revision");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x14})),
        {0x01, 0x01, 0x01, 0x07, 0x00, 0x08,
         0x00, 0x12, 0x08, 0x00, 0x00, 0x00},
        "legacy RFID feature bytes");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x11, 0x12, 0x13})),
        {0x01, 0x01, 0x13, 0x01, 0x30, 0x01, 0x10},
        "multiple standard commands preserve order");
    return failures;
}

int test_inputs_and_coins()
{
    using namespace gc::rfid;
    using namespace gc::rfid::jvs;

    int failures = 0;
    State state;
    state.assigned_address = Address{0x01};
    Device device{state};

    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x20, 0x02, 0x02})),
        {0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
        "switch input");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x20, 0x03, 0x02})),
        {0x01, 0x02},
        "switch player limit");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x20, 0x02, 0x03})),
        {0x01, 0x02},
        "switch byte limit");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x20, 0x02})),
        {0x01, 0x02},
        "truncated switch input");

    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x21, 0x02})),
        {0x01, 0x01, 0x00, 0x00, 0x00, 0x00},
        "initial coin input");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x26, 0x03})),
        {0x01, 0x01, 0x00, 0x00, 0x00},
        "general input without card");

    state.card_scan.Arm();
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x26, 0x03})),
        {0x01, 0x01, 0x19, 0x19, 0x19},
        "general input with card");
    failures += expect(state.card_scan.IsPresent(),
                       "general input does not consume card");

    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x31, 0x01, 0x00, 0x05})),
        {0x01, 0x01},
        "increase P1 coins");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x21, 0x02})),
        {0x01, 0x01, 0x00, 0x05, 0x00, 0x00},
        "read increased P1 coins");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x30, 0x01, 0x00, 0x02})),
        {0x01, 0x01},
        "decrease P1 coins");
    failures += expect(state.coins[0] == 3, "P1 decrease result");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x30, 0x01, 0x00, 0x09})),
        {0x01, 0x01},
        "saturating P1 decrease");
    failures += expect(state.coins[0] == 0, "P1 decrease saturates at zero");

    state.coins[0] = 0x3FFE;
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x31, 0x01, 0x00, 0x10})),
        {0x01, 0x01},
        "saturating P1 increase");
    failures += expect(state.coins[0] == 0x3FFF,
                       "P1 increase saturates at JVS 14-bit maximum");

    const auto coins_before_invalid = state.coins;
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x31, 0x00, 0x00, 0x05})),
        {0x01, 0x03},
        "invalid payout slot");
    failures += expect(state.coins == coins_before_invalid,
                       "invalid payout does not mutate coins");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x30, 0x03, 0x00, 0x05})),
        {0x01, 0x03},
        "invalid decrease slot");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x31, 0x01, 0x00})),
        {0x01, 0x02},
        "truncated payout parameters");
    return failures;
}

int test_card_output_and_overflow()
{
    using namespace gc::rfid;
    using namespace gc::rfid::jvs;

    int failures = 0;
    State state;
    state.assigned_address = Address{0x01};
    Device device{state};

    state.card_scan.Arm();
    std::vector<std::uint8_t> expected_card{0x01, 0x01};
    expected_card.insert(
        expected_card.end(), kCardData.begin(), kCardData.end());
    expected_card.push_back(0x01);
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x32, 0x01, 0x00})),
        expected_card,
        "card payload");
    failures += expect(!state.card_scan.IsPresent(),
                       "card payload consumes card");

    std::vector<std::uint8_t> expected_empty{0x01, 0x01};
    expected_empty.resize(expected_empty.size() + kCardData.size(), 0x00);
    expected_empty.push_back(0x01);
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x32, 0x01, 0x00})),
        expected_empty,
        "empty card payload");
    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x32, 0x02, 0x00})),
        {0x01, 0x03},
        "truncated general output");

    failures += expect_acknowledgement(
        device.HandlePacket(packet(
            Address{0x01},
            {0x32, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})),
        {status::acknowledgement_overflow.value},
        "oversized card reply");
    return failures;
}

int test_errors_and_retransmission()
{
    using namespace gc::rfid;
    using namespace gc::rfid::jvs;

    int failures = 0;
    State state;
    state.assigned_address = Address{0x01};
    Device device{state};

    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x11, 0x99, 0x12})),
        {0x02, 0x01, 0x13},
        "unknown command retains earlier report and stops");

    const auto retransmission =
        device.HandlePacket(packet(Address{0x01}, {0x2F}));
    failures += expect(
        retransmission &&
            std::holds_alternative<RetransmitPrevious>(*retransmission),
        "retransmission requests previous full acknowledgement");

    failures += expect_acknowledgement(
        device.HandlePacket(packet(Address{0x01}, {0x11, 0x2F})),
        {0x02, 0x01, 0x13},
        "retransmission command must be standalone");

    const ChecksumFailure addressed_failure{
        .address = Address{0x01},
        .byte_count = 2,
        .expected = 0x10,
        .actual = 0x11};
    const auto checksum_response =
        device.HandleChecksumFailure(addressed_failure);
    failures += expect(
        checksum_response &&
            std::ranges::equal(
                checksum_response->bytes(),
                std::initializer_list<std::uint8_t>{0x03}),
        "addressed checksum failure");

    const ChecksumFailure other_failure{
        .address = Address{0x02},
        .byte_count = 2,
        .expected = 0x10,
        .actual = 0x11};
    failures += expect(
        !device.HandleChecksumFailure(other_failure),
        "checksum failure for another address is ignored");
    return failures;
}

} // namespace

int main()
{
    const int failures =
        test_addressing_and_reset() +
        test_identity_and_capabilities() +
        test_inputs_and_coins() +
        test_card_output_and_overflow() +
        test_errors_and_retransmission();
    return failures == 0 ? 0 : 1;
}
