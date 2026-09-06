#include "Patches/WindowedWidescreen/WindowedWidescreenProfile.h"
#include <algorithm>
namespace gc::windowed_widescreen {
std::expected<WidescreenGameAbi, game_version::PlanError> BuildWidescreenGameAbi(
    const runtime_image::RuntimeImage& image, const WindowedWidescreenProfile& profile,
    const game_version::ApprovedVersionedPlan& plan) noexcept {
    using namespace game_version;
    const auto invalid = [&](std::string_view site) {
        return std::unexpected(PlanError{.stage = PlanStage::invalid_plan, .context = plan.context(),
            .feature = FeatureId::windowed_widescreen, .site = site});
    };
    const auto* build = std::get_if<GameBuild>(&plan.context().build);
    const auto* variant = std::get_if<GameImageVariant>(&plan.context().variant);
    if (!build || !variant || *build != profile.build || *variant != profile.variant ||
        image.base() != plan.image_base() || image.size() != plan.image_size())
        return invalid("runtime_image_binding");
    const auto check = [&](const SiteContract& expected) -> std::expected<void, PlanError> {
        const auto site = std::ranges::find_if(plan.sites(), [&](const ApprovedSite& actual) {
            const auto& contract = actual.contract();
            return contract.feature == expected.feature && contract.site == expected.site &&
                contract.kind == expected.kind && contract.rva == expected.rva &&
                contract.protected_span == expected.protected_span;
        });
        if (site == plan.sites().end()) return invalid(expected.site);
        const auto target = image.Resolve({"WindowedWidescreen", expected.site, expected.rva},
            std::max<std::size_t>(expected.protected_span, expected.original.size));
        if (!target) return std::unexpected(PlanError{.stage = PlanStage::address_range,
            .context = plan.context(), .feature = expected.feature, .site = expected.site,
            .rva = expected.rva, .memory = target.error()});
        if (*target != site->address) return invalid(expected.site);
        return {};
    };
    for (const auto& row : profile.byte_contracts)
        if (const auto result = check(row.contract); !result) return std::unexpected(result.error());
    for (const auto& row : profile.pointer_contracts)
        if (const auto result = check(row.contract); !result) return std::unexpected(result.error());
    WidescreenGameAbi abi{.layout = profile.layout, .hook_count = profile.hook_order.size(),
        .selected_hud_draws_only = profile.selected_hud_draws_only};
    const auto resolve = [&](runtime_image::Rva rva, std::size_t size, std::string_view site)
        -> std::expected<std::uintptr_t, PlanError> {
        const auto address = image.Resolve({"WindowedWidescreen", site, rva}, size);
        if (!address) return std::unexpected(PlanError{.stage = PlanStage::address_range,
            .context = plan.context(), .feature = FeatureId::windowed_widescreen,
            .site = site, .rva = rva, .memory = address.error()});
        return *address;
    };
    {
        const auto address = resolve(profile.layout.main_config_vtable, sizeof(std::uintptr_t), "main_config_vtable");
        if (!address) return std::unexpected(address.error());
        abi.main_config_vtable = *address;
    }
    {
        const auto address = resolve(profile.layout.batch_queue_pointer, sizeof(std::uintptr_t), "batch_queue_pointer");
        if (!address) return std::unexpected(address.error());
        abi.batch_queue_pointer = *address;
    }
    {
        const auto address = resolve(profile.layout.movie_clip_draw_visitor_vtable, sizeof(std::uintptr_t), "movie_clip_draw_visitor_vtable");
        if (!address) return std::unexpected(address.error());
        abi.movie_clip_draw_visitor_vtable = *address;
    }
    {
        const auto address = resolve(profile.layout.common_2d_vtable, sizeof(std::uintptr_t), "common_2d_vtable");
        if (!address) return std::unexpected(address.error());
        abi.common_2d_vtable = *address;
    }
    {
        const auto address = resolve(profile.layout.common_3d_vtable, sizeof(std::uintptr_t), "common_3d_vtable");
        if (!address) return std::unexpected(address.error());
        abi.common_3d_vtable = *address;
    }
    for (const auto& row : profile.byte_contracts) {
        if (row.site != WidescreenContractSite::batch_flush &&
            row.site != WidescreenContractSite::clip_continuation) continue;
        const auto address = resolve(row.contract.rva, row.contract.original.size, row.contract.site);
        if (!address) return std::unexpected(address.error());
        if (row.site == WidescreenContractSite::batch_flush)
            abi.batch_flush = reinterpret_cast<native::BatchFlush>(*address);
        else abi.clip_continuation = *address;
    }
    for (const auto& row : profile.pointer_contracts) {
        const auto address = resolve(row.target_rva, 1, row.contract.site);
        if (!address) return std::unexpected(address.error());
        switch (row.site) {
        case WidescreenContractSite::config_width_setter:
            abi.config_width_setter = reinterpret_cast<native::ConfigDimensionSetter>(*address); break;
        case WidescreenContractSite::config_height_setter:
            abi.config_height_setter = reinterpret_cast<native::ConfigDimensionSetter>(*address); break;
        case WidescreenContractSite::config_resize_setter:
            abi.config_resize_setter = reinterpret_cast<native::ConfigResizeSetter>(*address); break;
        case WidescreenContractSite::config_minmax_setter:
            abi.config_minmax_setter = reinterpret_cast<native::ConfigMinmaxSetter>(*address); break;
        case WidescreenContractSite::config_mode_setter:
            abi.config_mode_setter = reinterpret_cast<native::ConfigModeSetter>(*address); break;
        default: break;
        }
    }
    return abi;
}
    const char* WidescreenContractSiteName(
        const WidescreenContractSite site) noexcept
    {
        switch (site)
        {
        case WidescreenContractSite::none: return "none";
        case WidescreenContractSite::stage_title_draw_begin: return "stage_title_draw_begin";
        case WidescreenContractSite::stage_title_draw_end: return "stage_title_draw_end";
        case WidescreenContractSite::stage_players_draw_begin: return "stage_players_draw_begin";
        case WidescreenContractSite::stage_players_draw_end: return "stage_players_draw_end";
        case WidescreenContractSite::timed_text_draw_begin: return "timed_text_draw_begin";
        case WidescreenContractSite::timed_text_draw_end: return "timed_text_draw_end";
        case WidescreenContractSite::bar_difficulty_a_begin: return "bar_difficulty_a_begin";
        case WidescreenContractSite::bar_difficulty_a_end: return "bar_difficulty_a_end";
        case WidescreenContractSite::bar_difficulty_b_begin: return "bar_difficulty_b_begin";
        case WidescreenContractSite::bar_difficulty_b_end: return "bar_difficulty_b_end";
        case WidescreenContractSite::bar_panel_480_begin: return "bar_panel_480_begin";
        case WidescreenContractSite::bar_panel_480_end: return "bar_panel_480_end";
        case WidescreenContractSite::bar_panel_524_begin: return "bar_panel_524_begin";
        case WidescreenContractSite::bar_panel_524_end: return "bar_panel_524_end";
        case WidescreenContractSite::bar_panel_568_begin: return "bar_panel_568_begin";
        case WidescreenContractSite::bar_panel_568_end: return "bar_panel_568_end";
        case WidescreenContractSite::bar_stage_panel_begin: return "bar_stage_panel_begin";
        case WidescreenContractSite::bar_stage_panel_end: return "bar_stage_panel_end";
        case WidescreenContractSite::bar_stage_current_begin: return "bar_stage_current_begin";
        case WidescreenContractSite::bar_stage_current_end: return "bar_stage_current_end";
        case WidescreenContractSite::bar_stage_total_begin: return "bar_stage_total_begin";
        case WidescreenContractSite::bar_stage_total_end: return "bar_stage_total_end";
        case WidescreenContractSite::bar_gauge_begin: return "bar_gauge_begin";
        case WidescreenContractSite::bar_gauge_end: return "bar_gauge_end";
        case WidescreenContractSite::bar_panel_216_begin: return "bar_panel_216_begin";
        case WidescreenContractSite::bar_panel_216_end: return "bar_panel_216_end";
        case WidescreenContractSite::bar_score_panel_begin: return "bar_score_panel_begin";
        case WidescreenContractSite::bar_score_panel_end: return "bar_score_panel_end";
        case WidescreenContractSite::bar_score_digits_begin: return "bar_score_digits_begin";
        case WidescreenContractSite::bar_score_digits_end: return "bar_score_digits_end";
        case WidescreenContractSite::bar_extra_panel_a_begin: return "bar_extra_panel_a_begin";
        case WidescreenContractSite::bar_extra_panel_a_end: return "bar_extra_panel_a_end";
        case WidescreenContractSite::bar_extra_digits_a_begin: return "bar_extra_digits_a_begin";
        case WidescreenContractSite::bar_extra_digits_a_end: return "bar_extra_digits_a_end";
        case WidescreenContractSite::bar_extra_panel_b_begin: return "bar_extra_panel_b_begin";
        case WidescreenContractSite::bar_extra_panel_b_end: return "bar_extra_panel_b_end";
        case WidescreenContractSite::bar_extra_digits_b_begin: return "bar_extra_digits_b_begin";
        case WidescreenContractSite::bar_extra_digits_b_end: return "bar_extra_digits_b_end";
        case WidescreenContractSite::bar_mode_panel_begin: return "bar_mode_panel_begin";
        case WidescreenContractSite::bar_mode_panel_end: return "bar_mode_panel_end";
        case WidescreenContractSite::bar_player_panel_begin: return "bar_player_panel_begin";
        case WidescreenContractSite::bar_player_panel_end: return "bar_player_panel_end";
        case WidescreenContractSite::bar_status_panel_begin: return "bar_status_panel_begin";
        case WidescreenContractSite::bar_status_panel_end: return "bar_status_panel_end";
        case WidescreenContractSite::bar_names_end: return "bar_names_end";
        case WidescreenContractSite::chain_label_end: return "chain_label_end";
        case WidescreenContractSite::chain_digits_end: return "chain_digits_end";
        case WidescreenContractSite::chain_glow_end: return "chain_glow_end";
        case WidescreenContractSite::hundred_digits_end: return "hundred_digits_end";
        case WidescreenContractSite::effect_packet_end: return "effect_packet_end";
        case WidescreenContractSite::bar_names_begin: return "bar_names_begin";
        case WidescreenContractSite::chain_glow_begin: return "chain_glow_begin";
        case WidescreenContractSite::hundred_digits_begin: return "hundred_digits_begin";
        case WidescreenContractSite::effect_packet_allocated: return "effect_packet_allocated";
        case WidescreenContractSite::effect_packet_begin: return "effect_packet_begin";
        case WidescreenContractSite::config_apply: return "config_apply";
        case WidescreenContractSite::window_device_create: return "window_device_create";
        case WidescreenContractSite::frame_begin: return "frame_begin";
        case WidescreenContractSite::frame_end: return "frame_end";
        case WidescreenContractSite::task_dispatch: return "task_dispatch";
        case WidescreenContractSite::test_mode_native_begin: return "test_mode_native_begin";
        case WidescreenContractSite::test_mode_native_end: return "test_mode_native_end";
        case WidescreenContractSite::screen_width_int: return "screen_width_int";
        case WidescreenContractSite::screen_height_int: return "screen_height_int";
        case WidescreenContractSite::screen_width_float: return "screen_width_float";
        case WidescreenContractSite::screen_height_float: return "screen_height_float";
        case WidescreenContractSite::target_width_int: return "target_width_int";
        case WidescreenContractSite::target_height_int: return "target_height_int";
        case WidescreenContractSite::target_width_float: return "target_width_float";
        case WidescreenContractSite::target_height_float: return "target_height_float";
        case WidescreenContractSite::logical_resolution_set: return "logical_resolution_set";
        case WidescreenContractSite::logical_target_width_set: return "logical_target_width_set";
        case WidescreenContractSite::logical_target_height_set: return "logical_target_height_set";
        case WidescreenContractSite::viewport_reset: return "viewport_reset";
        case WidescreenContractSite::mouse_debug_poll: return "mouse_debug_poll";
        case WidescreenContractSite::reset_pre: return "reset_pre";
        case WidescreenContractSite::reset_post: return "reset_post";
        case WidescreenContractSite::gameplay_stage_background: return "gameplay_stage_background";
        case WidescreenContractSite::gameplay_track: return "gameplay_track";
        case WidescreenContractSite::gameplay_effects: return "gameplay_effects";
        case WidescreenContractSite::gameplay_effects_end: return "gameplay_effects_end";
        case WidescreenContractSite::gameplay_hud_projection: return "gameplay_hud_projection";
        case WidescreenContractSite::chain_label_begin: return "chain_label_begin";
        case WidescreenContractSite::chain_digits_begin: return "chain_digits_begin";
        case WidescreenContractSite::gameplay_feedback_draw_begin: return "gameplay_feedback_draw_begin";
        case WidescreenContractSite::gameplay_feedback_draw_end: return "gameplay_feedback_draw_end";
        case WidescreenContractSite::clip_default: return "clip_default";
        case WidescreenContractSite::clip_gate: return "clip_gate";
        case WidescreenContractSite::clip_continuation: return "clip_continuation";
        case WidescreenContractSite::batch_flush: return "batch_flush";
        case WidescreenContractSite::clip_owner: return "clip_owner";
        case WidescreenContractSite::live_frustum_helper: return "live_frustum_helper";
        case WidescreenContractSite::config_width_setter: return "config_width_setter";
        case WidescreenContractSite::config_height_setter: return "config_height_setter";
        case WidescreenContractSite::config_resize_setter: return "config_resize_setter";
        case WidescreenContractSite::config_minmax_setter: return "config_minmax_setter";
        case WidescreenContractSite::config_mode_setter: return "config_mode_setter";
        case WidescreenContractSite::common_2d_render: return "common_2d_render";
        case WidescreenContractSite::network_status_movie_clip_accept: return "network_status_movie_clip_accept";
        case WidescreenContractSite::network_status_shape_draw_visit: return "network_status_shape_draw_visit";
        case WidescreenContractSite::common_3d_render: return "common_3d_render";
        }
        return "unknown";
    }
} // namespace gc::windowed_widescreen
