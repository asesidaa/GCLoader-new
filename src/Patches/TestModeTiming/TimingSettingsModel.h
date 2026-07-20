#pragma once

#include <array>

namespace gc::test_mode_timing {

inline constexpr int kMinimumOffsetMs = -50;
inline constexpr int kMaximumOffsetMs = 50;

struct TimingOffsets {
    int game_ms{};
    int judge_ms{};

    friend bool operator==(
        const TimingOffsets&,
        const TimingOffsets&) = default;
};

enum class TimingRow {
    MusicOffset,
    JudgeOffset,
    SaveAndBack,
    Cancel,
};

enum class SaveStatus {
    Idle,
    Failed,
    Succeeded,
};

enum class TimingCommand {
    None,
    Save,
    Cancel,
};

class TimingSettingsModel {
public:
    void Activate(TimingOffsets live) noexcept;
    void SetRow(TimingRow row) noexcept;
    void AdjustSelected(int delta_ms) noexcept;

    [[nodiscard]] TimingCommand Confirm() const noexcept;
    [[nodiscard]] TimingCommand Back() const noexcept;

    void MarkSaveFailed() noexcept;
    void MarkSaveSucceeded() noexcept;

    [[nodiscard]] TimingOffsets original() const noexcept {
        return original_;
    }

    [[nodiscard]] TimingOffsets staged() const noexcept {
        return staged_;
    }

    [[nodiscard]] TimingRow row() const noexcept {
        return row_;
    }

    [[nodiscard]] SaveStatus status() const noexcept {
        return status_;
    }

    [[nodiscard]] bool dirty() const noexcept {
        return staged_ != original_;
    }

private:
    TimingOffsets original_{};
    TimingOffsets staged_{};
    TimingRow row_{TimingRow::MusicOffset};
    SaveStatus status_{SaveStatus::Idle};
};

[[nodiscard]] std::array<char, 8> FormatOffsetMs(int value) noexcept;

} // namespace gc::test_mode_timing
