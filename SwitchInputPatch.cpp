#include "SwitchInputPatch.h"

#include "config.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>

#include <safetyhook.hpp>
#include "plog/Log.h"

namespace gc::switch_input {
namespace {

safetyhook::InlineHook g_pressed_edge_hook{};
safetyhook::InlineHook g_held_state_hook{};
safetyhook::MidHook g_diagonal_match_hook{};

std::atomic<SwitchPatchState> g_active_state{SwitchPatchState::Arcade};
std::atomic_uint64_t g_virtual_button_edges{0};
std::atomic_uint64_t g_virtual_button_holds{0};
std::atomic_uint64_t g_cardinal_diagonal_matches{0};

std::uintptr_t executable_base() noexcept {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

void* rva_pointer(std::uintptr_t base, std::uintptr_t rva) noexcept {
    return reinterpret_cast<void*>(base + rva);
}

const char* requested_style_name(GameplayInputStyle style) noexcept {
    switch (style) {
    case GameplayInputStyle::Arcade:
        return "Arcade";
    case GameplayInputStyle::Switch:
        return "Switch";
    }
    return "Unknown";
}

const char* active_style_name(SwitchPatchState state) noexcept {
    return state == SwitchPatchState::Switch ? "Switch" : "Arcade";
}

void log_install_failure(
    SwitchHookSite site,
    const char* stage) noexcept {
    try {
        PLOG_ERROR << "SwitchInputPatch: install failure stage=" << stage
                   << " site=" << HookSiteName(site)
                   << " rva=0x" << std::hex << RvaForHookSite(site)
                   << std::dec;
    } catch (...) {
    }
}

void log_requested_and_active(GameplayInputStyle requested) noexcept {
    try {
        PLOG_INFO << "SwitchInputPatch: requested_style="
                  << requested_style_name(requested)
                  << " active_style="
                  << active_style_name(
                         g_active_state.load(std::memory_order_acquire));
    } catch (...) {
    }
}

bool read_bytes_safe(
    std::uintptr_t address,
    void* output,
    std::size_t size) noexcept {
    if (address == 0 || output == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool preflight_signatures(std::uintptr_t base) noexcept {
    std::array<std::uint8_t, kGameplayQueryEntrySignature.size()> pressed{};
    std::array<std::uint8_t, kGameplayQueryEntrySignature.size()> held{};
    std::array<std::uint8_t, kDiagonalMatchSignature.size()> diagonal{};

    if (!read_bytes_safe(
            base + kGameplayPressedQueryRva,
            pressed.data(),
            pressed.size())) {
        log_install_failure(SwitchHookSite::PressedEdge, "read_signature");
        return false;
    }
    if (!read_bytes_safe(
            base + kGameplayHeldQueryRva,
            held.data(),
            held.size())) {
        log_install_failure(SwitchHookSite::HeldState, "read_signature");
        return false;
    }
    if (!read_bytes_safe(
            base + kDiagonalMatchRva,
            diagonal.data(),
            diagonal.size())) {
        log_install_failure(SwitchHookSite::DiagonalMatch, "read_signature");
        return false;
    }

    SwitchHookSite mismatch = SwitchHookSite::None;
    if (!ValidateSwitchInputSignatures(
            {pressed, held, diagonal},
            &mismatch)) {
        log_install_failure(mismatch, "validate_signature");
        return false;
    }

    try {
        PLOG_INFO << "SwitchInputPatch: signature preflight passed"
                  << " pressed_rva=0x" << std::hex
                  << kGameplayPressedQueryRva
                  << " held_rva=0x" << kGameplayHeldQueryRva
                  << " diagonal_rva=0x" << kDiagonalMatchRva
                  << std::dec;
    } catch (...) {
    }
    return true;
}

void reset_hooks() noexcept {
    g_active_state.store(SwitchPatchState::Arcade, std::memory_order_release);
    try {
        g_diagonal_match_hook.reset();
    } catch (...) {
    }
    try {
        g_held_state_hook.reset();
    } catch (...) {
    }
    try {
        g_pressed_edge_hook.reset();
    } catch (...) {
    }
}

struct OriginalQueryContext {
    safetyhook::InlineHook* hook;
    void* self;
    int input_device_id;
    int gameplay_frame;
};

std::uint8_t query_original(
    void* opaque_context,
    LogicalInputId logical_input) noexcept {
    auto* context = static_cast<OriginalQueryContext*>(opaque_context);
    if (context == nullptr ||
        context->hook == nullptr ||
        !*context->hook) {
        return 0;
    }

    try {
        return context->hook->unsafe_thiscall<std::uint8_t>(
            context->self,
            context->input_device_id,
            logical_input,
            context->gameplay_frame);
    } catch (...) {
        return 0;
    }
}

void record_first_acceptance(
    std::atomic_uint64_t& counter,
    const char* behavior,
    LogicalInputId requested_input,
    LogicalInputId accepted_direction) noexcept {
    const auto count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count != 1) {
        return;
    }

    try {
        PLOG_INFO << "SwitchInputPatch: first " << behavior
                  << " requested_input=" << requested_input
                  << " accepted_direction=" << accepted_direction
                  << " count=" << count;
    } catch (...) {
    }
}

std::uint8_t query_gameplay_with_aliases(
    safetyhook::InlineHook& hook,
    std::atomic_uint64_t& counter,
    const char* behavior,
    void* self,
    int input_device_id,
    LogicalInputId requested_input,
    int gameplay_frame) noexcept {
    OriginalQueryContext context{
        &hook,
        self,
        input_device_id,
        gameplay_frame,
    };

    if (g_active_state.load(std::memory_order_acquire) !=
        SwitchPatchState::Switch) {
        return query_original(&context, requested_input);
    }

    const auto result = QueryButtonWithDirectionAliases(
        requested_input,
        &context,
        query_original);
    if (result.accepted_direction != kNoDirectionAlias) {
        record_first_acceptance(
            counter,
            behavior,
            requested_input,
            result.accepted_direction);
    }
    return result.value;
}

std::uint8_t __fastcall hook_pressed_edge(
    void* self,
    void*,
    int input_device_id,
    int logical_input,
    int gameplay_frame) noexcept {
    return query_gameplay_with_aliases(
        g_pressed_edge_hook,
        g_virtual_button_edges,
        "virtual_button_edge",
        self,
        input_device_id,
        logical_input,
        gameplay_frame);
}

std::uint8_t __fastcall hook_held_state(
    void* self,
    void*,
    int input_device_id,
    int logical_input,
    int gameplay_frame) noexcept {
    return query_gameplay_with_aliases(
        g_held_state_hook,
        g_virtual_button_holds,
        "virtual_button_hold",
        self,
        input_device_id,
        logical_input,
        gameplay_frame);
}

std::uintptr_t stack_address(
    void* frame_pointer,
    std::ptrdiff_t offset) noexcept {
    return reinterpret_cast<std::uintptr_t>(frame_pointer) +
           static_cast<std::uintptr_t>(offset);
}

bool guarded_stack_read(
    void* context,
    std::ptrdiff_t offset,
    void* output,
    std::size_t size) noexcept {
    if (context == nullptr || output == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(
            output,
            reinterpret_cast<const void*>(stack_address(context, offset)),
            size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guarded_stack_write(
    void* context,
    std::ptrdiff_t offset,
    const void* input,
    std::size_t size) noexcept {
    if (context == nullptr || input == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(
            reinterpret_cast<void*>(stack_address(context, offset)),
            input,
            size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void hook_diagonal_match(safetyhook::Context& context) noexcept {
    if (g_active_state.load(std::memory_order_acquire) !=
        SwitchPatchState::Switch) {
        return;
    }

    try {
        const StackAccessor stack{
            reinterpret_cast<void*>(context.ebp),
            guarded_stack_read,
            guarded_stack_write,
        };
        if (TryApplySwitchDiagonalMatch(stack)) {
            record_first_acceptance(
                g_cardinal_diagonal_matches,
                "cardinal_diagonal_match",
                kNoDirectionAlias,
                kNoDirectionAlias);
        }
    } catch (...) {
    }
}

bool install_hooks_transactionally(std::uintptr_t base) noexcept {
    HookCreationResults created{};
    SwitchHookSite current_site = SwitchHookSite::PressedEdge;
    const char* current_stage = "create_inline";

    try {
        g_pressed_edge_hook = safetyhook::create_inline(
            rva_pointer(base, kGameplayPressedQueryRva),
            reinterpret_cast<void*>(hook_pressed_edge));
        created.pressed_edge = static_cast<bool>(g_pressed_edge_hook);
        if (!created.pressed_edge) {
            log_install_failure(current_site, current_stage);
            reset_hooks();
            return false;
        }

        current_site = SwitchHookSite::HeldState;
        g_held_state_hook = safetyhook::create_inline(
            rva_pointer(base, kGameplayHeldQueryRva),
            reinterpret_cast<void*>(hook_held_state));
        created.held_state = static_cast<bool>(g_held_state_hook);
        if (!created.held_state) {
            log_install_failure(current_site, current_stage);
            reset_hooks();
            return false;
        }

        current_site = SwitchHookSite::DiagonalMatch;
        current_stage = "create_mid";
        g_diagonal_match_hook = safetyhook::create_mid(
            rva_pointer(base, kDiagonalMatchRva),
            hook_diagonal_match);
        created.diagonal_match = static_cast<bool>(g_diagonal_match_hook);
        if (!created.diagonal_match) {
            log_install_failure(current_site, current_stage);
            reset_hooks();
            return false;
        }
    } catch (...) {
        log_install_failure(current_site, current_stage);
        reset_hooks();
        return false;
    }

    const auto resolved_state = ResolveSwitchPatchState(created);
    if (resolved_state != SwitchPatchState::Switch) {
        log_install_failure(current_site, "resolve_complete_hook_set");
        reset_hooks();
        return false;
    }

    g_active_state.store(SwitchPatchState::Switch, std::memory_order_release);
    try {
        PLOG_INFO << "SwitchInputPatch: all hooks active"
                  << " pressed_rva=0x" << std::hex
                  << kGameplayPressedQueryRva
                  << " held_rva=0x" << kGameplayHeldQueryRva
                  << " diagonal_rva=0x" << kDiagonalMatchRva
                  << std::dec;
    } catch (...) {
    }
    return true;
}

} // namespace

void SwitchInputPatchInit() {
    static std::atomic_bool initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    const auto requested =
        ConfigManager::instance().GetGameplayInputStyle();
    g_active_state.store(SwitchPatchState::Arcade, std::memory_order_release);

    if (requested == GameplayInputStyle::Arcade) {
        log_requested_and_active(requested);
        return;
    }

    const auto base = executable_base();
    if (base == 0) {
        log_install_failure(SwitchHookSite::None, "resolve_main_executable");
        log_requested_and_active(requested);
        return;
    }

    if (!preflight_signatures(base)) {
        log_requested_and_active(requested);
        return;
    }

    install_hooks_transactionally(base);
    log_requested_and_active(requested);
}

} // namespace gc::switch_input
