#include "Rfid/ComPortState.h"

#include "plog/Log.h"

#include <algorithm>
#include <concepts>
#include <type_traits>
#include <utility>

namespace gc::rfid {
namespace {

[[nodiscard]] DCB DefaultDcb() noexcept
{
    DCB value{};
    value.DCBlength = sizeof(DCB);
    value.BaudRate = CBR_115200;
    value.fBinary = TRUE;
    value.ByteSize = 8;
    value.Parity = NOPARITY;
    value.StopBits = ONESTOPBIT;
    return value;
}

[[nodiscard]] const char* FramingErrorName(
    jvs::FramingError error) noexcept
{
    switch (error) {
    case jvs::FramingError::InvalidEscape:
        return "invalid_escape";
    case jvs::FramingError::ZeroByteCount:
        return "zero_byte_count";
    }
    return "unknown";
}

} // namespace

ComPortState::ComPortState() noexcept
    : dcb_{DefaultDcb()}
{
}

void ComPortState::Open() noexcept
{
    open_ = true;
}

void ComPortState::Close() noexcept
{
    open_ = false;
    state_.ResetBus();
    ResetSerialSession();
}

bool ComPortState::IsOpen() const noexcept
{
    return open_;
}

std::expected<void, DWORD> ComPortState::SetupComm(
    DWORD input_queue,
    DWORD output_queue) noexcept
{
    input_queue_size_ = input_queue;
    output_queue_size_ = output_queue;
    return {};
}

DCB ComPortState::GetCommState() const noexcept
{
    return dcb_;
}

std::expected<void, DWORD> ComPortState::SetCommState(
    const DCB& value) noexcept
{
    if (value.DCBlength != sizeof(DCB) ||
        value.ByteSize != 8 ||
        value.Parity != NOPARITY ||
        value.StopBits != ONESTOPBIT) {
        return std::unexpected(ERROR_INVALID_PARAMETER);
    }

    dcb_ = value;
    return {};
}

DWORD ComPortState::GetCommMask() const noexcept
{
    return event_mask_;
}

std::expected<void, DWORD> ComPortState::SetCommMask(
    DWORD value) noexcept
{
    event_mask_ = value;
    return {};
}

COMMTIMEOUTS ComPortState::GetCommTimeouts() const noexcept
{
    return timeouts_;
}

std::expected<void, DWORD> ComPortState::SetCommTimeouts(
    const COMMTIMEOUTS& value) noexcept
{
    timeouts_ = value;
    return {};
}

DWORD ComPortState::ModemStatus() const noexcept
{
    return state_.assigned_address ? MS_CTS_ON : 0;
}

std::expected<void, DWORD> ComPortState::EscapeCommFunction(
    DWORD function) noexcept
{
    switch (function) {
    case SETDTR:
        dtr_ = true;
        break;
    case CLRDTR:
        dtr_ = false;
        break;
    case SETRTS:
        rts_ = true;
        break;
    case CLRRTS:
        rts_ = false;
        break;
    case SETXOFF:
        xoff_ = true;
        break;
    case SETXON:
        xoff_ = false;
        break;
    case SETBREAK:
        break_active_ = true;
        break;
    case CLRBREAK:
        break_active_ = false;
        break;
    default:
        return std::unexpected(ERROR_INVALID_FUNCTION);
    }
    return {};
}

std::expected<std::size_t, DWORD> ComPortState::Write(
    std::span<const std::byte> bytes,
    bool overlapped) noexcept
{
    if (overlapped) {
        return std::unexpected(ERROR_INVALID_PARAMETER);
    }

    decoder_.Consume(bytes, [this](jvs::DecodeEvent event) {
        std::visit(
            [this]<typename Event>(Event&& decoded) {
                using Decoded = std::remove_cvref_t<Event>;
                if constexpr (std::same_as<Decoded, jvs::DecodedPacket>) {
                    if (auto response = device_.HandlePacket(decoded)) {
                        QueueResponse(std::move(*response));
                    }
                } else if constexpr (
                    std::same_as<Decoded, jvs::ChecksumFailure>) {
                    PLOG_WARNING
                        << "RFID JVS checksum failure address=0x"
                        << std::hex
                        << static_cast<unsigned int>(decoded.address.value)
                        << " expected=0x"
                        << static_cast<unsigned int>(decoded.expected)
                        << " actual=0x"
                        << static_cast<unsigned int>(decoded.actual)
                        << std::dec
                        << " byte_count="
                        << static_cast<unsigned int>(decoded.byte_count);
                    if (auto response =
                            jvs::Device::HandleChecksumFailure(decoded)) {
                        QueueResponse(jvs::DeviceResponse{*response});
                    }
                } else {
                    PLOG_WARNING
                        << "RFID JVS framing error="
                        << FramingErrorName(decoded);
                }
            },
            std::move(event));
    });
    return bytes.size();
}

std::expected<std::size_t, DWORD> ComPortState::Read(
    std::span<std::byte> destination,
    bool overlapped) noexcept
{
    if (overlapped) {
        return std::unexpected(ERROR_INVALID_PARAMETER);
    }
    if (!pending_reply_ || destination.empty()) {
        return std::size_t{0};
    }

    const auto unread = pending_reply_->bytes().subspan(read_cursor_);
    const auto copy_size = std::min(destination.size(), unread.size());
    std::ranges::copy(
        unread.first(copy_size), destination.begin());
    read_cursor_ += copy_size;

    if (read_cursor_ == pending_reply_->bytes().size()) {
        pending_reply_.reset();
        read_cursor_ = 0;
    }
    return copy_size;
}

DWORD ComPortState::PendingByteCount() const noexcept
{
    if (!pending_reply_) {
        return 0;
    }
    return static_cast<DWORD>(
        pending_reply_->bytes().size() - read_cursor_);
}

COMSTAT ComPortState::CommStatus() const noexcept
{
    COMSTAT status{};
    status.cbInQue = PendingByteCount();
    return status;
}

std::pair<DWORD, DWORD> ComPortState::QueueSizes() const noexcept
{
    return {input_queue_size_, output_queue_size_};
}

LineState ComPortState::GetLineState() const noexcept
{
    return {
        .dtr = dtr_,
        .rts = rts_,
        .xoff = xoff_,
        .break_active = break_active_,
    };
}

std::uint64_t ComPortState::SequencingViolationCount() const noexcept
{
    return sequencing_violation_count_;
}

State& ComPortState::device_state() noexcept
{
    return state_;
}

void ComPortState::QueueResponse(const jvs::DeviceResponse& response) noexcept
{
    if (pending_reply_) {
        RecordSequencingViolation();
        return;
    }

    if (std::holds_alternative<jvs::RetransmitPrevious>(response)) {
        if (last_reply_) {
            pending_reply_ = *last_reply_;
            read_cursor_ = 0;
        } else {
            PLOG_WARNING
                << "RFID JVS retransmit requested without prior reply";
        }
        return;
    }

    const auto& acknowledgement = std::get<jvs::Acknowledgement>(response);
    const auto encoded = jvs::EncodePacket(
        jvs::address::master, acknowledgement.bytes());
    if (!encoded) {
        PLOG_ERROR << "RFID COM2: bounded acknowledgement could not be encoded";
        return;
    }

    pending_reply_ = *encoded;
    last_reply_ = *encoded;
    read_cursor_ = 0;
}

void ComPortState::RecordSequencingViolation() noexcept
{
    ++sequencing_violation_count_;
    PLOG_WARNING
        << "RFID COM2: discarded reply while an acknowledgement is pending";
}

void ComPortState::ResetSerialSession() noexcept
{
    decoder_.Reset();
    pending_reply_.reset();
    last_reply_.reset();
    read_cursor_ = 0;
    dcb_ = DefaultDcb();
    timeouts_ = {};
    event_mask_ = 0;
    input_queue_size_ = 0;
    output_queue_size_ = 0;
    dtr_ = false;
    rts_ = false;
    xoff_ = false;
    break_active_ = false;
    sequencing_violation_count_ = 0;
}

} // namespace gc::rfid
