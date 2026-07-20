#include "Patches/TestModeTiming/TimingSettingsModel.h"

#include <iostream>
#include <limits>
#include <string_view>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

} // namespace

int main() {
    using namespace gc::test_mode_timing;
    int failures = 0;

    TimingSettingsModel model;
    model.Activate({.game_ms = 75, .judge_ms = -80});
    failures += Expect(
        model.original() == TimingOffsets{75, -80},
        "activation preserves original live values");
    failures += Expect(
        model.staged() == TimingOffsets{50, -50},
        "activation clamps staged values");
    failures += Expect(
        model.row() == TimingRow::MusicOffset,
        "activation selects music");
    failures += Expect(model.dirty(), "out-of-range activation is dirty");
    failures += Expect(
        model.status() == SaveStatus::Idle,
        "activation clears status");

    failures += Expect(
        std::string_view{FormatOffsetMs(0).data()} == "+0 ms",
        "zero has explicit sign");
    failures += Expect(
        std::string_view{FormatOffsetMs(-16).data()} == "-16 ms",
        "negative offset is formatted");
    failures += Expect(
        std::string_view{FormatOffsetMs(50).data()} == "+50 ms",
        "positive offset has explicit sign");

    model.Activate({.game_ms = 0, .judge_ms = -16});
    failures += Expect(!model.dirty(), "in-range activation starts clean");
    model.SetRow(TimingRow::MusicOffset);
    model.AdjustSelected(1);
    failures += Expect(
        model.staged().game_ms == 1 && model.staged().judge_ms == -16,
        "music adjustment is isolated");
    model.AdjustSelected(-100);
    failures += Expect(
        model.staged().game_ms == -50,
        "music lower bound saturates");
    model.AdjustSelected(200);
    failures += Expect(
        model.staged().game_ms == 50,
        "music upper bound saturates");
    model.AdjustSelected(std::numeric_limits<int>::max());
    failures += Expect(
        model.staged().game_ms == 50,
        "large positive delta cannot overflow");

    model.Activate({.game_ms = 0, .judge_ms = -16});
    model.SetRow(TimingRow::MusicOffset);
    model.AdjustSelected(1);
    model.AdjustSelected(-1);
    failures += Expect(
        !model.dirty(),
        "returning to original values clears dirty state");

    model.SetRow(TimingRow::JudgeOffset);
    model.AdjustSelected(1);
    failures += Expect(
        model.staged().game_ms == 0 && model.staged().judge_ms == -15,
        "judge adjustment is isolated and one millisecond");

    model.SetRow(TimingRow::SaveAndBack);
    const auto before_action_adjust = model.staged();
    model.MarkSaveFailed();
    model.AdjustSelected(1);
    failures += Expect(
        model.staged() == before_action_adjust,
        "action rows ignore adjustment");
    failures += Expect(
        model.status() == SaveStatus::Failed,
        "action-row adjustment retains failure status");
    failures += Expect(
        model.Confirm() == TimingCommand::Save,
        "save row requests save");

    model.SetRow(TimingRow::Cancel);
    failures += Expect(
        model.Confirm() == TimingCommand::Cancel,
        "cancel row requests cancel");
    model.SetRow(TimingRow::MusicOffset);
    failures += Expect(
        model.Confirm() == TimingCommand::None,
        "offset row ignores confirm");
    failures += Expect(
        model.Back() == TimingCommand::Cancel,
        "back always requests cancel");

    model.Activate({.game_ms = -50, .judge_ms = -16});
    model.MarkSaveFailed();
    model.AdjustSelected(-1);
    failures += Expect(
        model.status() == SaveStatus::Failed,
        "clamped no-op retains failure status");
    model.AdjustSelected(1);
    failures += Expect(
        model.status() == SaveStatus::Idle,
        "real adjustment clears save failure");

    model.MarkSaveSucceeded();
    failures += Expect(
        model.status() == SaveStatus::Succeeded,
        "save success is visible");
    model.Activate({.game_ms = 0, .judge_ms = -16});
    failures += Expect(
        model.status() == SaveStatus::Idle,
        "reactivation clears success status");

    return failures == 0 ? 0 : 1;
}
