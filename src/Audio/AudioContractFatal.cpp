#include "Audio/AudioContractFatal.h"

#include <Windows.h>

#include <cstdlib>

namespace gc::audio
{
    namespace
    {
        constexpr DWORD kAudioContractExceptionCode = 0xE0474341;
    }

    [[noreturn]] void FailAudioContract(
        const AudioContractFatalReason reason,
        const std::uint64_t operand0,
        const std::uint64_t operand1,
        const std::uint64_t operand2,
        const std::uint64_t operand3) noexcept
    {
        EXCEPTION_RECORD record{};
        record.ExceptionCode = kAudioContractExceptionCode;
        record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
        record.NumberParameters = 5;
        record.ExceptionInformation[0] = static_cast<ULONG_PTR>(reason);
        record.ExceptionInformation[1] = static_cast<ULONG_PTR>(operand0);
        record.ExceptionInformation[2] = static_cast<ULONG_PTR>(operand1);
        record.ExceptionInformation[3] = static_cast<ULONG_PTR>(operand2);
        record.ExceptionInformation[4] = static_cast<ULONG_PTR>(operand3);
        RaiseFailFastException(
            &record,
            nullptr,
            FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);
        std::abort();
    }
} // namespace gc::audio
