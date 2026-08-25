#include "Input/Win32/ImeSuppression.h"

#include <imm.h>

#include <limits>

namespace gc::input
{
    std::expected<void, ImeSuppressionError> DisableProcessIme(
        const ImeSuppressionActions& actions) noexcept
    {
        if (actions.disable_ime == nullptr ||
            actions.get_last_error == nullptr)
        {
            return std::unexpected(ImeSuppressionError{
                .win32_error = ERROR_INVALID_PARAMETER,
            });
        }

        constexpr DWORD every_process_thread =
            (std::numeric_limits<DWORD>::max)();
        if (actions.disable_ime(actions.context, every_process_thread) != FALSE)
        {
            return {};
        }

        return std::unexpected(ImeSuppressionError{
            .win32_error = actions.get_last_error(actions.context),
        });
    }

    std::expected<void, ImeSuppressionError> DisableProcessIme() noexcept
    {
        return DisableProcessIme(ImeSuppressionActions{
            .disable_ime = +[](void*, const DWORD thread_id) noexcept
            {
                return ImmDisableIME(thread_id);
            },
            .get_last_error = +[](void*) noexcept
            {
                return GetLastError();
            },
        });
    }
} // namespace gc::input
