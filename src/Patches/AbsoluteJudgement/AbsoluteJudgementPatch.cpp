#include "Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h"

#include "Audio/AudioPatch.h"
#include "Input/Polling/GameplayTransitionJournal.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementDiagnostics.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"
#include "Patches/AbsoluteJudgement/JudgementScope.h"
#include "Patches/AbsoluteJudgement/NativeJudgementAbi.h"
#include "Logging/SessionLog.h"
#include "SystemPath/StartupFatal.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdlib>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "plog/Log.h"

namespace gc::absolute_judgement
{
    namespace
    {
        using namespace native_abi;

        enum class NativeSite : std::size_t
        {
            GameplayInitialization,
            SemanticStageEntry,
            SemanticStageExit,
            LoopGuard,
            Pressed,
            Held,
            Released,
            Direction,
            HeldAge,
        };

        enum class InstallStage : std::size_t
        {
            Preflight,
            Create,
            Enable,
        };

        struct NativeSiteContract final
        {
            NativeSite site{};
            std::uintptr_t rva{};
            std::span<const std::uint8_t> expected;
        };

        struct InstallFailure final
        {
            InstallStage stage{};
            NativeSite site{};
        };

        struct AbsoluteJudgementHooks final
        {
            safetyhook::MidHook gameplay_initialization;
            safetyhook::MidHook semantic_stage_entry;
            safetyhook::MidHook semantic_stage_exit;
            safetyhook::MidHook loop_guard;
            safetyhook::InlineHook pressed;
            safetyhook::InlineHook held;
            safetyhook::InlineHook released;
            safetyhook::InlineHook direction;
            safetyhook::InlineHook held_age;
            safetyhook::InlineHook timing_grade;
        };

        inline constexpr std::array<NativeSiteContract, 9> kSiteContracts{
            {
                {
                    NativeSite::GameplayInitialization,
                    kGameplayInitializationRva,
                    kGameplayInitializationPrefix
                },
                {
                    NativeSite::SemanticStageEntry,
                    kSemanticStageEntryRva,
                    kSemanticStageEntryPrefix
                },
                {
                    NativeSite::SemanticStageExit,
                    kSemanticStageExitRva,
                    kSemanticStageExitPrefix
                },
                {NativeSite::LoopGuard, kLoopGuardRva, kLoopGuardPrefix},
                {NativeSite::Pressed, kPressedRva, kPressedPrefix},
                {NativeSite::Held, kHeldRva, kHeldPrefix},
                {NativeSite::Released, kReleasedRva, kReleasedPrefix},
                {NativeSite::Direction, kDirectionRva, kDirectionPrefix},
                {NativeSite::HeldAge, kHeldAgeRva, kHeldAgePrefix},
            }
        };

        inline constexpr std::array<std::string_view, 9> kSiteNames{
            {
                "gameplay_initialization",
                "semantic_stage_entry",
                "semantic_stage_exit",
                "loop_guard",
                "pressed",
                "held",
                "released",
                "direction",
                "held_age",
            }
        };

        AbsoluteJudgementHooks g_hooks;
        AbsoluteJudgementHooks* g_active_hooks{};
        std::atomic_bool g_startup_fatal_published{false};

        [[noreturn]] void PublishStartupFatal(
            const AbsoluteJudgementFatalPredicate predicate,
            const std::string_view details) noexcept
        {
            std::array < char, 1024 > log{};
            const auto formatted = std::format_to_n(
                log.data(),
                log.size() - 1,
                "AbsoluteJudgement: startup-fatal predicate_id={} predicate={} {}",
                static_cast<unsigned>(predicate),
                AbsoluteJudgementFatalPredicateName(predicate),
                details);
            const auto size = (std::min)(
                static_cast<std::size_t>(formatted.size), log.size() - 1);
            log[size] = '\0';
            gc::system_path::PublishStartupFatal(
                g_startup_fatal_published,
                std::string_view(log.data(), size),
                L"GCLoader could not install the absolute-time judgement patch. "
                L"The supported game executable and enabled-mode configuration are "
                L"required. Check loader-log.txt for the exact failed stage and site.",
                L"GCLoader absolute-time judgement setup error",
                27);
            PLOG_FATAL << std::format(
                "AbsoluteJudgement: startup-fatal predicate_id={} predicate={}"
                " prior_predicate_id={} prior_predicate={}"
                " expression=startup_fatal_publisher_returned",
                static_cast<unsigned>(
                    AbsoluteJudgementFatalPredicate::StartupFatalPublisherReturned),
                AbsoluteJudgementFatalPredicateName(
                    AbsoluteJudgementFatalPredicate::StartupFatalPublisherReturned),
                static_cast<unsigned>(predicate),
                AbsoluteJudgementFatalPredicateName(predicate));
            gc::session_log::FlushActiveProcessLog();
            SetLastError(ERROR_SUCCESS);
            const auto terminated = TerminateProcess(GetCurrentProcess(), 0xA7);
            const auto last_error = GetLastError();
            PLOG_FATAL << std::format(
                "AbsoluteJudgement: startup-fatal predicate_id={} predicate={}"
                " return_value={} last_error={}",
                static_cast<unsigned>(
                    AbsoluteJudgementFatalPredicate::TerminateProcessReturned),
                AbsoluteJudgementFatalPredicateName(
                    AbsoluteJudgementFatalPredicate::TerminateProcessReturned),
                terminated,
                last_error);
            gc::session_log::FlushActiveProcessLog();
            RaiseFailFastException(nullptr, nullptr, 0);
            std::abort();
        }

        [[noreturn]] void PublishInstallFailure(
            const InstallFailure failure) noexcept
        {
            const auto site = static_cast<std::size_t>(failure.site);
            if (site >= kSiteNames.size())
            {
                PublishStartupFatal(
                    AbsoluteJudgementFatalPredicate::StartupHookTransactionInvalid,
                    "stage=unknown site=out_of_range");
            }
            switch (failure.stage)
            {
            case InstallStage::Preflight:
                PublishStartupFatal(
                    AbsoluteJudgementFatalPredicate::StartupSitePrefixMismatch,
                    std::format(
                        "stage=preflight site={} rva={}",
                        kSiteNames[site],
                        kSiteContracts[site].rva));
            case InstallStage::Create:
                PublishStartupFatal(
                    AbsoluteJudgementFatalPredicate::StartupHookCreateFailed,
                    std::format(
                        "stage=create site={} rva={}",
                        kSiteNames[site],
                        kSiteContracts[site].rva));
            case InstallStage::Enable:
                PublishStartupFatal(
                    AbsoluteJudgementFatalPredicate::StartupHookEnableFailed,
                    std::format(
                        "stage=enable site={} rva={}",
                        kSiteNames[site],
                        kSiteContracts[site].rva));
            }
            PublishStartupFatal(
                AbsoluteJudgementFatalPredicate::StartupHookTransactionInvalid,
                std::format(
                    "stage={} site={}",
                    static_cast<std::size_t>(failure.stage),
                    site));
        }

        [[nodiscard]] bool AddAddress(
            const std::uintptr_t base,
            const std::uintptr_t rva,
            std::uintptr_t* const result) noexcept
        {
            if (result == nullptr ||
                rva > (std::numeric_limits<std::uintptr_t>::max)() - base)
            {
                return false;
            }
            *result = base + rva;
            return true;
        }

        [[nodiscard]] bool AddSignedAddress(
            const std::uintptr_t base,
            const std::ptrdiff_t offset,
            std::uintptr_t* const result) noexcept
        {
            if (result == nullptr)
            {
                return false;
            }
            if (offset < 0)
            {
                const auto magnitude = static_cast<std::uintptr_t>(-offset);
                if (magnitude > base)
                {
                    return false;
                }
                *result = base - magnitude;
                return true;
            }
            return AddAddress(base, static_cast<std::uintptr_t>(offset), result);
        }

        [[nodiscard]] bool ReadU32Safe(
            const std::uintptr_t address,
            std::uint32_t* const value) noexcept
        {
            if (address == 0 || value == nullptr)
            {
                return false;
            }
            __try
            {
                *value = *reinterpret_cast<volatile const std::uint32_t*>(address);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        [[nodiscard]] std::uintptr_t ReadSemanticTuneManagerOrFatal(
            const safetyhook::Context& context) noexcept
        {
            std::uintptr_t slot{};
            std::uint32_t tune_manager{};
            if (!AddSignedAddress(
                static_cast<std::uintptr_t>(context.ebp),
                kSemanticStageTuneStackOffset,
                &slot) || !ReadU32Safe(slot, &tune_manager))
            {
                FailAbsoluteJudgementActiveStage(
                    AbsoluteJudgementFatalPredicate::GameImageAddressInvalid,
                    AbsoluteJudgementFatalReason::NativeStateMismatch,
                    {
                        context.ebp,
                        static_cast<std::uint64_t>(kSemanticStageTuneStackOffset)
                    });
            }
            if (tune_manager == 0)
            {
                FailAbsoluteJudgementActiveStage(
                    AbsoluteJudgementFatalPredicate::TuneMissing,
                    AbsoluteJudgementFatalReason::NativeStateMismatch,
                    {slot, tune_manager});
            }
            return static_cast<std::uintptr_t>(tune_manager);
        }

        [[nodiscard]] bool PrefixMatchesSafe(
            const std::uintptr_t address,
            const std::span<const std::uint8_t> expected) noexcept
        {
            if (address == 0 || expected.empty())
            {
                return false;
            }
            __try
            {
                const auto* bytes = reinterpret_cast<volatile const std::uint8_t*>(
                    address);
                for (std::size_t index = 0; index < expected.size(); ++index)
                {
                    if (bytes[index] != expected[index])
                    {
                        return false;
                    }
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        template <typename Hook>
        [[nodiscard]] bool EnableHook(Hook& hook)
        {
            const auto result = hook.enable();
            return result.has_value();
        }

        [[nodiscard]] std::optional<InstallFailure> InstallHooks(
            const std::uintptr_t executable_base,
            const bool install_judgement_hooks)
        {
            const std::span<const NativeSiteContract> contracts =
                install_judgement_hooks
                    ? std::span<const NativeSiteContract>{kSiteContracts}
                    : std::span<const NativeSiteContract>{kSiteContracts}.first(3);
            for (const auto& contract : contracts)
            {
                std::uintptr_t address{};
                if (!AddAddress(executable_base, contract.rva, &address) ||
                    !PrefixMatchesSafe(address, contract.expected))
                {
                    return InstallFailure{
                        .stage = InstallStage::Preflight,
                        .site = contract.site,
                    };
                }
            }

            // Install directly into stable process-lifetime storage. Once any hook is
            // enabled its handler must never observe a moved-from trampoline owner.
            auto& pending = g_hooks;
            auto created_gameplay_initialization = safetyhook::MidHook::create(
                reinterpret_cast<void*>(
                    executable_base + kGameplayInitializationRva),
                &HookGameplayInitialization,
                safetyhook::MidHook::StartDisabled);
            if (!created_gameplay_initialization)
            {
                return InstallFailure{
                    InstallStage::Create, NativeSite::GameplayInitialization
                };
            }
            pending.gameplay_initialization =
                std::move(*created_gameplay_initialization);

            auto created_semantic_stage_entry = safetyhook::MidHook::create(
                reinterpret_cast<void*>(
                    executable_base + kSemanticStageEntryRva),
                &HookSemanticStageEntry,
                safetyhook::MidHook::StartDisabled);
            if (!created_semantic_stage_entry)
            {
                return InstallFailure{
                    InstallStage::Create, NativeSite::SemanticStageEntry
                };
            }
            pending.semantic_stage_entry =
                std::move(*created_semantic_stage_entry);

            auto created_semantic_stage_exit = safetyhook::MidHook::create(
                reinterpret_cast<void*>(
                    executable_base + kSemanticStageExitRva),
                &HookSemanticStageExit,
                safetyhook::MidHook::StartDisabled);
            if (!created_semantic_stage_exit)
            {
                return InstallFailure{
                    InstallStage::Create, NativeSite::SemanticStageExit
                };
            }
            pending.semantic_stage_exit = std::move(*created_semantic_stage_exit);

            if (!install_judgement_hooks)
            {
                g_active_hooks = &pending;
                if (!EnableHook(pending.semantic_stage_exit))
                {
                    return InstallFailure{
                        InstallStage::Enable, NativeSite::SemanticStageExit
                    };
                }
                if (!EnableHook(pending.gameplay_initialization))
                {
                    return InstallFailure{
                        InstallStage::Enable,
                        NativeSite::GameplayInitialization
                    };
                }
                if (!EnableHook(pending.semantic_stage_entry))
                {
                    return InstallFailure{
                        InstallStage::Enable, NativeSite::SemanticStageEntry
                    };
                }
                return std::nullopt;
            }

            auto created_loop_guard = safetyhook::MidHook::create(
                reinterpret_cast<void*>(executable_base + kLoopGuardRva),
                &HookLoopGuard,
                safetyhook::MidHook::StartDisabled);
            if (!created_loop_guard)
            {
                return InstallFailure{InstallStage::Create, NativeSite::LoopGuard};
            }
            pending.loop_guard = std::move(*created_loop_guard);

            auto created_pressed = safetyhook::InlineHook::create(
                reinterpret_cast<void*>(executable_base + kPressedRva),
                reinterpret_cast<void*>(&HookPressed),
                safetyhook::InlineHook::StartDisabled);
            if (!created_pressed)
            {
                return InstallFailure{InstallStage::Create, NativeSite::Pressed};
            }
            pending.pressed = std::move(*created_pressed);

            auto created_held = safetyhook::InlineHook::create(
                reinterpret_cast<void*>(executable_base + kHeldRva),
                reinterpret_cast<void*>(&HookHeld),
                safetyhook::InlineHook::StartDisabled);
            if (!created_held)
            {
                return InstallFailure{InstallStage::Create, NativeSite::Held};
            }
            pending.held = std::move(*created_held);

            auto created_released = safetyhook::InlineHook::create(
                reinterpret_cast<void*>(executable_base + kReleasedRva),
                reinterpret_cast<void*>(&HookReleased),
                safetyhook::InlineHook::StartDisabled);
            if (!created_released)
            {
                return InstallFailure{InstallStage::Create, NativeSite::Released};
            }
            pending.released = std::move(*created_released);

            auto created_direction = safetyhook::InlineHook::create(
                reinterpret_cast<void*>(executable_base + kDirectionRva),
                reinterpret_cast<void*>(&HookDirection),
                safetyhook::InlineHook::StartDisabled);
            if (!created_direction)
            {
                return InstallFailure{InstallStage::Create, NativeSite::Direction};
            }
            pending.direction = std::move(*created_direction);

            auto created_held_age = safetyhook::InlineHook::create(
                reinterpret_cast<void*>(executable_base + kHeldAgeRva),
                reinterpret_cast<void*>(&HookHeldAge),
                safetyhook::InlineHook::StartDisabled);
            if (!created_held_age)
            {
                return InstallFailure{InstallStage::Create, NativeSite::HeldAge};
            }
            pending.held_age = std::move(*created_held_age);

            g_active_hooks = &pending;
            if (!EnableHook(pending.pressed))
            {
                return InstallFailure{InstallStage::Enable, NativeSite::Pressed};
            }
            if (!EnableHook(pending.held))
            {
                return InstallFailure{InstallStage::Enable, NativeSite::Held};
            }
            if (!EnableHook(pending.released))
            {
                return InstallFailure{InstallStage::Enable, NativeSite::Released};
            }
            if (!EnableHook(pending.direction))
            {
                return InstallFailure{InstallStage::Enable, NativeSite::Direction};
            }
            if (!EnableHook(pending.held_age))
            {
                return InstallFailure{InstallStage::Enable, NativeSite::HeldAge};
            }
            if (!EnableHook(pending.loop_guard))
            {
                return InstallFailure{InstallStage::Enable, NativeSite::LoopGuard};
            }
            if (!EnableHook(pending.semantic_stage_exit))
            {
                return InstallFailure{
                    InstallStage::Enable, NativeSite::SemanticStageExit
                };
            }
            if (!EnableHook(pending.gameplay_initialization))
            {
                return InstallFailure{
                    InstallStage::Enable, NativeSite::GameplayInitialization
                };
            }
            // The semantic entry hook is the final operational commit point.
            if (!EnableHook(pending.semantic_stage_entry))
            {
                return InstallFailure{
                    InstallStage::Enable, NativeSite::SemanticStageEntry
                };
            }

            return std::nullopt;
        }

        [[nodiscard]] bool InstallTimingGradeDiagnosticHook(
            const std::uintptr_t executable_base)
        {
            std::uintptr_t address{};
            if (!AddAddress(executable_base, kTimingGradeRva, &address) ||
                !PrefixMatchesSafe(address, kTimingGradePrefix))
            {
                PLOG_WARNING << std::format(
                    "AbsoluteJudgement: diagnostic-hook feature=timing_grade"
                    " available=0 stage=preflight rva={:#x}",
                    kTimingGradeRva);
                return false;
            }

            auto created = safetyhook::InlineHook::create(
                reinterpret_cast<void*>(address),
                reinterpret_cast<void*>(&HookTimingGrade),
                safetyhook::InlineHook::StartDisabled);
            if (!created)
            {
                PLOG_WARNING << std::format(
                    "AbsoluteJudgement: diagnostic-hook feature=timing_grade"
                    " available=0 stage=create rva={:#x}",
                    kTimingGradeRva);
                return false;
            }
            g_hooks.timing_grade = std::move(*created);
            if (!EnableHook(g_hooks.timing_grade))
            {
                PLOG_WARNING << std::format(
                    "AbsoluteJudgement: diagnostic-hook feature=timing_grade"
                    " available=0 stage=enable rva={:#x}",
                    kTimingGradeRva);
                return false;
            }
            return true;
        }

        template <typename Value>
        [[nodiscard]] Value AnswerQueryOrFatal(
            const JudgementQueryResult<Value>& result) noexcept
        {
            if (result.disposition == JudgementQueryDisposition::Answered)
            {
                return result.value;
            }
            FailAbsoluteJudgementQueryInvariant(
                result.invariant,
                result.history_error,
                result.failure_operand0,
                result.failure_operand1,
                result.failure_operand_count);
        }
    } // namespace

    // SafetyHook requires a mutable Context reference in the mid-hook callback ABI.
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void HookGameplayInitialization(safetyhook::Context& context) noexcept
    {
        ObserveAbsoluteJudgementGameplayInitialization(context.ecx);
    }

    // SafetyHook requires a mutable Context reference in the mid-hook callback ABI.
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void HookSemanticStageEntry(safetyhook::Context& context) noexcept
    {
        BeginAbsoluteJudgementSemanticStage(
            ReadSemanticTuneManagerOrFatal(context));
    }

    // SafetyHook requires a mutable Context reference in the mid-hook callback ABI.
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void HookSemanticStageExit(safetyhook::Context& context) noexcept
    {
        EndAbsoluteJudgementSemanticStage(
            ReadSemanticTuneManagerOrFatal(context));
    }

    void HookLoopGuard(safetyhook::Context& context) noexcept
    {
        if (!AbsoluteJudgementSemanticStageOpen())
        {
            FailAbsoluteJudgementActiveStage(
                AbsoluteJudgementFatalPredicate::SemanticStageMissingAtOwnedLoop,
                AbsoluteJudgementFatalReason::NativeStateMismatch);
        }
        DispatchAbsoluteJudgementOuterCall(context);
    }

    std::uint8_t __fastcall HookPressed(
        void* const self,
        void*,
        const int id,
        const int frame) noexcept
    {
        if (ActiveJudgementScopeData() == nullptr)
        {
            return g_active_hooks->pressed.unsafe_thiscall<std::uint8_t>(
                self, id, frame);
        }
        const auto query = QueryJudgementPressed(
            self, AbsoluteJudgementStageGeneration(), id, frame);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return g_active_hooks->pressed.unsafe_thiscall<std::uint8_t>(
                self, id, frame);
        }
        return AnswerQueryOrFatal(query);
    }

    std::uint8_t __fastcall HookHeld(
        void* const self,
        void*,
        const int id,
        const int frame) noexcept
    {
        if (ActiveJudgementScopeData() == nullptr)
        {
            return g_active_hooks->held.unsafe_thiscall<std::uint8_t>(
                self, id, frame);
        }
        const auto query = QueryJudgementHeld(
            self, AbsoluteJudgementStageGeneration(), id, frame);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return g_active_hooks->held.unsafe_thiscall<std::uint8_t>(
                self, id, frame);
        }
        return AnswerQueryOrFatal(query);
    }

    std::uint8_t __fastcall HookReleased(
        void* const self,
        void*,
        const int id,
        const int frame) noexcept
    {
        if (ActiveJudgementScopeData() == nullptr)
        {
            return g_active_hooks->released.unsafe_thiscall<std::uint8_t>(
                self, id, frame);
        }
        const auto query = QueryJudgementReleased(
            self, AbsoluteJudgementStageGeneration(), id, frame);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
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
        const int frame) noexcept
    {
        if (ActiveJudgementScopeData() == nullptr)
        {
            return g_active_hooks->direction.unsafe_thiscall<int>(
                self, booster, x, y, frame);
        }
        const auto query = QueryJudgementDirection(
            self,
            AbsoluteJudgementStageGeneration(),
            booster,
            x,
            y,
            frame);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return g_active_hooks->direction.unsafe_thiscall<int>(
                self, booster, x, y, frame);
        }
        return AnswerQueryOrFatal(query);
    }

    int __fastcall HookHeldAge(
        void* const self,
        void*,
        const unsigned int id) noexcept
    {
        if (ActiveJudgementScopeData() == nullptr)
        {
            return g_active_hooks->held_age.unsafe_thiscall<int>(self, id);
        }
        const auto query = QueryJudgementHeldAge(
            self, AbsoluteJudgementStageGeneration(), id);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return g_active_hooks->held_age.unsafe_thiscall<int>(self, id);
        }
        return AnswerQueryOrFatal(query);
    }

    int __fastcall HookTimingGrade(
        void* const self,
        void*,
        const float* const note,
        const int recognition_ms) noexcept
    {
        const bool record = ActiveJudgementScopeData() != nullptr &&
            note != nullptr;
        std::int32_t note_target_ms{};
        if (record)
        {
            note_target_ms = static_cast<std::int32_t>(
                note[kTimingGradeNoteTargetFloatIndex]);
        }

        const int native_grade =
            g_active_hooks->timing_grade.unsafe_thiscall<int>(
                self, note, recognition_ms);
        if (record)
        {
            RecordActiveTimingGradeObservation(
                reinterpret_cast<std::uintptr_t>(note),
                recognition_ms,
                note_target_ms,
                native_grade);
        }
        return native_grade;
    }

    void InitializeAbsoluteJudgementOrFatal(
        // The patch boundary owns its immutable startup snapshot.
        // ReSharper disable once CppPassValueParameterByConstReference
        JudgementSettings settings) noexcept
    {
        const bool enabled = settings.enabled();
        if (!gc::input::PrepareGameplayTransitionTransport(enabled))
        {
            PublishStartupFatal(
                AbsoluteJudgementFatalPredicate::
                InputQpcFrequencyInvalidAtStageEntry,
                "stage=transport_prepare qpc_frequency_unavailable=1");
        }

        const auto target_fps = settings.target_fps();
        const auto input_rate_hz = settings.input_rate_hz();
        const auto backend = settings.audio_backend();
        const bool audio_hook_committed = gc::audio::IsAudioHookCommitted();
        const bool lifecycle_hooks_required =
            enabled || backend == gc::audio::AudioBackend::asio;
        JudgementDiagnostics().SetStartupTargetFps(target_fps);
        if (!lifecycle_hooks_required)
        {
            JudgementDiagnostics().LogStartup({
                .enabled = false,
                .target_fps = target_fps,
                .input_rate_hz = input_rate_hz,
                .backend = gc::audio::AudioBackendName(backend),
                .audio_hook_committed = audio_hook_committed,
                .installed_site_count = 0,
                .timing_grade_diagnostic_hook = false,
            });
            if (target_fps != 60)
            {
                PLOG_WARNING
                    << "AbsoluteJudgement: stock judgement at non-60 target FPS "
                    "has no FPS-independent judgement guarantee";
            }
            return;
        }

        const auto expected_domain = settings.expected_clock_domain();
        if (enabled && backend == gc::audio::AudioBackend::wasapi_exclusive &&
            !expected_domain.has_value())
        {
            PublishStartupFatal(
                AbsoluteJudgementFatalPredicate::
                AudioBackendUnsupportedForAbsoluteJudgement,
                std::format(
                    "stage=capability configured_backend={}",
                    static_cast<std::uint32_t>(backend)));
        }
        if (enabled && input_rate_hz != 1000)
        {
            PublishStartupFatal(
                AbsoluteJudgementFatalPredicate::InputTransportRateNot1000,
                std::format(
                    "stage=capability configured_rate_hz={}", input_rate_hz));
        }
        if (!audio_hook_committed)
        {
            PublishStartupFatal(
                AbsoluteJudgementFatalPredicate::ExactAudioHookRouteUnavailable,
                "stage=capability audio_hook_committed=0");
        }

        const auto executable_base = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        if (executable_base == 0)
        {
            PublishStartupFatal(
                AbsoluteJudgementFatalPredicate::GameImageAddressInvalid,
                "stage=executable_base module_handle=0");
        }
        InitializeAbsoluteJudgementRuntime(
            executable_base, backend, enabled, expected_domain);
        if (const auto installation = InstallHooks(executable_base, enabled);
            installation)
        {
            PublishInstallFailure(*installation);
        }
        const bool timing_grade_diagnostic_hook = enabled &&
            InstallTimingGradeDiagnosticHook(executable_base);

        JudgementDiagnostics().LogStartup({
            .enabled = enabled,
            .target_fps = target_fps,
            .input_rate_hz = input_rate_hz,
            .backend = gc::audio::AudioBackendName(backend),
            .audio_hook_committed = audio_hook_committed,
            .installed_site_count = enabled ? kSiteContracts.size() : 3,
            .timing_grade_diagnostic_hook =
            timing_grade_diagnostic_hook,
        });
        if (!enabled && target_fps != 60)
        {
            PLOG_WARNING
                << "AbsoluteJudgement: stock judgement at non-60 target FPS "
                   "has no FPS-independent judgement guarantee";
        }
    }
} // namespace gc::absolute_judgement
