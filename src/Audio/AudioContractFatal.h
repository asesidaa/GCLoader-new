#pragma once

#include <cstdint>

namespace gc::audio
{
    enum class AudioContractFatalReason : std::uint32_t
    {
        QueryPerformanceCounterUnavailable = 1,
        QueryPerformanceFrequencyUnavailable,
        LogicalPlayOrderExhausted,
        LogicalCursorStateInvalid,
        LogicalCursorArithmeticFailure,
        LogicalCursorControlRejected,
        LogicalStageClockUnavailable,
        LogicalStageClockArithmeticFailure,
        AbsoluteJudgementContractFailure,
        AsioDriverContractFailure,
        AsioCallbackContractFailure,
        AsioRuntimeNotification,
        AsioShutdownFailure,
        AsioOwnershipFailure,
    };

    [[noreturn]] void FailAudioContract(
        AudioContractFatalReason reason,
        std::uint64_t operand0 = 0,
        std::uint64_t operand1 = 0,
        std::uint64_t operand2 = 0,
        std::uint64_t operand3 = 0) noexcept;
} // namespace gc::audio
