#pragma once

#include "Input/Polling/GameplayTransitionJournal.h"

#include <cstdint>
#include <string>

namespace gc::input {

struct InputPollingOpenResult {
    bool success = false;
    std::string message;
};

InputPollingOpenResult OpenInputPollingRuntime();
void CloseInputPollingRuntime() noexcept;
std::uint32_t ReadPublishedInput() noexcept;

}
