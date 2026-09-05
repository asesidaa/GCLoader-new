#include "Input/Switch/SwitchInputPatch.h"
#include "Diagnostics/FatalProcess.h"
#include "plog/Log.h"
#include <Windows.h>
#include <atomic>
#include <cstring>

namespace gc::switch_input {
namespace detail { QueryOriginals g_query_originals; }
namespace {
std::atomic<input::GameplayInputStyle> g_active_state{input::GameplayInputStyle::Arcade};
std::atomic_uint64_t g_virtual_button_edges{};
std::atomic_uint64_t g_virtual_button_holds{};
std::atomic_uint64_t g_cardinal_diagonal_matches{};

struct OriginalQueryContext
{
    GameplayQueryFn original;
    void* self;
    int input_device_id;
    int gameplay_frame;
};

std::uint8_t query_original(
    void* opaque_context,
    LogicalInputId logical_input) noexcept
{
    auto* context = static_cast<OriginalQueryContext*>(opaque_context);
    if (context == nullptr ||
        context->original == nullptr)
    {
        return 0;
    }

    try
    {
        return context->original(
            context->self,
            context->input_device_id,
            logical_input,
            context->gameplay_frame);
    }
    catch (...)
    {
        return 0;
    }
}

void record_first_acceptance(
    std::atomic_uint64_t& counter,
    const char* behavior,
    LogicalInputId requested_input,
    LogicalInputId accepted_direction) noexcept
{
    const auto count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count != 1)
    {
        return;
    }

    try
    {
        PLOG_INFO << "SwitchInputPatch: first " << behavior
            << " requested_input=" << requested_input
            << " accepted_direction=" << accepted_direction
            << " count=" << count;
    }
    catch (...)
    {
    }
}

std::uint8_t query_gameplay_with_aliases(
    GameplayQueryFn original,
    std::atomic_uint64_t& counter,
    const char* behavior,
    void* self,
    int input_device_id,
    LogicalInputId requested_input,
    int gameplay_frame) noexcept
{
    OriginalQueryContext context{
        original,
        self,
        input_device_id,
        gameplay_frame,
    };

    if (g_active_state.load(std::memory_order_acquire) !=
        input::GameplayInputStyle::Switch)
    {
        return query_original(&context, requested_input);
    }

    const auto result = QueryButtonWithDirectionAliases(
        requested_input,
        &context,
        query_original);
    if (result.accepted_direction != kNoDirectionAlias)
    {
        record_first_acceptance(
            counter,
            behavior,
            requested_input,
            result.accepted_direction);
    }
    return result.value;
}

std::uintptr_t StackAddress(void* frame, std::ptrdiff_t offset) noexcept {
    return reinterpret_cast<std::uintptr_t>(frame) + static_cast<std::uintptr_t>(offset);
}
template <class Value>
bool ReadStack(void* frame, std::ptrdiff_t offset, Value& value) noexcept {
    if (!frame) return false;
    __try {
        std::memcpy(&value, reinterpret_cast<const void*>(StackAddress(frame, offset)), sizeof(value));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteMatch(void* frame, std::ptrdiff_t offset) noexcept {
    if (!frame) return false;
    __try {
        const std::uint8_t matched = 1;
        std::memcpy(reinterpret_cast<void*>(StackAddress(frame, offset)), &matched, sizeof(matched));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool TryApplyDiagonalMatch(void* frame, const DiagonalStackLayout& layout) noexcept {
    std::uint8_t native_match{};
    if (!ReadStack(frame, layout.native_match_offset, native_match) || native_match != 0) return false;
    LogicalInputId target_direction{}, current_direction{};
    if (!ReadStack(frame, layout.target_direction_offset, target_direction) ||
        !ReadStack(frame, layout.current_direction_offset, current_direction) ||
        !IsSwitchDiagonalComponent(target_direction, current_direction)) return false;
    return WriteMatch(frame, layout.native_match_offset);
}
}
std::uint8_t __fastcall SwitchPressedEdgeDetour(
    void* self,
    void*,
    int input_device_id,
    int logical_input,
    int gameplay_frame) noexcept
{
    return query_gameplay_with_aliases(
        detail::g_query_originals.pressed,
        g_virtual_button_edges,
        "virtual_button_edge",
        self,
        input_device_id,
        logical_input,
        gameplay_frame);
}

std::uint8_t __fastcall SwitchHeldStateDetour(
    void* self,
    void*,
    int input_device_id,
    int logical_input,
    int gameplay_frame) noexcept
{
    return query_gameplay_with_aliases(
        detail::g_query_originals.held,
        g_virtual_button_holds,
        "virtual_button_hold",
        self,
        input_device_id,
        logical_input,
        gameplay_frame);
}

void ApplySwitchDiagonalMatch(safetyhook::Context& context, const DiagonalStackLayout& layout) noexcept {
    if (g_active_state.load(std::memory_order_acquire) != input::GameplayInputStyle::Switch) return;
    try {
        if (TryApplyDiagonalMatch(reinterpret_cast<void*>(context.ebp), layout))
            record_first_acceptance(g_cardinal_diagonal_matches, "cardinal_diagonal_match",
                kNoDirectionAlias, kNoDirectionAlias);
    } catch (...) {}
}
void ActivateSwitchInput(const game_version::ApprovedVersionedPlan& plan) noexcept {
    bool enabled{};
    for (const auto& site : plan.sites())
        if (site.contract().feature == game_version::FeatureId::switch_input) enabled = true;
    if (!enabled) return;
    if (!detail::g_query_originals.pressed || !detail::g_query_originals.held)
        diagnostics::AbortProcess({});
    g_active_state.store(input::GameplayInputStyle::Switch, std::memory_order_release);
    try {
        PLOG_INFO << "SwitchInputPatch: all hooks active";
        PLOG_INFO << "SwitchInputPatch: requested_style=Switch active_style=Switch";
    } catch (...) {}
}
}
