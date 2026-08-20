#include "Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h"

#include "Audio/AudioPatch.h"
#include "Config/config.h"
#include "Input/Polling/GameplayTransitionJournal.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"
#include "Patches/AbsoluteJudgement/JudgementScope.h"
#include "Patches/AbsoluteJudgement/NativeJudgementAbi.h"
#include "SystemPath/StartupFatal.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "plog/Log.h"

namespace gc::absolute_judgement {
namespace {

using namespace native_abi;

enum class NativeSite : std::size_t {
    StageBegin,
    StageEnd,
    LoopGuard,
    Pressed,
    Held,
    Released,
    Direction,
    HeldAge,
};

enum class InstallStage : std::size_t {
    Preflight,
    Create,
    Enable,
};

struct NativeSiteContract final {
    NativeSite site{};
    std::uintptr_t rva{};
    std::span<const std::uint8_t> expected;
};

struct InstallFailure final {
    InstallStage stage{};
    NativeSite site{};
};

struct AbsoluteJudgementHooks final {
    safetyhook::InlineHook stage_begin;
    safetyhook::InlineHook stage_end;
    safetyhook::MidHook loop_guard;
    safetyhook::InlineHook pressed;
    safetyhook::InlineHook held;
    safetyhook::InlineHook released;
    safetyhook::InlineHook direction;
    safetyhook::InlineHook held_age;
};

inline constexpr std::array<NativeSiteContract, 8> kSiteContracts{{
    {NativeSite::StageBegin, kStageBeginRva, kStageBeginPrefix},
    {NativeSite::StageEnd, kStageEndRva, kStageEndPrefix},
    {NativeSite::LoopGuard, kLoopGuardRva, kLoopGuardPrefix},
    {NativeSite::Pressed, kPressedRva, kPressedPrefix},
    {NativeSite::Held, kHeldRva, kHeldPrefix},
    {NativeSite::Released, kReleasedRva, kReleasedPrefix},
    {NativeSite::Direction, kDirectionRva, kDirectionPrefix},
    {NativeSite::HeldAge, kHeldAgeRva, kHeldAgePrefix},
}};

inline constexpr std::array<std::string_view, 8> kPreflightFailureLogs{{
    "AbsoluteJudgement: startup fatal stage=preflight site=native_stage_begin",
    "AbsoluteJudgement: startup fatal stage=preflight site=native_stage_end",
    "AbsoluteJudgement: startup fatal stage=preflight site=loop_guard",
    "AbsoluteJudgement: startup fatal stage=preflight site=pressed",
    "AbsoluteJudgement: startup fatal stage=preflight site=held",
    "AbsoluteJudgement: startup fatal stage=preflight site=released",
    "AbsoluteJudgement: startup fatal stage=preflight site=direction",
    "AbsoluteJudgement: startup fatal stage=preflight site=held_age",
}};
inline constexpr std::array<std::string_view, 8> kCreateFailureLogs{{
    "AbsoluteJudgement: startup fatal stage=create site=native_stage_begin",
    "AbsoluteJudgement: startup fatal stage=create site=native_stage_end",
    "AbsoluteJudgement: startup fatal stage=create site=loop_guard",
    "AbsoluteJudgement: startup fatal stage=create site=pressed",
    "AbsoluteJudgement: startup fatal stage=create site=held",
    "AbsoluteJudgement: startup fatal stage=create site=released",
    "AbsoluteJudgement: startup fatal stage=create site=direction",
    "AbsoluteJudgement: startup fatal stage=create site=held_age",
}};
inline constexpr std::array<std::string_view, 8> kEnableFailureLogs{{
    "AbsoluteJudgement: startup fatal stage=enable site=native_stage_begin",
    "AbsoluteJudgement: startup fatal stage=enable site=native_stage_end",
    "AbsoluteJudgement: startup fatal stage=enable site=loop_guard",
    "AbsoluteJudgement: startup fatal stage=enable site=pressed",
    "AbsoluteJudgement: startup fatal stage=enable site=held",
    "AbsoluteJudgement: startup fatal stage=enable site=released",
    "AbsoluteJudgement: startup fatal stage=enable site=direction",
    "AbsoluteJudgement: startup fatal stage=enable site=held_age",
}};

AbsoluteJudgementHooks g_hooks;
AbsoluteJudgementHooks* g_active_hooks{};
std::atomic_bool g_startup_fatal_published{false};

[[noreturn]] void PublishStartupFatal(
    const std::string_view log) noexcept {
    gc::system_path::PublishStartupFatal(
        g_startup_fatal_published,
        log,
        L"GCLoader could not install the absolute-time judgement patch. "
        L"The supported game executable and enabled-mode configuration are "
        L"required. Check loader-log.txt for the exact failed stage and site.",
        L"GCLoader absolute-time judgement setup error",
        27);
    std::abort();
}

[[noreturn]] void PublishInstallFailure(
    const InstallFailure failure) noexcept {
    const auto site = static_cast<std::size_t>(failure.site);
    switch (failure.stage) {
    case InstallStage::Preflight:
        PublishStartupFatal(kPreflightFailureLogs[site]);
    case InstallStage::Create:
        PublishStartupFatal(kCreateFailureLogs[site]);
    case InstallStage::Enable:
        PublishStartupFatal(kEnableFailureLogs[site]);
    }
    std::abort();
}

[[nodiscard]] bool AddAddress(
    const std::uintptr_t base,
    const std::uintptr_t rva,
    std::uintptr_t* const result) noexcept {
    if (result == nullptr ||
        rva > (std::numeric_limits<std::uintptr_t>::max)() - base) {
        return false;
    }
    *result = base + rva;
    return true;
}

[[nodiscard]] bool PrefixMatchesSafe(
    const std::uintptr_t address,
    const std::span<const std::uint8_t> expected) noexcept {
    if (address == 0 || expected.empty()) {
        return false;
    }
    __try {
        const auto* bytes = reinterpret_cast<volatile const std::uint8_t*>(
            address);
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (bytes[index] != expected[index]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename Hook>
[[nodiscard]] bool EnableHook(Hook& hook) {
    const auto result = hook.enable();
    return result.has_value();
}

[[nodiscard]] std::optional<InstallFailure> InstallHooks(
    const std::uintptr_t executable_base) {
    for (const auto& contract : kSiteContracts) {
        std::uintptr_t address{};
        if (!AddAddress(executable_base, contract.rva, &address) ||
            !PrefixMatchesSafe(address, contract.expected)) {
            return InstallFailure{
                .stage = InstallStage::Preflight,
                .site = contract.site,
            };
        }
    }

    // Install directly into stable process-lifetime storage. Once any hook is
    // enabled its handler must never observe a moved-from trampoline owner.
    auto& pending = g_hooks;
    auto created_stage_begin = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(executable_base + kStageBeginRva),
        reinterpret_cast<void*>(&HookStageBegin),
        safetyhook::InlineHook::StartDisabled);
    if (!created_stage_begin) {
        return InstallFailure{InstallStage::Create, NativeSite::StageBegin};
    }
    pending.stage_begin = std::move(*created_stage_begin);

    auto created_stage_end = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(executable_base + kStageEndRva),
        reinterpret_cast<void*>(&HookStageEnd),
        safetyhook::InlineHook::StartDisabled);
    if (!created_stage_end) {
        return InstallFailure{InstallStage::Create, NativeSite::StageEnd};
    }
    pending.stage_end = std::move(*created_stage_end);

    auto created_loop_guard = safetyhook::MidHook::create(
        reinterpret_cast<void*>(executable_base + kLoopGuardRva),
        &HookLoopGuard,
        safetyhook::MidHook::StartDisabled);
    if (!created_loop_guard) {
        return InstallFailure{InstallStage::Create, NativeSite::LoopGuard};
    }
    pending.loop_guard = std::move(*created_loop_guard);

    auto created_pressed = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(executable_base + kPressedRva),
        reinterpret_cast<void*>(&HookPressed),
        safetyhook::InlineHook::StartDisabled);
    if (!created_pressed) {
        return InstallFailure{InstallStage::Create, NativeSite::Pressed};
    }
    pending.pressed = std::move(*created_pressed);

    auto created_held = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(executable_base + kHeldRva),
        reinterpret_cast<void*>(&HookHeld),
        safetyhook::InlineHook::StartDisabled);
    if (!created_held) {
        return InstallFailure{InstallStage::Create, NativeSite::Held};
    }
    pending.held = std::move(*created_held);

    auto created_released = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(executable_base + kReleasedRva),
        reinterpret_cast<void*>(&HookReleased),
        safetyhook::InlineHook::StartDisabled);
    if (!created_released) {
        return InstallFailure{InstallStage::Create, NativeSite::Released};
    }
    pending.released = std::move(*created_released);

    auto created_direction = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(executable_base + kDirectionRva),
        reinterpret_cast<void*>(&HookDirection),
        safetyhook::InlineHook::StartDisabled);
    if (!created_direction) {
        return InstallFailure{InstallStage::Create, NativeSite::Direction};
    }
    pending.direction = std::move(*created_direction);

    auto created_held_age = safetyhook::InlineHook::create(
        reinterpret_cast<void*>(executable_base + kHeldAgeRva),
        reinterpret_cast<void*>(&HookHeldAge),
        safetyhook::InlineHook::StartDisabled);
    if (!created_held_age) {
        return InstallFailure{InstallStage::Create, NativeSite::HeldAge};
    }
    pending.held_age = std::move(*created_held_age);

    g_active_hooks = &pending;
    if (!EnableHook(pending.pressed)) {
        return InstallFailure{InstallStage::Enable, NativeSite::Pressed};
    }
    if (!EnableHook(pending.held)) {
        return InstallFailure{InstallStage::Enable, NativeSite::Held};
    }
    if (!EnableHook(pending.released)) {
        return InstallFailure{InstallStage::Enable, NativeSite::Released};
    }
    if (!EnableHook(pending.direction)) {
        return InstallFailure{InstallStage::Enable, NativeSite::Direction};
    }
    if (!EnableHook(pending.held_age)) {
        return InstallFailure{InstallStage::Enable, NativeSite::HeldAge};
    }
    if (!EnableHook(pending.loop_guard)) {
        return InstallFailure{InstallStage::Enable, NativeSite::LoopGuard};
    }
    if (!EnableHook(pending.stage_end)) {
        return InstallFailure{InstallStage::Enable, NativeSite::StageEnd};
    }
    // The authoritative begin hook is the final operational commit point.
    if (!EnableHook(pending.stage_begin)) {
        return InstallFailure{InstallStage::Enable, NativeSite::StageBegin};
    }

    return std::nullopt;
}

template <typename Value>
[[nodiscard]] Value AnswerQueryOrFatal(
    const JudgementQueryResult<Value>& result) noexcept {
    if (result.disposition == JudgementQueryDisposition::Answered) {
        return result.value;
    }
    FailAbsoluteJudgementQueryInvariant(
        result.invariant, result.history_error);
}

} // namespace

std::uint8_t __fastcall HookStageBegin(
    void* const self,
    void*) noexcept {
    const auto result =
        g_active_hooks->stage_begin.unsafe_thiscall<std::uint8_t>(self);
    if (result != 0) {
        BeginAbsoluteJudgementNativeStage(
            reinterpret_cast<std::uintptr_t>(self));
    }
    return result;
}

int __fastcall HookStageEnd(void* const self, void*) noexcept {
    EndAbsoluteJudgementNativeStage(
        reinterpret_cast<std::uintptr_t>(self));
    return g_active_hooks->stage_end.unsafe_thiscall<int>(self);
}

void HookLoopGuard(safetyhook::Context& context) noexcept {
    if (!AbsoluteJudgementNativeStageOpen()) {
        return;
    }
    try {
        DispatchAbsoluteJudgementOuterCall(context);
    } catch (const std::bad_alloc&) {
        FailAbsoluteJudgementActiveStage(
            AbsoluteJudgementFatalReason::StorageAllocationFailure);
    } catch (...) {
        FailAbsoluteJudgementActiveStage(
            AbsoluteJudgementFatalReason::UnexpectedInternalException);
    }
}

std::uint8_t __fastcall HookPressed(
    void* const self,
    void*,
    const int id,
    const int frame) noexcept {
    const auto query = QueryJudgementPressed(
        self, AbsoluteJudgementStageGeneration(), id, frame);
    if (query.disposition == JudgementQueryDisposition::Inactive) {
        return g_active_hooks->pressed.unsafe_thiscall<std::uint8_t>(
            self, id, frame);
    }
    return AnswerQueryOrFatal(query);
}

std::uint8_t __fastcall HookHeld(
    void* const self,
    void*,
    const int id,
    const int frame) noexcept {
    const auto query = QueryJudgementHeld(
        self, AbsoluteJudgementStageGeneration(), id, frame);
    if (query.disposition == JudgementQueryDisposition::Inactive) {
        return g_active_hooks->held.unsafe_thiscall<std::uint8_t>(
            self, id, frame);
    }
    return AnswerQueryOrFatal(query);
}

std::uint8_t __fastcall HookReleased(
    void* const self,
    void*,
    const int id,
    const int frame) noexcept {
    const auto query = QueryJudgementReleased(
        self, AbsoluteJudgementStageGeneration(), id, frame);
    if (query.disposition == JudgementQueryDisposition::Inactive) {
        return g_active_hooks->released.unsafe_thiscall<std::uint8_t>(
            self, id, frame);
    }
    return AnswerQueryOrFatal(query);
}

int __fastcall HookDirection(
    void* const self,
    void*,
    const int booster,
    float* const x,
    float* const y,
    const int frame) noexcept {
    const auto query = QueryJudgementDirection(
        self,
        AbsoluteJudgementStageGeneration(),
        booster,
        x,
        y,
        frame);
    if (query.disposition == JudgementQueryDisposition::Inactive) {
        return g_active_hooks->direction.unsafe_thiscall<int>(
            self, booster, x, y, frame);
    }
    return AnswerQueryOrFatal(query);
}

int __fastcall HookHeldAge(
    void* const self,
    void*,
    const unsigned int id) noexcept {
    const auto query = QueryJudgementHeldAge(
        self, AbsoluteJudgementStageGeneration(), id);
    if (query.disposition == JudgementQueryDisposition::Inactive) {
        return g_active_hooks->held_age.unsafe_thiscall<int>(self, id);
    }
    return AnswerQueryOrFatal(query);
}

void InitializeAbsoluteJudgementOrFatal() noexcept {
    auto& config = ConfigManager::instance();
    const bool enabled = config.GetEnableAbsoluteTimeJudgement();
    if (!gc::input::PrepareGameplayTransitionTransport(enabled)) {
        PublishStartupFatal(
            "AbsoluteJudgement: startup fatal stage=transport_prepare");
    }

    const auto target_fps = config.GetTargetFps();
    const auto input_rate_hz = config.GetInputPollHertz();
    const auto backend = config.GetAudioBackend();
    if (!enabled) {
        JudgementDiagnostics().LogStartup({
            .enabled = false,
            .target_fps = target_fps,
            .input_rate_hz = input_rate_hz,
            .backend = gc::config::AudioBackendName(backend),
            .exact_provider_capable = false,
            .installed_site_count = 0,
        });
        if (target_fps != 60) {
            PLOG_WARNING
                << "AbsoluteJudgement: stock judgement at non-60 target FPS "
                   "has no FPS-independent judgement guarantee";
        }
        return;
    }

    if (backend != gc::config::AudioBackend::wasapi_exclusive) {
        PublishStartupFatal(
            "AbsoluteJudgement: startup fatal stage=capability "
            "reason=backend_not_wasapi_exclusive");
    }
    if (input_rate_hz != 1000) {
        PublishStartupFatal(
            "AbsoluteJudgement: startup fatal stage=capability "
            "reason=input_poll_rate_not_1000");
    }
    if (!gc::audio::IsAudioHookCommitted()) {
        PublishStartupFatal(
            "AbsoluteJudgement: startup fatal stage=capability "
            "reason=exact_wasapi_route_unavailable");
    }

    const auto executable_base = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    if (executable_base == 0) {
        PublishStartupFatal(
            "AbsoluteJudgement: startup fatal stage=executable_base");
    }
    InitializeAbsoluteJudgementRuntime(executable_base);
    const auto installation = InstallHooks(executable_base);
    if (installation) {
        PublishInstallFailure(*installation);
    }

    JudgementDiagnostics().LogStartup({
        .enabled = true,
        .target_fps = target_fps,
        .input_rate_hz = input_rate_hz,
        .backend = gc::config::AudioBackendName(backend),
        .exact_provider_capable = true,
        .installed_site_count = 8,
    });
}

} // namespace gc::absolute_judgement
