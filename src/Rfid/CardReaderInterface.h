#pragma once

#include "Rfid/State.h"

#include <Windows.h>

#include <expected>

namespace gc::rfid::card_reader {

enum class CardReaderConnectionOutcome {
    accepted,
    invalid,
    client_disconnected,
};

[[nodiscard]] std::expected<CardReaderConnectionOutcome, DWORD>
ServeOneCardReaderConnection(
    const wchar_t* pipe_name,
    CardScanState& card_scan) noexcept;

} // namespace gc::rfid::card_reader
