#include "Patches/WindowedWidescreen/GameplayHudHooks.h"
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include "Patches/WindowedWidescreen/RenderHooks.h"
#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace gc::windowed_widescreen::detail {
GameplayHudOriginals g_gameplay_originals;

void ResetScopedRenderState(
    WindowedWidescreenRuntime& runtime) noexcept
{
    runtime.active_gameplay_tune = 0;
    runtime.gameplay_frame_active.store(
        false,
        std::memory_order_release);
    runtime.gameplay_feedback_draw_scope =
        GameplayFeedbackDrawScope::none;
    runtime.effect_root_active = false;
    runtime.effect_root_owner = GameplayEffectOwner::none;
    runtime.effect_packets.clear();
    runtime.gameplay_render_thread = 0;
    runtime.test_mode_native_active = false;
}

[[nodiscard]] int CallTaskDispatchOriginal(
    void* context,
    const std::uintptr_t task_node) noexcept
{
    return context && g_gameplay_originals.task_dispatch
        ? g_gameplay_originals.task_dispatch(reinterpret_cast<void*>(task_node)) : 0;

}

int __fastcall TaskDispatchDetour(
    void* const task_node,
    void*) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr)
    {
        return 0;
    }
    if (!RuntimeCallbacksAreActive(*runtime))
    {
        return CallTaskDispatchOriginal(
            runtime,
            reinterpret_cast<std::uintptr_t>(task_node));
    }
    std::uintptr_t task{}, vtable{};
    if (!ReadRuntimePointer(nullptr, reinterpret_cast<std::uintptr_t>(task_node), task) ||
        !ReadRuntimePointer(nullptr, task, vtable)) vtable = 0;
    if (!RequestRuntimeSpace(runtime, runtime->classifier.ClassifyTask(vtable)))
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::task_dispatch});
    return CallTaskDispatchOriginal(runtime, reinterpret_cast<std::uintptr_t>(task_node));
}

void RequestGameplayPass(const GameplayPass pass) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    if (!RequestRuntimeSpace(runtime, PassClassifier::ClassifyGameplay(pass)))
        PublishRenderRuntimeFatal(*runtime, {
            .stage = WindowedWidescreenOperationStage::render_transition});
}

void GameplayStageBackgroundMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime != nullptr && RuntimeCallbacksAreActive(*runtime))
    {
        runtime->gameplay_render_thread = GetCurrentThreadId();
        runtime->gameplay_frame_active.store(
            true,
            std::memory_order_release);
    }
    RequestGameplayPass(GameplayPass::stage_background);
}

void GameplayTrackMid(safetyhook::Context& context) noexcept
{
    RequestGameplayPass(GameplayPass::perspective_track);
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    if (context.ecx == 0 || runtime->active_gameplay_tune != 0 ||
        runtime->gameplay_feedback_draw_scope !=
        GameplayFeedbackDrawScope::none)
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    runtime->active_gameplay_tune =
        static_cast<std::uintptr_t>(context.ecx);
}

template <std::size_t SlotCount>
[[nodiscard]] bool TryMatchEffectSlots(
    const WidescreenNativeLayout& layout, const std::uintptr_t tune,
    const std::uintptr_t effect,
    const std::array<std::uint32_t, SlotCount>& slots,
    bool& matches) noexcept
{
    matches = false;
    const auto collection_extent =
        layout.tune_effect_collection_offset + layout.pointer_collection_end_offset;
    if (tune == 0 || effect == 0 ||
        tune > std::numeric_limits<std::uintptr_t>::max() -
        collection_extent)
    {
        return false;
    }

    const auto collection = tune + layout.tune_effect_collection_offset;
    std::uintptr_t begin{};
    std::uintptr_t end{};
    if (!ReadRuntimePointer(
            nullptr,
            collection + layout.pointer_collection_begin_offset,
            begin) ||
        !ReadRuntimePointer(
            nullptr,
            collection + layout.pointer_collection_end_offset,
            end) ||
        begin == 0 || end < begin ||
        (end - begin) % sizeof(std::uintptr_t) != 0)
    {
        return false;
    }

    const auto count =
        (end - begin) / sizeof(std::uintptr_t);
    if (slots.empty() || count <= slots.back())
    {
        return false;
    }

    for (const auto slot : slots)
    {
        const auto byte_offset =
            static_cast<std::size_t>(slot) * sizeof(std::uintptr_t);
        if (begin >
            std::numeric_limits<std::uintptr_t>::max() - byte_offset)
        {
            return false;
        }
        std::uintptr_t candidate{};
        if (!ReadRuntimePointer(
            nullptr,
            begin + byte_offset,
            candidate))
        {
            return false;
        }
        matches = matches || candidate == effect;
    }
    return true;
}

void GameplayEffectsMid(safetyhook::Context& context) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    if (context.ecx == 0 || runtime->active_gameplay_tune == 0 ||
        runtime->active_gameplay_tune !=
        static_cast<std::uintptr_t>(context.ecx) ||
        runtime->gameplay_feedback_draw_scope !=
        GameplayFeedbackDrawScope::none)
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    RequestGameplayPass(GameplayPass::orthographic_effects);
}


namespace {
[[noreturn]] void PlacementFailure(WindowedWidescreenRuntime& runtime) noexcept
{
    PublishRenderRuntimeFatal(runtime, {
        .stage = WindowedWidescreenOperationStage::gameplay_hud_placement});
}

WindowedWidescreenRuntime* GameplayRuntime() noexcept
{
    auto* runtime = g_callback_runtime.load(std::memory_order_acquire);
    return runtime && RuntimeCallbacksAreActive(*runtime) && runtime->active_gameplay_tune &&
        runtime->gameplay_render_thread == GetCurrentThreadId() ? runtime : nullptr;
}

void BeginSelectedDraw(WindowedWidescreenRuntime& runtime,
    GameplayHudPlacement placement, GameplayFeedbackDrawScope owner) noexcept
{
    if (runtime.gameplay_feedback_draw_scope != GameplayFeedbackDrawScope::none)
        PlacementFailure(runtime);
    const auto result = runtime.compositor.BeginGameplayHudDraw(placement);
    if (!result) {
        runtime.last_compositor_error = result.error();
        PlacementFailure(runtime);
    }
    runtime.gameplay_feedback_draw_scope = owner;
}

void EndSelectedDraw(WindowedWidescreenRuntime& runtime,
    GameplayFeedbackDrawScope owner) noexcept
{
    if (runtime.gameplay_feedback_draw_scope != owner) PlacementFailure(runtime);
    const auto result = runtime.compositor.EndGameplayHudDraw();
    if (!result) {
        runtime.last_compositor_error = result.error();
        PlacementFailure(runtime);
    }
    runtime.gameplay_feedback_draw_scope = GameplayFeedbackDrawScope::none;
}

GameplayEffectOwner TakePacketOwner(WindowedWidescreenRuntime& runtime,
    std::uintptr_t address) noexcept
{
    auto& packets = runtime.effect_packets;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (packets[i].address != address) continue;
        const auto owner = packets[i].owner;
        packets[i] = packets.back();
        packets.pop_back();
        return owner;
    }
    return GameplayEffectOwner::none;
}
}

void GameplayEffectsEndMid(safetyhook::Context&) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    if (runtime->gameplay_feedback_draw_scope != GameplayFeedbackDrawScope::none ||
        runtime->compositor.gameplay_hud_draw_active() ||
        runtime->compositor.physical_gameplay_hud_overlay_active() ||
        runtime->effect_root_active || !runtime->effect_packets.empty())
        PlacementFailure(*runtime);
    runtime->active_gameplay_tune = 0;
}

void GameplayFeedbackDrawBeginMid(safetyhook::Context& context) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    if (runtime->effect_root_active) PlacementFailure(*runtime);
    bool judgement_text{}, tutorial{};
    const auto root = static_cast<std::uintptr_t>(context.ecx);
    if (!TryMatchEffectSlots(runtime->abi.layout, runtime->active_gameplay_tune,
            root, kPlayerOneJudgementTextSlots, judgement_text) ||
        !TryMatchEffectSlots(runtime->abi.layout, runtime->active_gameplay_tune,
            root, kNoteTutorialSlots, tutorial))
        PlacementFailure(*runtime);
    runtime->effect_root_active = true;
    // Track-position grade effects (slots 93..97) retain the native 3D viewport.
    runtime->effect_root_owner = judgement_text ? GameplayEffectOwner::judgement_text :
        tutorial ? GameplayEffectOwner::tutorial : GameplayEffectOwner::none;
    if (runtime->effect_root_owner != GameplayEffectOwner::none) {
        std::uintptr_t manager{};
        const auto offset = runtime->abi.layout.effect_root_manager_offset;
        if (root > std::numeric_limits<std::uintptr_t>::max() - offset ||
            !ReadRuntimePointer(nullptr, root + offset, manager))
            PlacementFailure(*runtime);
        // Managed roots queue their children; placement is applied at submission.
        if (!manager)
            BeginSelectedDraw(*runtime, GameplayHudPlacement::right,
                GameplayFeedbackDrawScope::direct_effect);
    }
}

void GameplayFeedbackDrawEndMid(safetyhook::Context&) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    if (!runtime->effect_root_active) PlacementFailure(*runtime);
    if (runtime->gameplay_feedback_draw_scope == GameplayFeedbackDrawScope::direct_effect)
        EndSelectedDraw(*runtime, GameplayFeedbackDrawScope::direct_effect);
    runtime->effect_root_active = false;
    runtime->effect_root_owner = GameplayEffectOwner::none;
}

void EffectPacketAllocatedMid(safetyhook::Context& context) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    if (context.eax) {
        // Every allocation invalidates an old address, including unselected roots.
        (void)TakePacketOwner(*runtime, context.eax);
        if (runtime->effect_root_active &&
            runtime->effect_root_owner != GameplayEffectOwner::none) {
            try {
                runtime->effect_packets.push_back({context.eax, runtime->effect_root_owner});
            } catch (...) {
                PlacementFailure(*runtime);
            }
        }
    }
}

void EffectPacketSubmitMid(safetyhook::Context& context) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    if (TakePacketOwner(*runtime, context.ecx) != GameplayEffectOwner::none)
        BeginSelectedDraw(*runtime, GameplayHudPlacement::right,
            GameplayFeedbackDrawScope::effect_packet);
}

void EffectPacketEndMid(safetyhook::Context&) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    if (runtime->gameplay_feedback_draw_scope == GameplayFeedbackDrawScope::effect_packet)
        EndSelectedDraw(*runtime, GameplayFeedbackDrawScope::effect_packet);
}

void BarDrawBeginMid(safetyhook::Context&) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    BeginSelectedDraw(*runtime, runtime->settings.gameplay_hud_placement(),
        GameplayFeedbackDrawScope::bar);
}

void BarDrawEndMid(safetyhook::Context&) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    // Some post-call entries are also branch targets that bypass the draw.
    if (runtime->gameplay_feedback_draw_scope == GameplayFeedbackDrawScope::none) return;
    EndSelectedDraw(*runtime, GameplayFeedbackDrawScope::bar);
}

void TestModeNativeBeginMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }

    // LoopLastTask calls IDirect3DDevice9::BeginScene directly, so the
    // game's normal frame wrapper never opens a compositor frame for
    // this standalone 2D traversal. Its direct EndScene is likewise
    // unconditional, making these two guarded sites the frame owner.
    if (runtime->test_mode_native_active)
    {
        return;
    }
    if (!BeginCompositorFrame(runtime))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                render_transition,
            });
    }

    if (!RequestRuntimeSpace(runtime, RenderSpace::native_2d))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                render_transition,
            });
    }
    runtime->test_mode_native_active = true;
}

void TestModeNativeEndMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    if (!runtime->test_mode_native_active)
    {
        return;
    }

    runtime->test_mode_native_active = false;
    if (!EndCompositorFrame(runtime))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                render_transition,
            });
    }
}

[[nodiscard]] bool TryApplyNativeHudOrthographicArguments(
    const std::uint32_t stack_pointer) noexcept
{
    __try
    {
        auto* const arguments =
            reinterpret_cast<HudOrthographicArguments*>(
                static_cast<std::uintptr_t>(stack_pointer));
        ApplyNativeHudOrthographicArguments(*arguments);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void GameplayHudProjectionMid(safetyhook::Context& context) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    if (!TryApplyNativeHudOrthographicArguments(context.esp))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage =
                WindowedWidescreenOperationStage::projection,
            });
    }
}

[[nodiscard]] bool TryReadComboEntry(
    const WidescreenNativeLayout& layout, const std::uint32_t frame_pointer,
    std::int32_t& entry) noexcept
{
    if (frame_pointer < layout.combo_entry_frame_offset)
    {
        return false;
    }
    __try
    {
        entry = *reinterpret_cast<const std::int32_t*>(
            static_cast<std::uintptr_t>(frame_pointer) -
            layout.combo_entry_frame_offset);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


namespace {
void BeginCounterDraw(safetyhook::Context& context) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    std::int32_t entry{};
    if (!TryReadComboEntry(runtime->abi.layout, context.ebp, entry)) PlacementFailure(*runtime);
    BeginSelectedDraw(*runtime, ResolveComboHudPlacement(entry), GameplayFeedbackDrawScope::counter);
}
}

void ChainLabelBeginMid(safetyhook::Context& context) noexcept
{
    BeginCounterDraw(context);
}
void ChainDigitsBeginMid(safetyhook::Context& context) noexcept
{
    BeginCounterDraw(context);
}
void ChainGlowBeginMid(safetyhook::Context& context) noexcept
{
    BeginCounterDraw(context);
}
void HundredDigitsBeginMid(safetyhook::Context& context) noexcept
{
    BeginCounterDraw(context);
}
void CounterDrawEndMid(safetyhook::Context&) noexcept
{
    auto* runtime = GameplayRuntime();
    if (!runtime) return;
    if (runtime->gameplay_feedback_draw_scope == GameplayFeedbackDrawScope::none) return;
    EndSelectedDraw(*runtime, GameplayFeedbackDrawScope::counter);
}
void ClipGateMid(safetyhook::Context& context) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    auto instruction_pointer = context.eip;
    const auto result = ApplyClipGateHook(
        runtime->abi.clip_continuation,
        instruction_pointer);
    if (!result)
    {
        PublishRenderRuntimeFatal(*runtime, result.error());
    }
    context.eip = instruction_pointer;
}


}
