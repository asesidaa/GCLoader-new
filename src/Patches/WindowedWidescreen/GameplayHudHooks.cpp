#include "Patches/WindowedWidescreen/GameplayHudHooks.h"
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include "Patches/WindowedWidescreen/RenderHooks.h"
#include "Patches/WindowedWidescreen/GameplayFeedbackPlacement.h"
#include <plog/Log.h>
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
    runtime.note_tutorial_group_active = false;
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
        GameplayFeedbackDrawScope::none ||
        runtime->note_tutorial_group_active)
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

[[nodiscard]] bool TryMatchPlayerOneJudgementEffect(
    const WidescreenNativeLayout& layout, const std::uintptr_t tune,
    const std::uintptr_t effect,
    bool& matches) noexcept
{
    return TryMatchEffectSlots(layout, tune, effect, kPlayerOneJudgementSlots, matches);

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
        GameplayFeedbackDrawScope::none ||
        runtime->note_tutorial_group_active)
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

void GameplayEffectsEndMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    if (runtime->gameplay_feedback_draw_scope !=
        GameplayFeedbackDrawScope::none ||
        runtime->compositor.physical_gameplay_hud_overlay_active())
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    if (runtime->note_tutorial_group_active)
    {
        runtime->note_tutorial_group_active = false;
        if (!RequestRuntimeGameplayHudPlacement(
            *runtime,
            runtime->settings.gameplay_hud_placement()))
        {
            PublishRenderRuntimeFatal(
                *runtime,
                WindowedWidescreenError{
                    .stage = WindowedWidescreenOperationStage::
                    gameplay_hud_placement,
                });
        }
    }
    runtime->active_gameplay_tune = 0;
}

void GameplayFeedbackDrawBeginMid(
    safetyhook::Context& context) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
        runtime->active_gameplay_tune == 0)
    {
        return;
    }
    if (runtime->gameplay_feedback_draw_scope !=
        GameplayFeedbackDrawScope::none)
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }

    bool is_player_one_judgement{};
    if (!TryMatchPlayerOneJudgementEffect(
        runtime->abi.layout, runtime->active_gameplay_tune,
        static_cast<std::uintptr_t>(context.ecx),
        is_player_one_judgement))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    if (!is_player_one_judgement)
    {
        return;
    }

    const auto begun =
        runtime->compositor.BeginPhysicalGameplayHudOverlay(
            GameplayHudPlacement::right);
    if (!begun)
    {
        runtime->last_compositor_error = begun.error();
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    runtime->gameplay_feedback_draw_scope =
        GameplayFeedbackDrawScope::physical_gameplay_hud_overlay;
    if (!runtime->judgement_scope_reported)
    {
        runtime->judgement_scope_reported = true;
        PLOG_INFO
            << "WindowedWidescreen diagnostic: Player 1 judgement root entered right HUD overlay";
    }
}

void GameplayFeedbackDrawEndMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
        runtime->gameplay_feedback_draw_scope ==
        GameplayFeedbackDrawScope::none)
    {
        return;
    }
    if (runtime->gameplay_feedback_draw_scope !=
        GameplayFeedbackDrawScope::physical_gameplay_hud_overlay)
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    const auto ended =
        runtime->compositor.EndPhysicalGameplayHudOverlay();
    if (!ended)
    {
        runtime->last_compositor_error = ended.error();
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    runtime->gameplay_feedback_draw_scope =
        GameplayFeedbackDrawScope::none;
}

void NoteTutorialGroupBeginMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
        runtime->active_gameplay_tune == 0)
    {
        return;
    }
    if (runtime->note_tutorial_group_active ||
        runtime->gameplay_feedback_draw_scope !=
        GameplayFeedbackDrawScope::none ||
        !RequestRuntimeGameplayHudPlacement(
            *runtime,
            GameplayHudPlacement::right))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
    runtime->note_tutorial_group_active = true;
    if (!runtime->note_tutorial_scope_reported)
    {
        runtime->note_tutorial_scope_reported = true;
        PLOG_INFO
            << "WindowedWidescreen diagnostic: note tutorial group entered right HUD viewport";
    }
}

void NoteTutorialGroupEndMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
        !runtime->note_tutorial_group_active)
    {
        return;
    }
    runtime->note_tutorial_group_active = false;
    if (!RequestRuntimeGameplayHudPlacement(
        *runtime,
        runtime->settings.gameplay_hud_placement()))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
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

void ComboBeginMid(safetyhook::Context& context) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }

    std::int32_t entry{};
    if (!TryReadComboEntry(runtime->abi.layout, context.ebp, entry) ||
        !RequestRuntimeGameplayHudPlacement(
            *runtime,
            ResolveComboHudPlacement(entry)))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
}

void ComboEndMid(safetyhook::Context&) noexcept
{
    auto* runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime))
    {
        return;
    }
    if (!RequestRuntimeGameplayHudPlacement(
        *runtime,
        runtime->settings.gameplay_hud_placement()))
    {
        PublishRenderRuntimeFatal(
            *runtime,
            WindowedWidescreenError{
                .stage = WindowedWidescreenOperationStage::
                gameplay_hud_placement,
            });
    }
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
