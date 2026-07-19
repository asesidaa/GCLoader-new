#pragma once

#include <Windows.h>

#include <expected>
#include <string>

namespace gc::input {

enum class InputWorkerWake {
    Stop,
    Timer,
    Quit,
};

[[nodiscard]] std::expected<InputWorkerWake, std::string>
WaitForInputWorkerWake(HANDLE stop_event, HANDLE timer);

} // namespace gc::input
