#pragma once

#include <Windows.h>

#include <expected>

namespace gc::input
{
    struct ImeSuppressionError
    {
        DWORD win32_error{ERROR_SUCCESS};
    };

    [[nodiscard]] std::expected<void, ImeSuppressionError>
    DisableProcessIme() noexcept;
} // namespace gc::input
