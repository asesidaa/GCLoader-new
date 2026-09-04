#pragma once

#include <string>

namespace gc::diagnostics {

struct FatalProcessReport final {
    std::string log;
    std::wstring modal;
    std::wstring title;
};

[[noreturn]] void AbortProcess(FatalProcessReport report) noexcept;

} // namespace gc::diagnostics
