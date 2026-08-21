#pragma once

#include "Input/Polling/GameplayTransitionJournal.h"

#include <cstdint>
#include <expected>

namespace gc::absolute_judgement {

struct NativeJudgementIdentity {
    std::uint64_t stage_generation{};
    std::uintptr_t tune_manager{};
    std::uintptr_t tune{};
    std::uintptr_t judgement_state{};
    std::uintptr_t score_state{};
    std::uintptr_t booster{};
    std::uint32_t player{};
    std::int32_t game_time_offset_ms{};
    std::int32_t hold_safe_frame{};
    std::int32_t slide_hold_safe_frame{};
};

enum class JudgementStageError : std::uint8_t {
    AlreadyOpen,
    GenerationExhausted,
    InputCapabilityUnavailable,
    NativeIdentityInvalid,
    NativeIdentityChanged,
    EndpointGenerationChanged,
    InputGenerationChanged,
    QpcFrequencyChanged,
    GameTimeOffsetChanged,
    SafeFrameChanged,
    CleanupIdentityChanged,
};

class JudgementStage final {
public:
    [[nodiscard]] std::expected<void, JudgementStageError> Begin(
        std::uintptr_t tune_manager,
        std::int64_t stage_entry_qpc) noexcept;
    [[nodiscard]] std::expected<void, JudgementStageError> BindOrValidate(
        const NativeJudgementIdentity& native,
        std::uint64_t endpoint_generation,
        std::int64_t endpoint_qpc_frequency) noexcept;
    void Activate() noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool open() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool bound() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::uintptr_t tune_manager() const noexcept;
    [[nodiscard]] const NativeJudgementIdentity& native() const noexcept;
    [[nodiscard]] const gc::input::GameplayTransitionCutoff& cutoff()
        const noexcept;
    [[nodiscard]] std::uint64_t endpoint_generation() const noexcept;

private:
    NativeJudgementIdentity native_{};
    gc::input::GameplayTransitionCutoff cutoff_{};
    std::uint64_t generation_{};
    std::uintptr_t tune_manager_{};
    std::uint64_t endpoint_generation_{};
    bool open_{};
    bool bound_{};
    bool active_{};
};

} // namespace gc::absolute_judgement
