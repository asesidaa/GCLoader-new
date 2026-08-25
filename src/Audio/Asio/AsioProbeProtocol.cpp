// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioProbeProtocol.h"

#include <algorithm>
#include <bit>
// ReSharper disable once CppUnusedIncludeDirective
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gc::audio {
namespace {

enum class MessageKind : std::uint16_t {
    request = 1,
    capability = 2,
    failure = 3,
    control_panel_request = 4,
    control_panel_success = 5,
};

bool IsValidUtf8(std::string_view text) noexcept {
    std::size_t index{};
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7F) {
            ++index;
            continue;
        }

        std::size_t continuation_count{};
        std::uint32_t code_point{};
        std::uint32_t minimum{};
        if ((lead & 0xE0U) == 0xC0U) {
            continuation_count = 1;
            code_point = lead & 0x1FU;
            minimum = 0x80;
        } else if ((lead & 0xF0U) == 0xE0U) {
            continuation_count = 2;
            code_point = lead & 0x0FU;
            minimum = 0x800;
        } else if ((lead & 0xF8U) == 0xF0U) {
            continuation_count = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuation_count > text.size() - index - 1) {
            return false;
        }
        for (std::size_t offset = 1;
             offset <= continuation_count;
             ++offset) {
            const auto byte =
                static_cast<unsigned char>(text[index + offset]);
            if ((byte & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (byte & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

class Writer {
public:
    void U8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void U16(std::uint16_t value) {
        U8(static_cast<std::uint8_t>(value));
        U8(static_cast<std::uint8_t>(value >> 8U));
    }

    void U32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            U8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void U64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            U8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void I32(std::int32_t value) {
        U32(std::bit_cast<std::uint32_t>(value));
    }

    void I64(std::int64_t value) {
        U64(std::bit_cast<std::uint64_t>(value));
    }

    void Double(double value) {
        U64(std::bit_cast<std::uint64_t>(value));
    }

    void Bytes(std::span<const std::byte> bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    std::expected<void, AsioProbeProtocolError> String(
        std::string_view text,
        std::uint32_t maximum) {
        if (text.size() > maximum ||
            text.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(
                AsioProbeProtocolError::limit_exceeded);
        }
        if (!IsValidUtf8(text)) {
            return std::unexpected(
                AsioProbeProtocolError::invalid_utf8);
        }
        U32(static_cast<std::uint32_t>(text.size()));
        Bytes(std::as_bytes(std::span{text}));
        return {};
    }

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] std::vector<std::byte> Take() noexcept {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) noexcept
        : bytes_(bytes) {}

    bool U8(std::uint8_t* value) noexcept {
        if (!Require(1)) {
            return false;
        }
        *value = std::to_integer<std::uint8_t>(bytes_[cursor_++]);
        return true;
    }

    bool U16(std::uint16_t* value) noexcept {
        std::uint8_t low{};
        std::uint8_t high{};
        if (!U8(&low) || !U8(&high)) {
            return false;
        }
        *value = static_cast<std::uint16_t>(
            low | (static_cast<std::uint16_t>(high) << 8U));
        return true;
    }

    bool U32(std::uint32_t* value) noexcept {
        if (!Require(4)) {
            return false;
        }
        std::uint32_t result{};
        for (unsigned shift = 0; shift < 32; shift += 8) {
            result |= std::to_integer<std::uint32_t>(bytes_[cursor_++])
                << shift;
        }
        *value = result;
        return true;
    }

    bool U64(std::uint64_t* value) noexcept {
        if (!Require(8)) {
            return false;
        }
        std::uint64_t result{};
        for (unsigned shift = 0; shift < 64; shift += 8) {
            result |= std::to_integer<std::uint64_t>(bytes_[cursor_++])
                << shift;
        }
        *value = result;
        return true;
    }

    bool I32(std::int32_t* value) noexcept {
        std::uint32_t raw{};
        if (!U32(&raw)) {
            return false;
        }
        *value = std::bit_cast<std::int32_t>(raw);
        return true;
    }

    bool I64(std::int64_t* value) noexcept {
        std::uint64_t raw{};
        if (!U64(&raw)) {
            return false;
        }
        *value = std::bit_cast<std::int64_t>(raw);
        return true;
    }

    bool Double(double* value) noexcept {
        std::uint64_t raw{};
        if (!U64(&raw)) {
            return false;
        }
        *value = std::bit_cast<double>(raw);
        return true;
    }

    bool String(
        std::uint32_t maximum,
        std::string* value) {
        std::uint32_t length{};
        if (!U32(&length)) {
            return false;
        }
        if (length > maximum) {
            error_ = AsioProbeProtocolError::limit_exceeded;
            return false;
        }
        if (!Require(length)) {
            return false;
        }
        const auto* first = reinterpret_cast<const char*>(
            bytes_.data() + cursor_);
        const std::string_view view{first, length};
        if (!IsValidUtf8(view)) {
            error_ = AsioProbeProtocolError::invalid_utf8;
            return false;
        }
        value->assign(view);
        cursor_ += length;
        return true;
    }

    [[nodiscard]] bool Finished() noexcept {
        if (error_.has_value()) {
            return false;
        }
        if (cursor_ != bytes_.size()) {
            error_ = AsioProbeProtocolError::trailing_data;
            return false;
        }
        return true;
    }

    [[nodiscard]] AsioProbeProtocolError error() const noexcept {
        return error_.value_or(AsioProbeProtocolError::truncated);
    }

private:
    bool Require(std::size_t count) noexcept {
        if (count > bytes_.size() - cursor_) {
            error_ = AsioProbeProtocolError::truncated;
            return false;
        }
        return true;
    }

    std::span<const std::byte> bytes_;
    std::size_t cursor_{};
    std::optional<AsioProbeProtocolError> error_;
};

struct Envelope {
    MessageKind kind{};
    std::span<const std::byte> payload;
};

std::expected<Envelope, AsioProbeProtocolError> ReadEnvelope(
    std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < kAsioProbeEnvelopeBytes) {
        return std::unexpected(AsioProbeProtocolError::truncated);
    }
    Reader reader{bytes.first(kAsioProbeEnvelopeBytes)};
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t kind{};
    std::uint32_t payload_size{};
    if (!reader.U32(&magic) || !reader.U16(&version) ||
        !reader.U16(&kind) || !reader.U32(&payload_size)) {
        return std::unexpected(reader.error());
    }
    if (magic != kAsioProbeMagic) {
        return std::unexpected(AsioProbeProtocolError::wrong_magic);
    }
    if (version != kAsioProbeProtocolVersion) {
        return std::unexpected(AsioProbeProtocolError::wrong_version);
    }
    if (payload_size > kAsioProbeMaxPayloadBytes) {
        return std::unexpected(
            AsioProbeProtocolError::limit_exceeded);
    }
    const auto total_size =
        static_cast<std::size_t>(kAsioProbeEnvelopeBytes) + payload_size;
    if (bytes.size() < total_size) {
        return std::unexpected(AsioProbeProtocolError::truncated);
    }
    if (bytes.size() > total_size) {
        return std::unexpected(AsioProbeProtocolError::trailing_data);
    }
    return Envelope{
        static_cast<MessageKind>(kind),
        bytes.subspan(kAsioProbeEnvelopeBytes, payload_size),
    };
}

std::expected<std::vector<std::byte>, AsioProbeProtocolError> Frame(
    MessageKind kind,
    const Writer& payload) {
    if (payload.bytes().size() > kAsioProbeMaxPayloadBytes ||
        payload.bytes().size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            AsioProbeProtocolError::limit_exceeded);
    }
    Writer frame;
    frame.U32(kAsioProbeMagic);
    frame.U16(kAsioProbeProtocolVersion);
    frame.U16(static_cast<std::uint16_t>(kind));
    frame.U32(static_cast<std::uint32_t>(payload.bytes().size()));
    frame.Bytes(payload.bytes());
    return frame.Take();
}

bool IsKnownProbeMode(std::uint32_t value) noexcept {
    return value == static_cast<std::uint32_t>(AsioProbeMode::inspect) ||
        value == static_cast<std::uint32_t>(AsioProbeMode::validate);
}

bool IsKnownFailureStage(std::uint32_t value) noexcept {
    return value <=
        static_cast<std::uint32_t>(
            AsioFailureStage::foreground_monitor);
}

bool IsKnownResultDomain(std::uint32_t value) noexcept {
    return value <= static_cast<std::uint32_t>(AsioResultDomain::winmm);
}

bool IsKnownSampleType(std::int32_t value) noexcept {
    switch (value) {
    case ASIOSTInt16LSB:
    case ASIOSTInt24LSB:
    case ASIOSTInt32LSB:
    case ASIOSTFloat32LSB:
    case ASIOSTFloat64LSB:
    case ASIOSTInt32LSB16:
    case ASIOSTInt32LSB18:
    case ASIOSTInt32LSB20:
    case ASIOSTInt32LSB24:
        return true;
    default:
        return false;
    }
}

std::expected<void, AsioProbeProtocolError> WriteGuid(
    Writer& writer,
    const CLSID& clsid) {
    writer.U32(clsid.Data1);
    writer.U16(clsid.Data2);
    writer.U16(clsid.Data3);
    for (const auto value : clsid.Data4) {
        writer.U8(value);
    }
    return {};
}

bool ReadGuid(Reader& reader, CLSID* clsid) noexcept {
    std::uint32_t data1{};
    std::uint16_t data2{};
    std::uint16_t data3{};
    if (!reader.U32(&data1) ||
        !reader.U16(&data2) ||
        !reader.U16(&data3)) {
        return false;
    }
    clsid->Data1 = data1;
    clsid->Data2 = data2;
    clsid->Data3 = data3;
    for (auto& value : clsid->Data4) {
        if (!reader.U8(&value)) {
            return false;
        }
    }
    return true;
}

std::expected<void, AsioProbeProtocolError> WriteReport(
    Writer& writer,
    const AsioCapabilityReport& report) {
    auto text = writer.String(
        report.registration.registry_name,
        kAsioProbeMaxDriverNameBytes);
    if (!text) {
        return text;
    }
    (void)WriteGuid(writer, report.registration.clsid);
    text = writer.String(
        report.reported_driver_name,
        kAsioProbeMaxDriverNameBytes);
    if (!text) {
        return text;
    }
    if (report.driver_version < std::numeric_limits<std::int32_t>::min() ||
        report.driver_version > std::numeric_limits<std::int32_t>::max()) {
        return std::unexpected(
            AsioProbeProtocolError::integer_overflow);
    }
    writer.I32(static_cast<std::int32_t>(report.driver_version));
    writer.Double(report.original_sample_rate);
    writer.Double(report.sample_rate);
    for (const auto value : {
             report.buffer_limits.minimum,
             report.buffer_limits.maximum,
             report.buffer_limits.preferred,
             report.buffer_limits.granularity}) {
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) {
            return std::unexpected(
                AsioProbeProtocolError::integer_overflow);
        }
        writer.I32(static_cast<std::int32_t>(value));
    }
    writer.U32(report.input_channels);
    if (report.input_channels > kAsioProbeMaxChannels) {
        return std::unexpected(
            AsioProbeProtocolError::limit_exceeded);
    }
    if (report.output_channels.size() > kAsioProbeMaxChannels) {
        return std::unexpected(
            AsioProbeProtocolError::limit_exceeded);
    }
    writer.U32(static_cast<std::uint32_t>(report.output_channels.size()));
    for (const auto& channel : report.output_channels) {
        writer.U32(channel.index);
        text = writer.String(channel.name, kAsioProbeMaxChannelNameBytes);
        if (!text) {
            return text;
        }
        if (!IsKnownSampleType(
                static_cast<std::int32_t>(channel.sample_type))) {
            return std::unexpected(AsioProbeProtocolError::unknown_enum);
        }
        writer.I32(static_cast<std::int32_t>(channel.sample_type));
    }
    writer.U32(report.selected_base_channel);
    writer.U32(report.effective_buffer_frames);
    writer.U32(report.input_latency_frames);
    writer.U32(report.output_latency_frames);
    writer.U32(report.output_ready_supported ? 1U : 0U);
    writer.U32(report.overload_reporting_supported ? 1U : 0U);
    return {};
}

std::expected<AsioCapabilityReport, AsioProbeProtocolError> ReadReport(
    std::span<const std::byte> payload) {
    Reader reader{payload};
    AsioCapabilityReport report;
    std::int32_t driver_version{};
    std::int32_t minimum{};
    std::int32_t maximum{};
    std::int32_t preferred{};
    std::int32_t granularity{};
    std::uint32_t channel_count{};
    if (!reader.String(
            kAsioProbeMaxDriverNameBytes,
            &report.registration.registry_name) ||
        !ReadGuid(reader, &report.registration.clsid) ||
        !reader.String(
            kAsioProbeMaxDriverNameBytes,
            &report.reported_driver_name) ||
        !reader.I32(&driver_version) ||
        !reader.Double(&report.original_sample_rate) ||
        !reader.Double(&report.sample_rate) ||
        !reader.I32(&minimum) ||
        !reader.I32(&maximum) ||
        !reader.I32(&preferred) ||
        !reader.I32(&granularity) ||
        !reader.U32(&report.input_channels) ||
        !reader.U32(&channel_count)) {
        return std::unexpected(reader.error());
    }
    if (report.input_channels > kAsioProbeMaxChannels ||
        channel_count > kAsioProbeMaxChannels) {
        return std::unexpected(
            AsioProbeProtocolError::limit_exceeded);
    }
    if (!std::isfinite(report.original_sample_rate) ||
        !std::isfinite(report.sample_rate)) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
    report.driver_version = driver_version;
    report.buffer_limits = {minimum, maximum, preferred, granularity};
    report.output_channels.reserve(channel_count);
    for (std::uint32_t index = 0; index < channel_count; ++index) {
        AsioChannelDescriptor channel;
        std::int32_t sample_type{};
        if (!reader.U32(&channel.index) ||
            !reader.String(kAsioProbeMaxChannelNameBytes, &channel.name) ||
            !reader.I32(&sample_type)) {
            return std::unexpected(reader.error());
        }
        if (!IsKnownSampleType(sample_type)) {
            return std::unexpected(AsioProbeProtocolError::unknown_enum);
        }
        channel.sample_type = sample_type;
        report.output_channels.push_back(std::move(channel));
    }

    std::uint32_t output_ready{};
    std::uint32_t overload_reporting{};
    if (!reader.U32(&report.selected_base_channel) ||
        !reader.U32(&report.effective_buffer_frames) ||
        !reader.U32(&report.input_latency_frames) ||
        !reader.U32(&report.output_latency_frames) ||
        !reader.U32(&output_ready) ||
        !reader.U32(&overload_reporting)) {
        return std::unexpected(reader.error());
    }
    if (output_ready > 1 || overload_reporting > 1) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
    report.output_ready_supported = output_ready != 0;
    report.overload_reporting_supported = overload_reporting != 0;
    if (!reader.Finished()) {
        return std::unexpected(reader.error());
    }
    return report;
}

std::expected<void, AsioProbeProtocolError> WriteFailure(
    Writer& writer,
    const AsioFailure& failure) {
    const auto stage = static_cast<std::uint32_t>(failure.stage);
    const auto domain = static_cast<std::uint32_t>(failure.domain);
    if (!IsKnownFailureStage(stage) || !IsKnownResultDomain(domain)) {
        return std::unexpected(AsioProbeProtocolError::unknown_enum);
    }
    writer.U32(stage);
    writer.U32(domain);
    writer.I64(failure.result);
    auto text = writer.String(
        failure.driver_message,
        kAsioProbeMaxFailureTextBytes);
    if (!text) {
        return text;
    }
    return writer.String(
        failure.detail,
        kAsioProbeMaxFailureTextBytes);
}

std::expected<AsioFailure, AsioProbeProtocolError> ReadFailure(
    std::span<const std::byte> payload) {
    Reader reader{payload};
    std::uint32_t stage{};
    std::uint32_t domain{};
    AsioFailure failure;
    if (!reader.U32(&stage) || !reader.U32(&domain) ||
        !reader.I64(&failure.result)) {
        return std::unexpected(reader.error());
    }
    if (!IsKnownFailureStage(stage) || !IsKnownResultDomain(domain)) {
        return std::unexpected(AsioProbeProtocolError::unknown_enum);
    }
    failure.stage = static_cast<AsioFailureStage>(stage);
    failure.domain = static_cast<AsioResultDomain>(domain);
    if (!reader.String(
            kAsioProbeMaxFailureTextBytes,
            &failure.driver_message) ||
        !reader.String(
            kAsioProbeMaxFailureTextBytes,
            &failure.detail)) {
        return std::unexpected(reader.error());
    }
    if (!reader.Finished()) {
        return std::unexpected(reader.error());
    }
    return failure;
}

} // namespace

std::expected<std::vector<std::byte>, AsioProbeProtocolError>
EncodeAsioProbeRequest(const AsioProbeRequest& request) noexcept {
    try {
        const auto mode = static_cast<std::uint32_t>(request.mode);
        if (!IsKnownProbeMode(mode)) {
            return std::unexpected(AsioProbeProtocolError::unknown_enum);
        }
        Writer payload;
        payload.U32(mode);
        payload.U32(request.buffer_frames);
        payload.U32(request.output_base_channel);
        auto name = payload.String(
            request.driver_name,
            kAsioProbeMaxDriverNameBytes);
        if (!name) {
            return std::unexpected(name.error());
        }
        return Frame(MessageKind::request, payload);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

std::expected<AsioProbeRequest, AsioProbeProtocolError>
DecodeAsioProbeRequest(std::span<const std::byte> bytes) noexcept {
    try {
        const auto envelope = ReadEnvelope(bytes);
        if (!envelope) {
            return std::unexpected(envelope.error());
        }
        if (envelope->kind != MessageKind::request) {
            return std::unexpected(AsioProbeProtocolError::wrong_kind);
        }
        Reader reader{envelope->payload};
        std::uint32_t mode{};
        AsioProbeRequest request;
        if (!reader.U32(&mode) ||
            !reader.U32(&request.buffer_frames) ||
            !reader.U32(&request.output_base_channel) ||
            !reader.String(
                kAsioProbeMaxDriverNameBytes,
                &request.driver_name)) {
            return std::unexpected(reader.error());
        }
        if (!IsKnownProbeMode(mode)) {
            return std::unexpected(AsioProbeProtocolError::unknown_enum);
        }
        if (!reader.Finished()) {
            return std::unexpected(reader.error());
        }
        request.mode = static_cast<AsioProbeMode>(mode);
        return request;
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

std::expected<std::vector<std::byte>, AsioProbeProtocolError>
EncodeAsioProbeResult(const AsioProbeResult& result) noexcept {
    try {
        Writer payload;
        if (result.has_value()) {
            auto encoded = WriteReport(payload, *result);
            if (!encoded) {
                return std::unexpected(encoded.error());
            }
            return Frame(MessageKind::capability, payload);
        }
        auto encoded = WriteFailure(payload, result.error());
        if (!encoded) {
            return std::unexpected(encoded.error());
        }
        return Frame(MessageKind::failure, payload);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

std::expected<AsioProbeResult, AsioProbeProtocolError>
DecodeAsioProbeResult(std::span<const std::byte> bytes) noexcept {
    try {
        const auto envelope = ReadEnvelope(bytes);
        if (!envelope) {
            return std::unexpected(envelope.error());
        }
        if (envelope->kind == MessageKind::capability) {
            auto report = ReadReport(envelope->payload);
            if (!report) {
                return std::unexpected(report.error());
            }
            return AsioProbeResult{std::move(*report)};
        }
        if (envelope->kind == MessageKind::failure) {
            auto failure = ReadFailure(envelope->payload);
            if (!failure) {
                return std::unexpected(failure.error());
            }
            return AsioProbeResult{
                std::unexpected(std::move(*failure))};
        }
        return std::unexpected(AsioProbeProtocolError::wrong_kind);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

std::expected<std::vector<std::byte>, AsioProbeProtocolError>
EncodeAsioControlPanelRequest(
    const AsioControlPanelRequest& request) noexcept {
    try {
        if (request.driver_name.empty()) {
            return std::unexpected(
                AsioProbeProtocolError::invalid_value);
        }
        Writer payload;
        auto name = payload.String(
            request.driver_name,
            kAsioProbeMaxDriverNameBytes);
        if (!name) {
            return std::unexpected(name.error());
        }
        return Frame(MessageKind::control_panel_request, payload);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

std::expected<AsioControlPanelRequest, AsioProbeProtocolError>
DecodeAsioControlPanelRequest(
    std::span<const std::byte> bytes) noexcept {
    try {
        const auto envelope = ReadEnvelope(bytes);
        if (!envelope) {
            return std::unexpected(envelope.error());
        }
        if (envelope->kind != MessageKind::control_panel_request) {
            return std::unexpected(AsioProbeProtocolError::wrong_kind);
        }
        Reader reader{envelope->payload};
        AsioControlPanelRequest request;
        if (!reader.String(
                kAsioProbeMaxDriverNameBytes,
                &request.driver_name)) {
            return std::unexpected(reader.error());
        }
        if (!reader.Finished()) {
            return std::unexpected(reader.error());
        }
        if (request.driver_name.empty()) {
            return std::unexpected(
                AsioProbeProtocolError::invalid_value);
        }
        return request;
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

std::expected<std::vector<std::byte>, AsioProbeProtocolError>
EncodeAsioControlPanelResult(
    const AsioControlPanelResult& result) noexcept {
    try {
        Writer payload;
        if (result.has_value()) {
            return Frame(MessageKind::control_panel_success, payload);
        }
        auto encoded = WriteFailure(payload, result.error());
        if (!encoded) {
            return std::unexpected(encoded.error());
        }
        return Frame(MessageKind::failure, payload);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

std::expected<AsioControlPanelResult, AsioProbeProtocolError>
DecodeAsioControlPanelResult(
    std::span<const std::byte> bytes) noexcept {
    try {
        const auto envelope = ReadEnvelope(bytes);
        if (!envelope) {
            return std::unexpected(envelope.error());
        }
        if (envelope->kind == MessageKind::control_panel_success) {
            if (!envelope->payload.empty()) {
                return std::unexpected(
                    AsioProbeProtocolError::trailing_data);
            }
            return AsioControlPanelResult{};
        }
        if (envelope->kind == MessageKind::failure) {
            auto failure = ReadFailure(envelope->payload);
            if (!failure) {
                return std::unexpected(failure.error());
            }
            return AsioControlPanelResult{
                std::unexpected(std::move(*failure))};
        }
        return std::unexpected(AsioProbeProtocolError::wrong_kind);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            AsioProbeProtocolError::allocation_failure);
    } catch (...) {
        return std::unexpected(AsioProbeProtocolError::invalid_value);
    }
}

} // namespace gc::audio
