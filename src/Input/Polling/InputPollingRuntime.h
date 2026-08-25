#pragma once

#include "Input/Types/InputSettings.h"

#include <cstdint>
#include <expected>
#include <string>

namespace gc::input
{
    struct InputPollingOpenResult
    {
        bool success = false;
        std::string message;
    };

    [[nodiscard]] std::expected<void, std::string>
    ConfigureInputPollingRuntime(InputSettings settings) noexcept;
    InputPollingOpenResult OpenInputPollingRuntime();
    void CloseInputPollingRuntime() noexcept;
    std::uint32_t ReadPublishedInput() noexcept;
}
