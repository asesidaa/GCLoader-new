#include "Patches/TestModeTiming/TimingSettingsModel.h"

#include <algorithm>
#include <cstdio>

namespace gc::test_mode_timing {

void TimingSettingsModel::Activate(TimingOffsets live) noexcept {
    original_ = live;
    staged_ = {
        std::clamp(live.game_ms, kMinimumOffsetMs, kMaximumOffsetMs),
        std::clamp(live.judge_ms, kMinimumOffsetMs, kMaximumOffsetMs),
    };
    row_ = TimingRow::MusicOffset;
    status_ = SaveStatus::Idle;
}

void TimingSettingsModel::SetRow(TimingRow row) noexcept {
    row_ = row;
}

void TimingSettingsModel::AdjustSelected(int delta_ms) noexcept {
    int* value = nullptr;
    if (row_ == TimingRow::MusicOffset) {
        value = &staged_.game_ms;
    } else if (row_ == TimingRow::JudgeOffset) {
        value = &staged_.judge_ms;
    }

    if (value == nullptr) {
        return;
    }

    const int before = *value;
    const auto candidate = static_cast<long long>(*value) + delta_ms;
    *value = static_cast<int>(std::clamp(
        candidate,
        static_cast<long long>(kMinimumOffsetMs),
        static_cast<long long>(kMaximumOffsetMs)));
    if (*value != before) {
        status_ = SaveStatus::Idle;
    }
}

TimingCommand TimingSettingsModel::Confirm() const noexcept {
    if (row_ == TimingRow::SaveAndBack) {
        return TimingCommand::Save;
    }
    if (row_ == TimingRow::Cancel) {
        return TimingCommand::Cancel;
    }
    return TimingCommand::None;
}

// Back is an instance-level menu action even though it does not inspect state.
// ReSharper disable once CppMemberFunctionMayBeStatic
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
TimingCommand TimingSettingsModel::Back() const noexcept {
    return TimingCommand::Cancel;
}

void TimingSettingsModel::MarkSaveFailed() noexcept {
    status_ = SaveStatus::Failed;
}

void TimingSettingsModel::MarkSaveSucceeded() noexcept {
    status_ = SaveStatus::Succeeded;
}

std::array<char, 8> FormatOffsetMs(int value) noexcept {
    std::array<char, 8> result{};
    std::snprintf(result.data(), result.size(), "%+d ms", value);
    return result;
}

} // namespace gc::test_mode_timing
