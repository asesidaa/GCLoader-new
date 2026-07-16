#include "Rfid/Jvs/Device.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace gc::rfid::jvs {
namespace {

constexpr std::uint8_t kSupportedPlayers = 2;
constexpr std::uint8_t kSwitchBytesPerPlayer = 2;
constexpr std::uint8_t kSupportedCoinSlots = 2;
constexpr std::uint16_t kMaximumCoinCount = 0x3FFF;

constexpr std::string_view kRfidId =
    "TAITO CORP.;RFID CTRL P.C.B.;Ver1.00;";

constexpr std::array<std::uint8_t, 10> kLegacyFeatureBytes{
    0x01, 0x07, 0x00, 0x08, 0x00,
    0x12, 0x08, 0x00, 0x00, 0x00};

struct RequestCursor {
    std::span<const std::uint8_t> remaining;

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> Take(
        std::size_t count) noexcept
    {
        if (remaining.size() < count) {
            return std::nullopt;
        }

        const auto taken = remaining.first(count);
        remaining = remaining.subspan(count);
        return taken;
    }
};

[[nodiscard]] bool AppendOrOverflow(
    ReplyWriter& writer,
    std::uint8_t value) noexcept
{
    if (writer.Append(value)) {
        return true;
    }

    writer.SetOverflow();
    return false;
}

[[nodiscard]] bool AppendOrOverflow(
    ReplyWriter& writer,
    std::span<const std::uint8_t> values) noexcept
{
    if (writer.Append(values)) {
        return true;
    }

    writer.SetOverflow();
    return false;
}

[[nodiscard]] std::uint16_t ParameterValue(
    std::span<const std::uint8_t> parameters) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(parameters[1]) << 8) |
        parameters[2]);
}

} // namespace

ReplyWriter::ReplyWriter(Acknowledgement& reply) noexcept
    : reply_{reply}
{
}

bool ReplyWriter::Append(std::uint8_t value) noexcept
{
    if (reply_.size >= reply_.payload.size()) {
        return false;
    }

    reply_.payload[reply_.size++] = value;
    return true;
}

bool ReplyWriter::Append(
    std::span<const std::uint8_t> values) noexcept
{
    const auto available = reply_.payload.size() - reply_.size;
    if (values.size() > available) {
        return false;
    }

    std::ranges::copy(values, reply_.payload.begin() + reply_.size);
    reply_.size = static_cast<std::uint8_t>(reply_.size + values.size());
    return true;
}

void ReplyWriter::SetStatus(Status value) noexcept
{
    reply_.payload.front() = value.value;
    if (reply_.size == 0) {
        reply_.size = 1;
    }
}

void ReplyWriter::SetOverflow() noexcept
{
    reply_.payload.front() = status::acknowledgement_overflow.value;
    reply_.size = 1;
}

Device::Device(gc::rfid::State& state) noexcept
    : state_{state}
{
}

std::optional<DeviceResponse> Device::HandlePacket(
    const DecodedPacket& packet) noexcept
{
    const auto payload = packet.payload();
    if (payload.empty()) {
        return std::nullopt;
    }

    const bool is_reset = payload.front() == command::reset.value;
    const bool is_set_address =
        payload.front() == command::set_address.value;
    const bool accepts_broadcast =
        packet.address.is_broadcast() && (is_reset || is_set_address);
    const bool accepts_assigned =
        state_.assigned_address &&
        packet.address == *state_.assigned_address;
    if (!accepts_broadcast && !accepts_assigned) {
        return std::nullopt;
    }

    Acknowledgement acknowledgement;
    ReplyWriter writer{acknowledgement};
    RequestCursor cursor{payload};
    bool acknowledgement_required = false;

    const auto finish = [&]() -> std::optional<DeviceResponse> {
        if (!acknowledgement_required) {
            return std::nullopt;
        }
        return DeviceResponse{acknowledgement};
    };

    const auto append_report = [&](Report value) {
        acknowledgement_required = true;
        return AppendOrOverflow(writer, value.value);
    };

    const auto invalid_input = [&]() -> std::optional<DeviceResponse> {
        if (!append_report(report::invalid_input_parameter)) {
            return DeviceResponse{acknowledgement};
        }
        return finish();
    };

    while (!cursor.remaining.empty()) {
        const auto command_bytes = cursor.Take(1);
        const CommandId command_id{(*command_bytes)[0]};

        switch (command_id.value) {
        case command::reset.value: {
            if (!packet.address.is_broadcast()) {
                writer.SetStatus(status::unknown_command);
                acknowledgement_required = true;
                return finish();
            }

            const auto parameters = cursor.Take(1);
            if (!parameters || (*parameters)[0] != 0xD9) {
                return finish();
            }
            state_.ResetBus();
            break;
        }

        case command::set_address.value: {
            if (!packet.address.is_broadcast()) {
                writer.SetStatus(status::unknown_command);
                acknowledgement_required = true;
                return finish();
            }

            const auto parameters = cursor.Take(1);
            if (!parameters) {
                return invalid_input();
            }
            state_.assigned_address = Address{(*parameters)[0]};
            if (!append_report(report::ok)) {
                return DeviceResponse{acknowledgement};
            }
            break;
        }

        case command::read_id.value: {
            if (!append_report(report::ok)) {
                return DeviceResponse{acknowledgement};
            }
            for (const char character : kRfidId) {
                if (!AppendOrOverflow(
                        writer, static_cast<std::uint8_t>(character))) {
                    return DeviceResponse{acknowledgement};
                }
            }
            if (!AppendOrOverflow(writer, 0x00)) {
                return DeviceResponse{acknowledgement};
            }
            break;
        }

        case command::command_format_revision.value:
            if (!append_report(report::ok) ||
                !AppendOrOverflow(writer, 0x13)) {
                return DeviceResponse{acknowledgement};
            }
            break;

        case command::jvs_revision.value:
            if (!append_report(report::ok) ||
                !AppendOrOverflow(writer, 0x30)) {
                return DeviceResponse{acknowledgement};
            }
            break;

        case command::communication_revision.value:
            if (!append_report(report::ok) ||
                !AppendOrOverflow(writer, 0x10)) {
                return DeviceResponse{acknowledgement};
            }
            break;

        case command::capabilities.value:
            if (!append_report(report::ok) ||
                !AppendOrOverflow(writer, kLegacyFeatureBytes)) {
                return DeviceResponse{acknowledgement};
            }
            break;

        case command::read_switches.value: {
            const auto parameters = cursor.Take(2);
            if (!parameters) {
                return invalid_input();
            }

            const auto players = (*parameters)[0];
            const auto bytes_per_player = (*parameters)[1];
            if (players == 0 || players > kSupportedPlayers ||
                bytes_per_player == 0 ||
                bytes_per_player > kSwitchBytesPerPlayer) {
                if (!append_report(report::invalid_input_parameter)) {
                    return DeviceResponse{acknowledgement};
                }
                break;
            }

            if (!append_report(report::ok) ||
                !AppendOrOverflow(writer, 0x00)) {
                return DeviceResponse{acknowledgement};
            }
            const auto data_size = static_cast<std::size_t>(players) *
                bytes_per_player;
            for (std::size_t i = 0; i < data_size; ++i) {
                if (!AppendOrOverflow(writer, 0x00)) {
                    return DeviceResponse{acknowledgement};
                }
            }
            break;
        }

        case command::read_coins.value: {
            const auto parameters = cursor.Take(1);
            if (!parameters) {
                return invalid_input();
            }

            const auto slots = (*parameters)[0];
            if (slots == 0 || slots > kSupportedCoinSlots) {
                if (!append_report(report::invalid_input_parameter)) {
                    return DeviceResponse{acknowledgement};
                }
                break;
            }

            if (!append_report(report::ok)) {
                return DeviceResponse{acknowledgement};
            }
            for (std::size_t slot = 0; slot < slots; ++slot) {
                const auto value = state_.coins[slot];
                if (!AppendOrOverflow(
                        writer, static_cast<std::uint8_t>(value >> 8)) ||
                    !AppendOrOverflow(
                        writer, static_cast<std::uint8_t>(value))) {
                    return DeviceResponse{acknowledgement};
                }
            }
            break;
        }

        case command::read_general_input.value: {
            const auto parameters = cursor.Take(1);
            if (!parameters) {
                return invalid_input();
            }

            if (!append_report(report::ok)) {
                return DeviceResponse{acknowledgement};
            }
            const auto value = state_.card_scan.IsPresent()
                ? std::uint8_t{0x19}
                : std::uint8_t{0x00};
            for (std::size_t i = 0; i < (*parameters)[0]; ++i) {
                if (!AppendOrOverflow(writer, value)) {
                    return DeviceResponse{acknowledgement};
                }
            }
            break;
        }

        case command::retransmit.value:
            if (payload.size() == 1) {
                return DeviceResponse{RetransmitPrevious{}};
            }
            writer.SetStatus(status::unknown_command);
            acknowledgement_required = true;
            return finish();

        case command::decrease_coins.value:
        case command::increase_coins.value: {
            const auto parameters = cursor.Take(3);
            if (!parameters) {
                return invalid_input();
            }

            const auto slot = (*parameters)[0];
            if (slot == 0 || slot > state_.coins.size()) {
                if (!append_report(report::invalid_output_parameter)) {
                    return DeviceResponse{acknowledgement};
                }
                break;
            }

            const auto value = ParameterValue(*parameters);
            auto& coin_count = state_.coins[slot - 1];
            if (command_id == command::decrease_coins) {
                coin_count = value > coin_count
                    ? std::uint16_t{0}
                    : static_cast<std::uint16_t>(coin_count - value);
            } else {
                const auto sum =
                    static_cast<std::uint32_t>(coin_count) + value;
                coin_count = static_cast<std::uint16_t>(
                    std::min<std::uint32_t>(sum, kMaximumCoinCount));
            }

            if (!append_report(report::ok)) {
                return DeviceResponse{acknowledgement};
            }
            break;
        }

        case command::write_general_output.value: {
            const auto count_bytes = cursor.Take(1);
            if (!count_bytes) {
                return invalid_input();
            }

            const auto byte_count = (*count_bytes)[0];
            const auto output = cursor.Take(byte_count);
            if (!output) {
                if (!append_report(report::invalid_output_parameter)) {
                    return DeviceResponse{acknowledgement};
                }
                return finish();
            }

            // The RFID board specializes the standard output command as the
            // game's one-shot card-data transfer. Preserve those observed
            // bytes while validating the standard count-prefixed request.
            if (!append_report(report::ok)) {
                return DeviceResponse{acknowledgement};
            }

            const bool card_present = state_.card_scan.IsPresent();
            if (card_present) {
                if (!AppendOrOverflow(writer, kCardData)) {
                    return DeviceResponse{acknowledgement};
                }
            } else {
                const auto response_size =
                    static_cast<std::size_t>(byte_count) * kCardData.size();
                for (std::size_t i = 0; i < response_size; ++i) {
                    if (!AppendOrOverflow(writer, 0x00)) {
                        return DeviceResponse{acknowledgement};
                    }
                }
            }

            if (!append_report(report::ok)) {
                return DeviceResponse{acknowledgement};
            }
            if (card_present) {
                static_cast<void>(state_.card_scan.Consume());
            }
            break;
        }

        default:
            writer.SetStatus(status::unknown_command);
            acknowledgement_required = true;
            return finish();
        }
    }

    return finish();
}

std::optional<Acknowledgement> Device::HandleChecksumFailure(
    const ChecksumFailure& failure) noexcept
{
    if (!state_.assigned_address ||
        failure.address != *state_.assigned_address) {
        return std::nullopt;
    }

    Acknowledgement acknowledgement;
    acknowledgement.payload.front() = status::checksum_error.value;
    return acknowledgement;
}

} // namespace gc::rfid::jvs
