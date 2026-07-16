#include "Rfid/Jvs/Decoder.h"
#include "Rfid/Jvs/Encoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace {

using gc::rfid::jvs::Address;
using gc::rfid::jvs::ChecksumFailure;
using gc::rfid::jvs::DecodeEvent;
using gc::rfid::jvs::DecodedPacket;
using gc::rfid::jvs::Decoder;
using gc::rfid::jvs::FramingError;

constexpr std::array kPage16Encoded{
    std::byte{0xE0}, std::byte{0xFF}, std::byte{0x05},
    std::byte{0xD0}, std::byte{0xCF}, std::byte{0x00},
    std::byte{0xD0}, std::byte{0xDF}, std::byte{0x00},
    std::byte{0xB4}};

int expect(bool condition, const char* name)
{
    if (condition) {
        return 0;
    }
    std::cerr << name << " failed\n";
    return 1;
}

void consume(
    Decoder& decoder,
    std::span<const std::byte> bytes,
    std::vector<DecodeEvent>& events)
{
    decoder.Consume(bytes, [&events](DecodeEvent event) {
        events.push_back(std::move(event));
    });
}

int expect_page16_packet(
    const std::vector<DecodeEvent>& events,
    const char* name)
{
    if (events.size() != 1 ||
        !std::holds_alternative<DecodedPacket>(events.front())) {
        std::cerr << name << ": expected one decoded packet\n";
        return 1;
    }

    const auto& packet = std::get<DecodedPacket>(events.front());
    constexpr std::array<std::uint8_t, 4> expected_payload{
        0xD0, 0x00, 0xE0, 0x00};
    const bool payload_matches =
        packet.payload().size() == expected_payload.size() &&
        std::equal(
            packet.payload().begin(),
            packet.payload().end(),
            expected_payload.begin());
    const auto checksum = packet.checksum();
    if (packet.address != Address{0xFF} ||
        packet.byte_count != 0x05 ||
        !payload_matches ||
        !checksum || *checksum != 0xB4) {
        std::cerr << name << ": decoded fields differ\n";
        return 1;
    }
    return 0;
}

int expect_round_trip(
    Address address,
    std::span<const std::uint8_t> payload,
    const char* name)
{
    const auto encoded = gc::rfid::jvs::EncodePacket(address, payload);
    if (!encoded) {
        std::cerr << name << ": encoding failed\n";
        return 1;
    }
    if (encoded->size > gc::rfid::jvs::kMaxEncodedFrameSize) {
        std::cerr << name << ": encoded size exceeded capacity\n";
        return 1;
    }

    int failures = 0;
    for (std::size_t split = 0; split <= encoded->size; ++split) {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        consume(decoder, encoded->bytes().first(split), events);
        consume(decoder, encoded->bytes().subspan(split), events);
        if (events.size() != 1 ||
            !std::holds_alternative<DecodedPacket>(events.front())) {
            ++failures;
            continue;
        }
        const auto& packet = std::get<DecodedPacket>(events.front());
        if (packet.address != address ||
            !std::ranges::equal(packet.payload(), payload)) {
            ++failures;
        }
    }

    if (failures != 0) {
        std::cerr << name << ": " << failures
                  << " split round trips failed\n";
        return 1;
    }
    return 0;
}

} // namespace

int main()
{
    static_assert(Address{0x80}.value == 0x80);
    static_assert(gc::rfid::jvs::CommandId{0xFE}.value == 0xFE);
    static_assert(gc::rfid::jvs::kMaxDecodedAfterCount == 255);
    static_assert(gc::rfid::jvs::kMaxEncodedFrameSize == 515);
    static_assert(!Address{0x00}.is_standard_slave());
    static_assert(Address{0x01}.is_standard_slave());
    static_assert(!Address{0x20}.is_standard_slave());
    static_assert(Address{0xFF}.is_broadcast());

    int failures = 0;

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        constexpr std::array noise{
            std::byte{0x00}, std::byte{0xD0}, std::byte{0xCF}};
        consume(decoder, noise, events);
        failures += expect(events.empty(), "noise before sync");
    }

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        consume(decoder, kPage16Encoded, events);
        failures += expect_page16_packet(events, "PDF page 16 frame");
    }

    for (std::size_t split = 0; split <= kPage16Encoded.size(); ++split) {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        consume(
            decoder,
            {kPage16Encoded.data(), split},
            events);
        consume(
            decoder,
            {kPage16Encoded.data() + split,
             kPage16Encoded.size() - split},
            events);
        failures += expect_page16_packet(events, "every chunk split");
    }

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        constexpr std::array first{
            std::byte{0xE0}, std::byte{0x01}, std::byte{0x02},
            std::byte{0xD0}};
        constexpr std::array second{
            std::byte{0xCF}, std::byte{0xD3}};
        consume(decoder, first, events);
        failures += expect(events.empty(), "split marker remains pending");
        consume(decoder, second, events);
        failures += expect(
            events.size() == 1 &&
                std::holds_alternative<DecodedPacket>(events.front()),
            "split marker completes packet");
    }

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        std::array<std::byte, kPage16Encoded.size() * 2> frames{};
        std::copy(kPage16Encoded.begin(), kPage16Encoded.end(), frames.begin());
        std::copy(
            kPage16Encoded.begin(),
            kPage16Encoded.end(),
            frames.begin() + kPage16Encoded.size());
        consume(decoder, frames, events);
        failures += expect(
            events.size() == 2 &&
                std::holds_alternative<DecodedPacket>(events[0]) &&
                std::holds_alternative<DecodedPacket>(events[1]),
            "two packets in one chunk");
    }

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        constexpr std::array partial{
            std::byte{0xE0}, std::byte{0x01}, std::byte{0x04},
            std::byte{0x10}};
        consume(decoder, partial, events);
        consume(decoder, kPage16Encoded, events);
        failures += expect_page16_packet(events, "raw sync resynchronizes");
    }

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        constexpr std::array invalid_escape{
            std::byte{0xE0}, std::byte{0x01}, std::byte{0x02},
            std::byte{0xD0}, std::byte{0x00}};
        consume(decoder, invalid_escape, events);
        failures += expect(
            events.size() == 1 &&
                std::holds_alternative<FramingError>(events.front()) &&
                std::get<FramingError>(events.front()) ==
                    FramingError::InvalidEscape,
            "invalid escape event");
        events.clear();
        consume(decoder, kPage16Encoded, events);
        failures += expect_page16_packet(events, "recovery after bad escape");
    }

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        constexpr std::array zero_count{
            std::byte{0xE0}, std::byte{0x01}, std::byte{0x00}};
        consume(decoder, zero_count, events);
        failures += expect(
            events.size() == 1 &&
                std::holds_alternative<FramingError>(events.front()) &&
                std::get<FramingError>(events.front()) ==
                    FramingError::ZeroByteCount,
            "zero byte count");
    }

    {
        Decoder decoder;
        std::vector<DecodeEvent> events;
        auto bad_checksum = kPage16Encoded;
        bad_checksum.back() = std::byte{0xB5};
        consume(decoder, bad_checksum, events);
        failures += expect(
            events.size() == 1 &&
                std::holds_alternative<ChecksumFailure>(events.front()) &&
                std::get<ChecksumFailure>(events.front()).address ==
                    Address{0xFF},
            "checksum failure event");
    }

    {
        constexpr std::array<std::uint8_t, 4> payload{
            0xD0, 0x00, 0xE0, 0x00};
        const auto encoded = gc::rfid::jvs::EncodePacket(
            Address{0xFF}, payload);
        failures += expect(encoded.has_value(), "PDF page 16 encode");
        if (encoded) {
            failures += expect(
                std::ranges::equal(encoded->bytes(), kPage16Encoded),
                "PDF page 16 encoded bytes");
        }
    }

    {
        std::array<std::uint8_t, gc::rfid::jvs::kMaxPayloadSize + 1>
            too_large{};
        const auto encoded = gc::rfid::jvs::EncodePacket(
            Address{0x01}, too_large);
        failures += expect(
            !encoded &&
                encoded.error() ==
                    gc::rfid::jvs::EncodeError::PayloadTooLarge,
            "payload over 254 bytes rejected");
    }

    {
        std::array<std::uint8_t, gc::rfid::jvs::kMaxPayloadSize>
            payload{};
        constexpr std::array<std::size_t, 8> lengths{
            0, 1, 2, 0xCF, 0xD0, 0xDF, 0xE0, 0xFE};
        for (const auto length : lengths) {
            for (std::size_t index = 0; index < length; ++index) {
                payload[index] = static_cast<std::uint8_t>(
                    (index * 73 + length * 19) & 0xFF);
            }
            if (length > 0) {
                payload[0] = 0xD0;
            }
            if (length > 1) {
                payload[1] = 0xE0;
            }
            const Address address_value{
                static_cast<std::uint8_t>(
                    length % 2 == 0 ? 0xD0 : 0xE0)};
            failures += expect_round_trip(
                address_value,
                std::span{payload}.first(length),
                "generated encode/decode");
        }
    }

    {
        constexpr std::array<std::uint8_t, 1> checksum_d0{0xCD};
        constexpr std::array<std::uint8_t, 1> checksum_e0{0xDD};
        failures += expect_round_trip(
            Address{0x01}, checksum_d0, "escaped D0 checksum");
        failures += expect_round_trip(
            Address{0x01}, checksum_e0, "escaped E0 checksum");
    }

    {
        std::array<std::uint8_t, gc::rfid::jvs::kMaxPayloadSize>
            maximum{};
        maximum.fill(0xD0);
        const auto encoded = gc::rfid::jvs::EncodePacket(
            Address{0xE0}, maximum);
        failures += expect(encoded.has_value(), "maximum payload encodes");
        if (encoded) {
            failures += expect(
                encoded->size <= gc::rfid::jvs::kMaxEncodedFrameSize,
                "maximum frame stays bounded");
        }
        failures += expect_round_trip(
            Address{0xE0}, maximum, "maximum payload round trip");
    }

    return failures == 0 ? 0 : 1;
}
