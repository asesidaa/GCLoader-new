#pragma once
#include "Input/Switch/SwitchInputProfile.h"
namespace gc::switch_input {
namespace detail {
struct QueryOriginals final { GameplayQueryFn pressed{}; GameplayQueryFn held{}; };
extern QueryOriginals g_query_originals;
}
// Called only after the common executor completes every approved operation.
void ActivateSwitchInput(const game_version::ApprovedVersionedPlan&) noexcept;
std::uint8_t __fastcall SwitchPressedEdgeDetour(
    void*, void*, int input_device_id, LogicalInputId logical_input, int gameplay_frame) noexcept;
std::uint8_t __fastcall SwitchHeldStateDetour(
    void*, void*, int input_device_id, LogicalInputId logical_input, int gameplay_frame) noexcept;
void ApplySwitchDiagonalMatch(safetyhook::Context&, const DiagonalStackLayout&) noexcept;
}
