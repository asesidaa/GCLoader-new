#include "Patches/TestModeTiming/TimingSettingsGameAbi.h"
#include "Patches/TestModeTiming/TestModeTimingProfile.h"
#include <Windows.h>
#include <algorithm>

namespace gc::test_mode_timing {
namespace {
const TimingGameAbi& AbiFromContext(void* context) noexcept {
    return *static_cast<const TimingGameAbi*>(context);
}
bool ProductionWriteGameOffset(void* context, int value) noexcept {
    __try {
        *AbiFromContext(context).game_time_offset = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionWriteJudgOffset(void* context, int value) noexcept {
    __try {
        *AbiFromContext(context).judg_time_offset = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* ProductionGetTimingManager(void* context) noexcept {
    __try {
        const auto get_manager = AbiFromContext(context).get_timing_manager;
        return get_manager();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool ProductionSetGameTime(
    void* context,
    void* manager,
    int value) noexcept {
    __try {
        const auto setter = AbiFromContext(context).set_game_time;
        setter(manager, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionSetJudgTime(
    void* context,
    void* manager,
    int value) noexcept {
    __try {
        const auto setter = AbiFromContext(context).set_judg_time;
        setter(manager, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


}
std::expected<TimingGameAbi, game_version::PlanError> BuildTimingGameAbi(
    const runtime_image::RuntimeImage& image, const TestModeTimingProfile& profile,
    const game_version::ApprovedVersionedPlan& plan) noexcept {
    using namespace game_version;
    const auto invalid = [&](std::string_view site) {
        return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
            .context = plan.context(), .feature = FeatureId::test_mode_timing, .site = site});
    };
    if (plan.context().build != SelectedBuild{profile.build} ||
        plan.context().variant != SelectedVariant{profile.variant} ||
        image.base() != plan.image_base() || image.size() != plan.image_size())
        return invalid("runtime_image_binding");
    std::array<std::uintptr_t, 31> addresses{};
    for (std::size_t i = 0; i < profile.operations.size(); ++i) {
        const auto& expected = ContractOf(profile.operations[i]);
        const auto site = std::ranges::find_if(plan.sites(), [&](const ApprovedSite& entry) {
            const auto& actual = entry.contract();
            return actual.feature == expected.feature && actual.site == expected.site &&
                actual.rva == expected.rva && actual.kind == expected.kind;
        });
        if (site == plan.sites().end()) return invalid(expected.site);
        const auto resolved = image.Resolve({"test_mode_timing", expected.site, expected.rva},
            std::max<std::size_t>(expected.protected_span, expected.original.size));
        if (!resolved) return std::unexpected(PlanError{.stage = PlanStage::address_range,
            .context = plan.context(), .feature = expected.feature, .site = expected.site,
            .rva = expected.rva, .memory = resolved.error()});
        if (*resolved != site->address) return invalid(expected.site);
        addresses[i] = *resolved;
    }
    TimingGameAbi abi{.layout = profile.layout};
    abi.allocate = reinterpret_cast<GameAllocateFn>(addresses[3]);
    abi.deallocate = reinterpret_cast<GameDeallocateFn>(addresses[4]);
    abi.construct_sound = reinterpret_cast<SoundConstructorFn>(addresses[2]);
    abi.register_child = reinterpret_cast<RegisterChildFn>(addresses[5]);
    abi.base_update = reinterpret_cast<BaseUpdateFn>(addresses[6]);
    abi.set_cell_text = reinterpret_cast<SetCellTextFn>(addresses[7]);
    abi.set_selection = reinterpret_cast<SetSelectionFn>(addresses[8]);
    abi.draw_title = reinterpret_cast<DrawTitleFn>(addresses[9]);
    abi.set_title_position = reinterpret_cast<SetTitlePositionFn>(addresses[10]);
    abi.draw_help = reinterpret_cast<DrawHelpFn>(addresses[11]);
    abi.get_timing_manager = reinterpret_cast<TimingManagerFn>(addresses[12]);
    abi.set_judg_time = reinterpret_cast<TimingSetterFn>(addresses[13]);
    abi.set_game_time = reinterpret_cast<TimingSetterFn>(addresses[14]);
    for (std::size_t i = 0; i < abi.sound_vtable.size(); ++i) {
        const auto& contract = ContractOf(profile.operations[18 + i]);
        const auto target = image.Resolve({"test_mode_timing", contract.site, profile.sound_vtable_targets[i]}, 1);
        if (!target) return std::unexpected(PlanError{.stage = PlanStage::address_range,
            .context = plan.context(), .feature = FeatureId::test_mode_timing, .site = contract.site,
            .rva = profile.sound_vtable_targets[i], .memory = target.error()});
        abi.sound_vtable[i] = *target;
    }
    abi.destroy_sound = reinterpret_cast<ScalarDeletingDestructorFn>(abi.sound_vtable[abi.layout.destructor_slot]);
    const auto judg = image.Resolve({"test_mode_timing", "judg_time_offset", profile.judg_time_offset}, sizeof(int));
    if (!judg) return std::unexpected(PlanError{.stage = PlanStage::address_range,
        .context = plan.context(), .feature = FeatureId::test_mode_timing, .site = "judg_time_offset",
        .rva = profile.judg_time_offset, .memory = judg.error()});
    const auto game = image.Resolve({"test_mode_timing", "game_time_offset", profile.game_time_offset}, sizeof(int));
    if (!game) return std::unexpected(PlanError{.stage = PlanStage::address_range,
        .context = plan.context(), .feature = FeatureId::test_mode_timing, .site = "game_time_offset",
        .rva = profile.game_time_offset, .memory = game.error()});
    abi.judg_time_offset = reinterpret_cast<int*>(*judg);
    abi.game_time_offset = reinterpret_cast<int*>(*game);
    return abi;
}
TimingLiveActions
ProductionTimingLiveActions(const TimingGameAbi& abi) noexcept {
    return {
        .context = const_cast<TimingGameAbi*>(&abi),
        .write_game_time_offset = ProductionWriteGameOffset,
        .write_judg_time_offset = ProductionWriteJudgOffset,
        .get_timing_manager = ProductionGetTimingManager,
        .set_game_time = ProductionSetGameTime,
        .set_judg_time = ProductionSetJudgTime,
    };
}

bool ApplyLiveTiming(
    TimingOffsets offsets,
    const TimingLiveActions& actions) noexcept {
    if (actions.write_game_time_offset == nullptr ||
        actions.write_judg_time_offset == nullptr ||
        actions.get_timing_manager == nullptr ||
        actions.set_game_time == nullptr ||
        actions.set_judg_time == nullptr) {
        return false;
    }
    if (!actions.write_game_time_offset(
            actions.context, offsets.game_ms) ||
        !actions.write_judg_time_offset(
            actions.context, offsets.judge_ms)) {
        return false;
    }
    void* manager = actions.get_timing_manager(actions.context);
    return manager != nullptr &&
        actions.set_game_time(
            actions.context, manager, offsets.game_ms) &&
        actions.set_judg_time(
            actions.context, manager, offsets.judge_ms);
}

bool ApplyLiveTiming(
    const TimingGameAbi& abi,
    TimingOffsets offsets) noexcept {
    return ApplyLiveTiming(
        offsets, ProductionTimingLiveActions(abi));
}

} // namespace gc::test_mode_timing
