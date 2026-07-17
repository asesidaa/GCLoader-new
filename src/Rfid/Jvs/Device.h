#pragma once

#include "Rfid/Jvs/Types.h"
#include "Rfid/State.h"

#include <array>
#include <optional>
#include <span>
#include <variant>

namespace gc::rfid::jvs {

struct Acknowledgement {
    std::array<std::uint8_t, kMaxPayloadSize> payload{
        status::ok.value};
    std::uint8_t size{1};

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept
    {
        return {payload.data(), size};
    }
};

struct RetransmitPrevious {};

using DeviceResponse =
    std::variant<Acknowledgement, RetransmitPrevious>;

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

    [[nodiscard]] std::optional<DeviceResponse> HandlePacket(
        const DecodedPacket& packet) noexcept;
    [[nodiscard]] std::optional<Acknowledgement> HandleChecksumFailure(
        const ChecksumFailure& failure) noexcept;

private:
    gc::rfid::State& state_;
};

} // namespace gc::rfid::jvs
