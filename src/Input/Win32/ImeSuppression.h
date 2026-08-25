#pragma once

#include <Windows.h>

#include <expected>

namespace gc::input
{
    struct ImeSuppressionError
    {
        DWORD win32_error{ERROR_SUCCESS};
    };

    struct ImeSuppressionActions
    {
        void* context{};
        BOOL (*disable_ime)(void*, DWORD) noexcept{};
        DWORD (*get_last_error)(void*) noexcept{};
    };

    [[nodiscard]] std::expected<void, ImeSuppressionError> DisableProcessIme(
        const ImeSuppressionActions& actions) noexcept;

    [[nodiscard]] std::expected<void, ImeSuppressionError>
    DisableProcessIme() noexcept;
} // namespace gc::input
