#include "Input/Win32/ImeSuppression.h"

#include <imm.h>
#include <limits>

namespace gc::input {

std::expected<void, ImeSuppressionError> DisableProcessIme() noexcept
{
    constexpr DWORD every_process_thread = (std::numeric_limits<DWORD>::max)();
    if (ImmDisableIME(every_process_thread) != FALSE) return {};
    const DWORD error = GetLastError();
    return std::unexpected(ImeSuppressionError{error});
}

} // namespace gc::input
