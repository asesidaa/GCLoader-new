#include "Patches/TestModeTiming/TimingSettingsPatch.h"

#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"

#include <Windows.h>

#include <plog/Log.h>
#include "Patches/TestModeTiming/TestModeTimingProfile.h"
#include "Diagnostics/FatalProcess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace gc::test_mode_timing {

namespace detail { TimingOriginals g_originals; }

namespace {

inline constexpr std::array<const char*, 4> kTimingRowHelp{
    "LEFT/RIGHT: MUSIC OFFSET",
    "LEFT/RIGHT: JUDGE OFFSET",
    "SAVE VALUES AND RETURN",
    "DISCARD CHANGES AND RETURN",
};

const unsigned char* GameText(const char* text) noexcept {
    return reinterpret_cast<const unsigned char*>(text);
}

struct TimingRuntimeState {
    TimingRuntimeState(
        TimingGameAbi resolved_abi,
        std::filesystem::path config_path)
        : abi{std::move(resolved_abi)},
          store{std::move(config_path)} {
    }

    TimingGameAbi abi{};
    SystemConfigTimingStore store;
    TimingSettingsModel model{};
    std::optional<SystemConfigError> last_store_error{};
    void* carrier{};
    std::array<std::uintptr_t, kSoundVtableSlots> carrier_vtable{};
    bool carrier_ready{};
    std::atomic_bool callback_error_published{false};
};

std::unique_ptr<TimingRuntimeState> g_runtime_owner;
TimingRuntimeState* g_runtime = nullptr;

void LogCallbackFailure(const char* operation) noexcept {
    try {
        if (g_runtime != nullptr &&
            !g_runtime->callback_error_published.exchange(true)) {
            PLOG_ERROR << "TestModeTiming: callback failure operation="
                       << operation;
        }
    } catch (...) {
    }
}

bool ReadPointerField(
    void* object,
    std::size_t offset,
    void** value) noexcept {
    __try {
        *value = *reinterpret_cast<void**>(
            static_cast<std::byte*>(object) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadIntField(
    void* object,
    std::size_t offset,
    int* value) noexcept {
    __try {
        *value = *reinterpret_cast<int*>(
            static_cast<std::byte*>(object) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteIntField(
    void* object,
    std::size_t offset,
    int value) noexcept {
    __try {
        *reinterpret_cast<int*>(
            static_cast<std::byte*>(object) + offset) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteVtable(
    void* object,
    const std::uintptr_t* vtable) noexcept {
    __try {
        *static_cast<const std::uintptr_t**>(object) = vtable;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadLiveOffsets(
    const TimingGameAbi& abi,
    TimingOffsets* offsets) noexcept {
    __try {
        offsets->game_ms = *abi.game_time_offset;
        offsets->judge_ms = *abi.judg_time_offset;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSetSelection(
    const TimingGameAbi& abi,
    void* grid,
    int selection) noexcept {
    __try {
        abi.set_selection(grid, selection);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool DrawNativeTitle(void* opaque, const char* text) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        runtime.abi.draw_title(
            GameText(text), GameText(text), GameText(text), 4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetNativeTitlePosition(void* opaque, int x, int y) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        runtime.abi.set_title_position(x, y);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetNativeCell(
    void* opaque,
    void* grid,
    int row,
    int column,
    const char* text) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        runtime.abi.set_cell_text(
            grid, row, column, GameText(text));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool DrawNativeHelp(void* opaque, const char* text) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        runtime.abi.draw_help(GameText(text), GameText(text), 4, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

TimingRenderActions ProductionRenderActions(
    TimingRuntimeState& runtime) noexcept {
    return {
        .context = &runtime,
        .draw_title = DrawNativeTitle,
        .set_title_position = SetNativeTitlePosition,
        .set_cell = SetNativeCell,
        .draw_help = DrawNativeHelp,
    };
}

bool SelectionToRow(int selection, TimingRow* row) noexcept {
    if (selection < 0 || selection > 3) {
        return false;
    }
    *row = static_cast<TimingRow>(selection);
    return true;
}

bool SynchronizeSelection(
    TimingRuntimeState& runtime,
    int selection) noexcept {
    TimingRow row{};
    if (!SelectionToRow(selection, &row)) {
        return false;
    }
    runtime.model.SetRow(row);
    return true;
}

bool ReadCarrierGrid(
    const TimingRuntimeState& runtime,
    void* self,
    void** grid) noexcept {
    return self == runtime.carrier &&
        ReadPointerField(self, runtime.abi.layout.form_grid, grid) && *grid != nullptr;
}

CarrierCallbacks RuntimeCarrierCallbacks() noexcept {
    return {
        .activate = reinterpret_cast<std::uintptr_t>(&CarrierActivate),
        .render = reinterpret_cast<std::uintptr_t>(&CarrierRender),
        .confirm = reinterpret_cast<std::uintptr_t>(&CarrierConfirm),
        .back = reinterpret_cast<std::uintptr_t>(&CarrierBack),
        .increment = reinterpret_cast<std::uintptr_t>(&CarrierIncrement),
        .decrement = reinterpret_cast<std::uintptr_t>(&CarrierDecrement),
    };
}

void* RuntimeAllocate(void* opaque, std::size_t size) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        return runtime.abi.allocate(size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* RuntimeConstruct(
    void* opaque,
    void* raw,
    void* parent) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        return runtime.abi.construct_sound(raw, parent);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool RuntimePrepare(void* opaque, void* carrier) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    if (!PrepareCarrierLayout(carrier, runtime.abi.layout)) {
        return false;
    }
    return WriteVtable(carrier, runtime.carrier_vtable.data());
}

bool RuntimeRegister(
    void* opaque,
    void* owner,
    int index,
    void* carrier) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        return runtime.abi.register_child(owner, index, carrier) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void RuntimeDeallocate(void* opaque, void* raw) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        runtime.abi.deallocate(raw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void RuntimeDestroy(
    void* opaque,
    void* carrier,
    unsigned char flag) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    __try {
        runtime.abi.destroy_sound(carrier, flag);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

CarrierLifecycleActions RuntimeLifecycleActions(
    TimingRuntimeState& runtime) noexcept {
    return {
        .context = &runtime,
        .allocate = RuntimeAllocate,
        .construct = RuntimeConstruct,
        .prepare = RuntimePrepare,
        .register_child = RuntimeRegister,
        .deallocate = RuntimeDeallocate,
        .destroy = RuntimeDestroy,
    };
}

bool RuntimeSave(
    void* opaque,
    TimingOffsets offsets,
    SaveOutcome* outcome) noexcept {
    auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    auto saved = runtime.store.Save(offsets);
    if (!saved) {
        runtime.last_store_error = saved.error();
        return false;
    }
    runtime.last_store_error.reset();
    *outcome = *saved;
    return true;
}

bool RuntimeApplyLive(void* opaque, TimingOffsets offsets) noexcept {
    const auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
    return ApplyLiveTiming(runtime.abi, offsets);
}

void RuntimeStatusChanged(void*, SaveStatus) noexcept {
}

void RuntimeSaveFailed(void* opaque) noexcept {
    try {
        const auto& runtime = *static_cast<TimingRuntimeState*>(opaque);
        if (!runtime.last_store_error) {
            PLOG_ERROR << "TestModeTiming: system.cfg save failed"
                       << " path=" << runtime.store.path().string()
                       << " stage=unknown";
            return;
        }
        const auto& error = *runtime.last_store_error;
        PLOG_ERROR << "TestModeTiming: system.cfg save failed"
                   << " path=" << runtime.store.path().string()
                   << " stage=" << static_cast<int>(error.stage)
                   << " win32=" << error.win32_error
                   << " cleanup=" << error.cleanup_error;
    } catch (...) {
    }
}

void RuntimeApplyFailed(void*) noexcept {
    try {
        PLOG_FATAL
            << "TestModeTiming: persisted values but live ABI apply failed";
    } catch (...) {
    }
}

void RuntimeSaveSucceeded(
    void*,
    TimingOffsets before,
    TimingOffsets staged,
    SaveOutcome outcome) noexcept {
    try {
        PLOG_INFO << "TestModeTiming: saved GameTimeOffset "
                  << before.game_ms << " -> " << staged.game_ms
                  << ", JudgTimeOffset " << before.judge_ms << " -> "
                  << staged.judge_ms << ", config="
                  << (outcome == SaveOutcome::Changed
                          ? "replaced"
                          : "already matched");
    } catch (...) {
    }
}

TimingCommitActions ProductionCommitActions(
    TimingRuntimeState& runtime) noexcept {
    return {
        .context = &runtime,
        .save = RuntimeSave,
        .apply_live = RuntimeApplyLive,
        .status_changed = RuntimeStatusChanged,
        .save_failed = RuntimeSaveFailed,
        .apply_failed = RuntimeApplyFailed,
        .save_succeeded = RuntimeSaveSucceeded,
    };
}

std::expected<std::filesystem::path, DWORD> ResolveConfigPath() {
    constexpr wchar_t relative[] = L"data\\system.cfg";
    const DWORD required = GetFullPathNameW(relative, 0, nullptr, nullptr);
    if (required == 0) {
        return std::unexpected(GetLastError());
    }

    std::wstring buffer(required, L'\0');
    const DWORD copied = GetFullPathNameW(
        relative,
        required,
        buffer.data(),
        nullptr);
    if (copied == 0 || copied >= required) {
        return std::unexpected(
            copied == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
    }
    buffer.resize(copied);
    return std::filesystem::path{std::move(buffer)};
}

bool SetMainMenuCell(
    TimingRuntimeState& runtime,
    void* grid,
    int row,
    const char* text) noexcept {
    return SetNativeCell(&runtime, grid, row, 0, text);
}

void DegradeMainMenu(
    TimingRuntimeState& runtime,
    void* main_form,
    void* grid) noexcept {
    WriteIntField(main_form, runtime.abi.layout.form_row_count, 11);
    WriteIntField(grid, runtime.abi.layout.grid_row_count, 11);
    SetMainMenuCell(runtime, grid, 10, "EXIT");
    runtime.carrier = nullptr;
    runtime.carrier_ready = false;
}

class SelectionGuard {
public:
    SelectionGuard(
        void* grid,
        std::size_t selection_offset,
        int native_selection,
        int actual_selection) noexcept
        : grid_{grid},
          selection_offset_{selection_offset},
          actual_{actual_selection},
          changed_{native_selection != actual_selection},
          ready_{!changed_ || WriteIntField(
              grid_, selection_offset_, native_selection)} {
    }

    SelectionGuard(const SelectionGuard&) = delete;
    SelectionGuard& operator=(const SelectionGuard&) = delete;

    ~SelectionGuard() { Restore(); }

    [[nodiscard]] bool ready() const noexcept { return ready_; }

    void Restore() noexcept {
        if (changed_ && !restored_) {
            WriteIntField(grid_, selection_offset_, actual_);
            restored_ = true;
        }
    }

private:
    void* grid_{};
    std::size_t selection_offset_{};
    int actual_{};
    bool changed_{};
    bool ready_{};
    bool restored_{};
};

} // namespace

std::array<std::uintptr_t, kSoundVtableSlots> BuildCarrierVtable(
    std::span<const std::uintptr_t, kSoundVtableSlots> native,
    const CarrierCallbacks& callbacks,
    BaseUpdateFn base_update,
    const TimingNativeLayout& layout) noexcept {
    std::array<std::uintptr_t, kSoundVtableSlots> result{};
    std::ranges::copy(native, result.begin());
    result[layout.activate_slot] = callbacks.activate;
    result[layout.update_slot] = reinterpret_cast<std::uintptr_t>(base_update);
    result[layout.render_slot] = callbacks.render;
    result[layout.confirm_slot] = callbacks.confirm;
    result[layout.back_slot] = callbacks.back;
    result[layout.increment_slot] = callbacks.increment;
    result[layout.decrement_slot] = callbacks.decrement;
    return result;
}

bool RenderTimingSettings(
    const TimingSettingsModel& model,
    void* grid,
    const TimingRenderActions& actions) noexcept {
    if (grid == nullptr || actions.draw_title == nullptr ||
        actions.set_title_position == nullptr ||
        actions.set_cell == nullptr || actions.draw_help == nullptr) {
        return false;
    }

    const auto game_offset = FormatOffsetMs(model.staged().game_ms);
    const auto judge_offset = FormatOffsetMs(model.staged().judge_ms);
    if (!actions.draw_title(actions.context, kTimingTitle) ||
        !actions.set_title_position(actions.context, 4, 2) ||
        !actions.set_cell(
            actions.context, grid, 0, 0, "MUSIC OFFSET") ||
        !actions.set_cell(
            actions.context, grid, 0, 1, game_offset.data()) ||
        !actions.set_cell(
            actions.context, grid, 1, 0, "JUDGE OFFSET") ||
        !actions.set_cell(
            actions.context, grid, 1, 1, judge_offset.data()) ||
        !actions.set_cell(
            actions.context, grid, 2, 0, "SAVE AND BACK") ||
        !actions.set_cell(actions.context, grid, 2, 1, "") ||
        !actions.set_cell(actions.context, grid, 3, 0, "CANCEL") ||
        !actions.set_cell(actions.context, grid, 3, 1, "")) {
        return false;
    }

    const auto row = static_cast<std::size_t>(model.row());
    if (row >= kTimingRowHelp.size()) {
        return false;
    }
    const char* help = model.status() == SaveStatus::Failed
        ? kTimingSaveFailureHelp
        : kTimingRowHelp[row];
    return actions.draw_help(actions.context, help);
}

bool PrepareCarrierLayout(void* carrier, const TimingNativeLayout& layout) noexcept {
    static_assert(sizeof(void*) == 4);
    if (carrier == nullptr) {
        return false;
    }

    __try {
        auto* bytes = static_cast<std::byte*>(carrier);
        void* grid = *reinterpret_cast<void**>(bytes + layout.form_grid);
        if (grid == nullptr) {
            return false;
        }
        *reinterpret_cast<int*>(bytes + layout.form_row_count) = 4;
        *reinterpret_cast<int*>(bytes + layout.form_active_child) = -1;
        *reinterpret_cast<int*>(
            static_cast<std::byte*>(grid) + layout.grid_row_count) = 4;
        *reinterpret_cast<int*>(
            static_cast<std::byte*>(grid) + layout.grid_selection) = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CreateTimingCarrier(
    void* constructor_parent,
    void* owner,
    const CarrierLifecycleActions& actions,
    void** carrier_out,
    const TimingNativeLayout& layout) noexcept {
    if (carrier_out == nullptr) {
        return false;
    }
    *carrier_out = nullptr;
    if (constructor_parent == nullptr || owner == nullptr ||
        actions.allocate == nullptr || actions.construct == nullptr ||
        actions.prepare == nullptr || actions.register_child == nullptr ||
        actions.deallocate == nullptr || actions.destroy == nullptr) {
        return false;
    }

    void* raw = actions.allocate(actions.context, layout.sound_form_size);
    if (raw == nullptr) {
        return false;
    }
    void* carrier =
        actions.construct(actions.context, raw, constructor_parent);
    if (carrier == nullptr) {
        actions.deallocate(actions.context, raw);
        return false;
    }
    if (!actions.prepare(actions.context, carrier)) {
        actions.destroy(actions.context, carrier, 1);
        return false;
    }
    if (!actions.register_child(actions.context, owner, 10, carrier)) {
        actions.destroy(actions.context, carrier, 1);
        return false;
    }

    *carrier_out = carrier;
    return true;
}

int CommitTimingSelection(
    TimingSettingsModel& model,
    const TimingCommitActions& actions) noexcept {
    const auto command = model.Confirm();
    if (command == TimingCommand::None) {
        return 0;
    }
    if (command == TimingCommand::Cancel) {
        return CancelTimingEdit(model);
    }

    const auto before = model.original();
    const auto staged = model.staged();
    const auto notify_status = [&](SaveStatus status) noexcept {
        if (actions.status_changed != nullptr) {
            actions.status_changed(actions.context, status);
        }
    };

    if (!model.dirty()) {
        model.MarkSaveSucceeded();
        notify_status(SaveStatus::Succeeded);
        return 1;
    }
    if (actions.save == nullptr || actions.apply_live == nullptr) {
        model.MarkSaveFailed();
        notify_status(SaveStatus::Failed);
        if (actions.save_failed != nullptr) {
            actions.save_failed(actions.context);
        }
        return 0;
    }

    SaveOutcome outcome{};
    if (!actions.save(actions.context, staged, &outcome)) {
        model.MarkSaveFailed();
        notify_status(SaveStatus::Failed);
        if (actions.save_failed != nullptr) {
            actions.save_failed(actions.context);
        }
        return 0;
    }
    if (!actions.apply_live(actions.context, staged)) {
        model.MarkSaveFailed();
        notify_status(SaveStatus::Failed);
        if (actions.apply_failed != nullptr) {
            actions.apply_failed(actions.context);
        }
        return 0;
    }

    model.MarkSaveSucceeded();
    notify_status(SaveStatus::Succeeded);
    if (actions.save_succeeded != nullptr) {
        actions.save_succeeded(
            actions.context, before, staged, outcome);
    }
    return 1;
}

int CancelTimingEdit(TimingSettingsModel& model) noexcept {
    model.Activate(model.original());
    return 1;
}

void* __fastcall CarrierActivate(void* self, void*) noexcept {
    try {
        if (g_runtime == nullptr || self != g_runtime->carrier) {
            return nullptr;
        }
        TimingOffsets live{};
        void* grid = nullptr;
        if (!ReadLiveOffsets(g_runtime->abi, &live) ||
            !ReadCarrierGrid(*g_runtime, self, &grid)) {
            LogCallbackFailure("carrier_activate_read");
            return nullptr;
        }
        g_runtime->model.Activate(live);
        if (!CallSetSelection(g_runtime->abi, grid, 0)) {
            LogCallbackFailure("carrier_activate_selection");
            return nullptr;
        }
        return self;
    } catch (...) {
        LogCallbackFailure("carrier_activate_exception");
        return nullptr;
    }
}

void* __fastcall CarrierRender(
    void* self,
    void*,
    int,
    int) noexcept {
    try {
        if (g_runtime == nullptr) {
            return nullptr;
        }
        void* grid = nullptr;
        int selection = -1;
        if (!ReadCarrierGrid(*g_runtime, self, &grid) ||
            !ReadIntField(grid, g_runtime->abi.layout.grid_selection, &selection) ||
            !SynchronizeSelection(*g_runtime, selection) ||
            !RenderTimingSettings(
                g_runtime->model,
                grid,
                ProductionRenderActions(*g_runtime))) {
            LogCallbackFailure("carrier_render");
            return nullptr;
        }
        return self;
    } catch (...) {
        LogCallbackFailure("carrier_render_exception");
        return nullptr;
    }
}

// The native fastcall hook ABI fixes the mutable self pointer type.
// ReSharper disable once CppParameterMayBeConstPtrOrRef
int __fastcall CarrierConfirm(
    void* self,
    void*,
    int,
    int,
    int selection) noexcept {
    try {
        if (g_runtime == nullptr || self != g_runtime->carrier ||
            !SynchronizeSelection(*g_runtime, selection)) {
            LogCallbackFailure("carrier_confirm_selection");
            return 0;
        }
        return CommitTimingSelection(
            g_runtime->model,
            ProductionCommitActions(*g_runtime));
    } catch (...) {
        LogCallbackFailure("carrier_confirm_exception");
        return 0;
    }
}

// The native fastcall hook ABI fixes the mutable self pointer type.
// ReSharper disable once CppParameterMayBeConstPtrOrRef
int __fastcall CarrierBack(
    void* self,
    void*,
    int,
    int) noexcept {
    try {
        if (g_runtime == nullptr || self != g_runtime->carrier) {
            LogCallbackFailure("carrier_back_self");
            return 0;
        }
        return CancelTimingEdit(g_runtime->model);
    } catch (...) {
        LogCallbackFailure("carrier_back_exception");
        return 0;
    }
}

// The native fastcall hook ABI fixes the mutable self pointer type.
// ReSharper disable once CppParameterMayBeConstPtrOrRef
int __fastcall CarrierIncrement(
    void* self,
    void*,
    int,
    int,
    int selection) noexcept {
    try {
        if (g_runtime == nullptr || self != g_runtime->carrier ||
            !SynchronizeSelection(*g_runtime, selection)) {
            LogCallbackFailure("carrier_increment_selection");
            return 0;
        }
        g_runtime->model.AdjustSelected(1);
        return 0;
    } catch (...) {
        LogCallbackFailure("carrier_increment_exception");
        return 0;
    }
}

// The native fastcall hook ABI fixes the mutable self pointer type.
// ReSharper disable once CppParameterMayBeConstPtrOrRef
int __fastcall CarrierDecrement(
    void* self,
    void*,
    int,
    int,
    int selection) noexcept {
    try {
        if (g_runtime == nullptr || self != g_runtime->carrier ||
            !SynchronizeSelection(*g_runtime, selection)) {
            LogCallbackFailure("carrier_decrement_selection");
            return 0;
        }
        g_runtime->model.AdjustSelected(-1);
        return 0;
    } catch (...) {
        LogCallbackFailure("carrier_decrement_exception");
        return 0;
    }
}

void* __fastcall MainConstructorHook(
    void* self,
    void*,
    void* parent) noexcept {
    try {
        if (g_runtime == nullptr || !detail::g_originals.main_constructor) {
            return nullptr;
        }
        gc::absolute_judgement::
            EndAbsoluteJudgementSemanticStageForTestMode();
        void* result =
            detail::g_originals.main_constructor(
                self, parent);
        void* main_form = result != nullptr ? result : self;
        void* grid = nullptr;
        if (!ReadPointerField(main_form, g_runtime->abi.layout.form_grid, &grid) ||
            grid == nullptr ||
            !SetMainMenuCell(*g_runtime, grid, 10, kTimingTitle) ||
            !SetMainMenuCell(*g_runtime, grid, 11, "EXIT")) {
            if (grid != nullptr) {
                DegradeMainMenu(*g_runtime, main_form, grid);
            }
            LogCallbackFailure("main_constructor_labels");
            return result;
        }

        void* carrier = nullptr;
        if (!CreateTimingCarrier(
                parent,
                main_form,
                RuntimeLifecycleActions(*g_runtime),
                &carrier, g_runtime->abi.layout)) {
            DegradeMainMenu(*g_runtime, main_form, grid);
            LogCallbackFailure("main_constructor_carrier");
            return result;
        }
        g_runtime->carrier = carrier;
        g_runtime->carrier_ready = true;
        return result;
    } catch (...) {
        LogCallbackFailure("main_constructor_exception");
        return nullptr;
    }
}

void* __fastcall MainRenderHook(
    void* self,
    void*,
    int frame,
    int input) noexcept {
    try {
        if (g_runtime == nullptr || !detail::g_originals.main_render) {
            return nullptr;
        }
        void* grid = nullptr;
        int actual = -1;
        if (!ReadPointerField(self, g_runtime->abi.layout.form_grid, &grid) ||
            grid == nullptr ||
            !ReadIntField(grid, g_runtime->abi.layout.grid_selection, &actual)) {
            LogCallbackFailure("main_render_selection");
            return nullptr;
        }

        const auto route = g_runtime->carrier_ready
            ? RouteMainSelection(actual)
            : MainRenderRoute{actual, false};
        SelectionGuard guard{grid, g_runtime->abi.layout.grid_selection, route.native_selection, actual};
        if (!guard.ready()) {
            LogCallbackFailure("main_render_route");
            return nullptr;
        }
        void* result = detail::g_originals.main_render(
            self, frame, input);
        guard.Restore();
        if (route.draw_timing_help && g_runtime->carrier_ready &&
            !DrawNativeHelp(g_runtime, kTimingMainHelp)) {
            LogCallbackFailure("main_render_help");
        }
        return result;
    } catch (...) {
        LogCallbackFailure("main_render_exception");
        return nullptr;
    }
}

std::expected<void, game_version::PlanError> PrepareTestModeTimingRuntime(
    const game_version::ApprovedVersionedPlan& plan,
    const runtime_image::RuntimeImage& image) noexcept {
    using namespace game_version;
    const auto invalid = [&](std::string_view site, PlanStage stage = PlanStage::invalid_plan) {
        return std::unexpected(PlanError{.stage = stage, .context = plan.context(),
            .feature = FeatureId::test_mode_timing, .site = site});
    };
    try {
        if (g_runtime) return invalid("runtime_already_prepared");
        const auto* build = std::get_if<GameBuild>(&plan.context().build);
        const auto* variant = std::get_if<GameImageVariant>(&plan.context().variant);
        const auto* profile = build && variant ? ProfileFor(*build, *variant) : nullptr;
        if (!profile) return invalid("profile", PlanStage::unsupported_feature);
        auto abi = BuildTimingGameAbi(image, *profile, plan);
        if (!abi) return std::unexpected(abi.error());
        auto path = ResolveConfigPath();
        if (!path) {
            PLOG_ERROR << "TestModeTiming: config path resolution failed win32=" << path.error();
            return invalid("config_path");
        }
        auto candidate = std::make_unique<TimingRuntimeState>(std::move(*abi), std::move(*path));
        candidate->carrier_vtable = BuildCarrierVtable(candidate->abi.sound_vtable,
            RuntimeCarrierCallbacks(), candidate->abi.base_update, candidate->abi.layout);
        // The owner and constructed table stay at stable addresses for native callbacks.
        // Actual game allocation/construction still happens in MainConstructorHook.
        g_runtime_owner = std::move(candidate);
        g_runtime = g_runtime_owner.get();
        return {};
    } catch (...) {
        return invalid("runtime_allocation", PlanStage::allocation);
    }
}
void CompleteTestModeTimingStartup() noexcept {
    if (!g_runtime || !detail::g_originals.main_constructor || !detail::g_originals.main_render)
        diagnostics::AbortProcess({"TestModeTiming: startup publication is incomplete",
            L"GCLoader could not initialize test-mode timing. Check loader-log.txt.",
            L"GCLoader executable validation error"});
    try { PLOG_INFO << "TestModeTiming: installed carrier_slot=10 hooks=2"; } catch (...) {}
}

} // namespace gc::test_mode_timing
