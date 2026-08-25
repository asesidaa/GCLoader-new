#pragma once

#include "Rfid/Jvs/Decoder.h"
#include "Rfid/Jvs/Device.h"
// ReSharper disable once CppUnusedIncludeDirective
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

    constexpr bool operator==(const LineState&) const = default;
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
    void QueueResponse(const jvs::DeviceResponse& response) noexcept;
    void RecordSequencingViolation() noexcept;
    void ResetSerialSession() noexcept;

    State state_{};
    jvs::Device device_{state_};
    jvs::Decoder decoder_{};
    std::optional<jvs::EncodedFrame> pending_reply_;
    std::optional<jvs::EncodedFrame> last_reply_;
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

} // namespace gc::rfid
