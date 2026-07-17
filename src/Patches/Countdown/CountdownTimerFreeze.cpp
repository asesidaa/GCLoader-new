#include "Patches/Countdown/CountdownTimerFreeze.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>

#include "plog/Log.h"

namespace gc::timer_freeze {
namespace {

std::atomic_bool g_countdown_timer_freeze_enabled{false};
std::atomic_bool g_countdown_timer_initialized{false};
std::atomic_bool g_countdown_timer_patches_applied{false};

std::uintptr_t exe_base() {
    static const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

void* rva_ptr(std::uintptr_t rva) {
    return reinterpret_cast<void*>(exe_base() + rva);
}

bool make_writable(void* address, std::size_t size, DWORD& old_protect) {
    return VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect) != FALSE;
}

bool write_bytes(void* address, const void* data, std::size_t size) {
    DWORD old_protect = 0;
    if (!make_writable(address, size, old_protect)) {
        PLOG_ERROR << "GC_TIMER_FREEZE: VirtualProtect failed at " << address
                   << ", gle=" << GetLastError();
        return false;
    }

    std::memcpy(address, data, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, old_protect, &ignored);
    return true;
}

bool bytes_match(const void* address, const std::uint8_t* expected, std::size_t size) {
    __try {
        return std::memcmp(address, expected, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void expected_call_bytes(const CountdownDeltaPatchSite& site, std::uint8_t (&bytes)[kCountdownDeltaCallPatchSize]) {
    bytes[0] = 0xE8;
    const auto call_next = exe_base() + site.call_rva + kCountdownDeltaCallPatchSize;
    const auto target = exe_base() + kRvaGlobalFrameDeltaSeconds;
    const auto rel = static_cast<std::int32_t>(target - call_next);
    std::memcpy(bytes + 1, &rel, sizeof(rel));
}

bool patch_site(const CountdownDeltaPatchSite& site, bool enable) {
    auto* address = rva_ptr(site.call_rva);
    std::uint8_t original[kCountdownDeltaCallPatchSize]{};
    expected_call_bytes(site, original);
    const auto* replacement = enable ? kCountdownDeltaFrozenCallBytes : original;
    const auto* acceptable = enable ? original : kCountdownDeltaFrozenCallBytes;

    if (bytes_match(address, replacement, kCountdownDeltaCallPatchSize)) {
        return true;
    }
    if (!bytes_match(address, acceptable, kCountdownDeltaCallPatchSize)) {
        PLOG_ERROR << "GC_TIMER_FREEZE: countdown callsite byte mismatch at RVA 0x"
                   << std::hex << site.call_rva << std::dec
                   << " while " << (enable ? "enabling" : "disabling");
        return false;
    }

    if (!write_bytes(address, replacement, kCountdownDeltaCallPatchSize)) {
        PLOG_ERROR << "GC_TIMER_FREEZE: failed to patch countdown callsite at RVA 0x"
                   << std::hex << site.call_rva << std::dec;
        return false;
    }

    return true;
}

bool apply_countdown_timer_freeze_patches(bool enable) {
    std::size_t patched = 0;
    bool ok = true;

    for (const auto& site : kCountdownDeltaPatchSites) {
        if (patch_site(site, enable)) {
            ++patched;
        } else {
            ok = false;
        }
    }

    if (ok) {
        g_countdown_timer_patches_applied.store(enable, std::memory_order_relaxed);
    }

    PLOG_INFO << "GC_TIMER_FREEZE: " << (enable ? "enabled" : "disabled")
              << " local countdown delta call patches " << patched << "/"
              << (sizeof(kCountdownDeltaPatchSites) / sizeof(kCountdownDeltaPatchSites[0]));
    return ok;
}

} // namespace

void SetCountdownTimerFreezeEnabled(bool enabled) noexcept {
    g_countdown_timer_freeze_enabled.store(enabled, std::memory_order_relaxed);
    if (g_countdown_timer_initialized.load(std::memory_order_acquire)
        && g_countdown_timer_patches_applied.load(std::memory_order_relaxed) != enabled) {
        apply_countdown_timer_freeze_patches(enabled);
    }
}

bool IsCountdownTimerFreezeEnabled() noexcept {
    return g_countdown_timer_freeze_enabled.load(std::memory_order_relaxed);
}

void CountdownTimerFreezeInit() {
    bool expected = false;
    if (!g_countdown_timer_initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    PLOG_INFO << "GC_TIMER_FREEZE: initializing local countdown delta call patches, freeze_enabled="
              << IsCountdownTimerFreezeEnabled();

    if (IsCountdownTimerFreezeEnabled()) {
        apply_countdown_timer_freeze_patches(true);
    }
}

} // namespace gc::timer_freeze
