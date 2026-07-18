#include "Input/XioOnly/XioOnlyInputPatch.h"

#include <Windows.h>
#include <plog/Log.h>
#include <safetyhook.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace gc::xio_only_input {
namespace {

struct PcInputHooks {
    safetyhook::InlineHook pressed{};
    safetyhook::InlineHook held{};
    safetyhook::InlineHook released{};
    safetyhook::InlineHook repeat{};
};

PcInputHooks g_hooks;
std::atomic_bool g_active{false};

int __cdecl BlockNativePcInput(int) noexcept {
    return 0;
}

[[nodiscard]] const char* SiteName(PcInputQuerySite site) noexcept {
    switch (site) {
    case PcInputQuerySite::None:
        return "none";
    case PcInputQuerySite::Pressed:
        return "pressed";
    case PcInputQuerySite::Held:
        return "held";
    case PcInputQuerySite::Released:
        return "released";
    case PcInputQuerySite::Repeat:
        return "repeat";
    }
    return "unknown";
}

[[nodiscard]] std::uintptr_t SiteRva(PcInputQuerySite site) noexcept {
    switch (site) {
    case PcInputQuerySite::Pressed:
        return kPressedQueryRva;
    case PcInputQuerySite::Held:
        return kHeldQueryRva;
    case PcInputQuerySite::Released:
        return kReleasedQueryRva;
    case PcInputQuerySite::Repeat:
        return kRepeatQueryRva;
    case PcInputQuerySite::None:
        return 0;
    }
    return 0;
}

[[nodiscard]] bool ReadSignature(
    std::uintptr_t address,
    std::array<std::uint8_t, kPcInputQuerySignature.size()>& bytes) noexcept {
    __try {
        std::memcpy(
            bytes.data(),
            reinterpret_cast<const void*>(address),
            bytes.size());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ResetHooks() noexcept {
    g_hooks.repeat.reset();
    g_hooks.released.reset();
    g_hooks.held.reset();
    g_hooks.pressed.reset();
    g_active.store(false, std::memory_order_release);
}

[[nodiscard]] bool PreflightSignatures(std::uintptr_t base) noexcept {
    std::array<std::uint8_t, kPcInputQuerySignature.size()> pressed{};
    std::array<std::uint8_t, kPcInputQuerySignature.size()> held{};
    std::array<std::uint8_t, kPcInputQuerySignature.size()> released{};
    std::array<std::uint8_t, kPcInputQuerySignature.size()> repeat{};

    if (!ReadSignature(base + kPressedQueryRva, pressed) ||
        !ReadSignature(base + kHeldQueryRva, held) ||
        !ReadSignature(base + kReleasedQueryRva, released) ||
        !ReadSignature(base + kRepeatQueryRva, repeat)) {
        PLOG_ERROR << "XioOnlyInputPatch: signature read failed";
        return false;
    }

    PcInputQuerySite mismatch = PcInputQuerySite::None;
    if (!ValidatePcInputQuerySignatures(
            {pressed, held, released, repeat}, &mismatch)) {
        PLOG_ERROR << "XioOnlyInputPatch: signature mismatch site="
                   << SiteName(mismatch)
                   << " rva=0x" << std::hex << SiteRva(mismatch)
                   << std::dec;
        return false;
    }
    return true;
}

[[nodiscard]] bool CreateHook(
    safetyhook::InlineHook& hook,
    std::uintptr_t address) {
    hook = safetyhook::create_inline(
        reinterpret_cast<void*>(address),
        reinterpret_cast<void*>(&BlockNativePcInput));
    return static_cast<bool>(hook);
}

[[nodiscard]] bool InstallHooks(std::uintptr_t base) noexcept {
    PcInputQuerySite site = PcInputQuerySite::Pressed;
    try {
        PcInputHookSet created{};
        created.pressed = CreateHook(
            g_hooks.pressed, base + kPressedQueryRva);
        if (!created.pressed) {
            throw site;
        }

        site = PcInputQuerySite::Held;
        created.held = CreateHook(g_hooks.held, base + kHeldQueryRva);
        if (!created.held) {
            throw site;
        }

        site = PcInputQuerySite::Released;
        created.released = CreateHook(
            g_hooks.released, base + kReleasedQueryRva);
        if (!created.released) {
            throw site;
        }

        site = PcInputQuerySite::Repeat;
        created.repeat = CreateHook(
            g_hooks.repeat, base + kRepeatQueryRva);
        if (!created.repeat || !CompletePcInputHookSet(created)) {
            throw site;
        }
    } catch (PcInputQuerySite failed_site) {
        PLOG_ERROR << "XioOnlyInputPatch: hook creation failed site="
                   << SiteName(failed_site)
                   << " rva=0x" << std::hex << SiteRva(failed_site)
                   << std::dec;
        ResetHooks();
        return false;
    } catch (...) {
        PLOG_ERROR << "XioOnlyInputPatch: hook creation threw site="
                   << SiteName(site)
                   << " rva=0x" << std::hex << SiteRva(site)
                   << std::dec;
        ResetHooks();
        return false;
    }

    g_active.store(true, std::memory_order_release);
    PLOG_INFO << "XioOnlyInputPatch: native PC queries blocked"
              << " pressed_rva=0x" << std::hex << kPressedQueryRva
              << " held_rva=0x" << kHeldQueryRva
              << " released_rva=0x" << kReleasedQueryRva
              << " repeat_rva=0x" << kRepeatQueryRva
              << std::dec;
    return true;
}

} // namespace

bool XioOnlyInputPatchInit() {
    static std::atomic_bool initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return g_active.load(std::memory_order_acquire);
    }

    const auto base = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    if (base == 0) {
        PLOG_ERROR << "XioOnlyInputPatch: main executable unavailable";
        return false;
    }
    if (!PreflightSignatures(base)) {
        return false;
    }
    return InstallHooks(base);
}

} // namespace gc::xio_only_input
