#include "Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementProfile.h"
#include "Patches/AbsoluteJudgement/AbsoluteJudgementRuntime.h"
#include "Patches/AbsoluteJudgement/JudgementScope.h"
#include "Audio/AudioPatch.h"
#include "Input/Polling/GameplayTransitionJournal.h"
#include "Diagnostics/FatalProcess.h"
#include <Windows.h>
#include <format>
#include <limits>
#include <string_view>
#include "plog/Log.h"

namespace gc::absolute_judgement {
namespace detail { QueryOriginals g_originals; }
namespace {
native_abi::NativeLayout g_layout{};
bool g_observe_lifecycle{};

[[noreturn]] void AbortStartup(
    AbsoluteJudgementFatalPredicate predicate, std::string_view details) noexcept {
    diagnostics::AbortProcess({
        std::format("AbsoluteJudgement: startup-fatal predicate_id={} predicate={} {}",
            static_cast<unsigned>(predicate), AbsoluteJudgementFatalPredicateName(predicate), details),
        L"GCLoader could not prepare absolute-time judgement. Check loader-log.txt.",
        L"GCLoader absolute-time judgement setup error"});
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
                g_layout.semantic_stage_tune_stack_offset,
                &slot) || !ReadU32Safe(slot, &tune_manager))
            {
                FailAbsoluteJudgementActiveStage(
                    AbsoluteJudgementFatalPredicate::GameImageAddressInvalid,
                    AbsoluteJudgementFatalReason::NativeStateMismatch,
                    {
                        context.ebp,
                        static_cast<std::uint64_t>(g_layout.semantic_stage_tune_stack_offset)
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
        if (!g_observe_lifecycle) return;
        ObserveAbsoluteJudgementGameplayInitialization(context.ecx);
    }

    // SafetyHook requires a mutable Context reference in the mid-hook callback ABI.
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void HookSemanticStageEntry(safetyhook::Context& context) noexcept
    {
        if (!g_observe_lifecycle) return;
        BeginAbsoluteJudgementSemanticStage(
            ReadSemanticTuneManagerOrFatal(context));
    }

    // SafetyHook requires a mutable Context reference in the mid-hook callback ABI.
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void HookSemanticStageExit(safetyhook::Context& context) noexcept
    {
        if (!g_observe_lifecycle) return;
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
            return detail::g_originals.pressed(
                self, id, frame);
        }
        const auto query = QueryJudgementPressed(
            self, AbsoluteJudgementStageGeneration(), id, frame);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return detail::g_originals.pressed(
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
            return detail::g_originals.held(
                self, id, frame);
        }
        const auto query = QueryJudgementHeld(
            self, AbsoluteJudgementStageGeneration(), id, frame);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return detail::g_originals.held(
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
            return detail::g_originals.released(
                self, id, frame);
        }
        const auto query = QueryJudgementReleased(
            self, AbsoluteJudgementStageGeneration(), id, frame);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return detail::g_originals.released(
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
            return detail::g_originals.direction(
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
            return detail::g_originals.direction(
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
            return detail::g_originals.held_age(self, id);
        }
        const auto query = QueryJudgementHeldAge(
            self, AbsoluteJudgementStageGeneration(), id);
        if (query.disposition == JudgementQueryDisposition::Inactive)
        {
            return detail::g_originals.held_age(self, id);
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
                note[g_layout.timing_grade_note_target_float_index]);
        }

        const int native_grade =
            detail::g_originals.timing_grade(
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


std::expected<void, game_version::PlanError> PrepareAbsoluteJudgementRuntime(
    const game_version::ApprovedVersionedPlan& plan,
    const runtime_image::RuntimeImage& image, const JudgementSettings& settings) noexcept {
    using namespace game_version;
    const auto invalid = [&](std::string_view site) {
        return std::unexpected(PlanError{.stage = PlanStage::invalid_plan,
            .context = plan.context(), .feature = FeatureId::absolute_judgement, .site = site});
    };
    const auto* build = std::get_if<GameBuild>(&plan.context().build);
    const auto* variant = std::get_if<GameImageVariant>(&plan.context().variant);
    const auto* profile = build && variant ? ProfileFor(*build, *variant) : nullptr;
    if (!profile || image.base() != plan.image_base() || image.size() != plan.image_size())
        return invalid("runtime_image_binding");

    native_abi::NativeTargets targets{};
    std::size_t contracts{}, read_targets{};
    for (const auto& site : plan.sites()) {
        const auto& contract = site.contract();
        if (contract.feature != FeatureId::absolute_judgement) continue;
        ++contracts;
        if (contract.kind != VersionedOperationKind::read_only_contract) continue;
        const auto resolved = image.Resolve(
            {"absolute_judgement", contract.site, contract.rva}, contract.protected_span);
        if (!resolved) return std::unexpected(PlanError{.stage = PlanStage::address_range,
            .context = plan.context(), .feature = contract.feature, .site = contract.site,
            .rva = contract.rva, .memory = resolved.error()});
        if (*resolved != site.address) return invalid(contract.site);
        ++read_targets;
        using namespace native_abi;
        if (contract.site == "loop_tail") targets.loop_tail = *resolved;
        else if (contract.site == "recognition") targets.recognition = reinterpret_cast<RecognitionFn>(*resolved);
        else if (contract.site == "score") targets.score = reinterpret_cast<ScoreFn>(*resolved);
        else if (contract.site == "get_input_manager") targets.get_input_manager = reinterpret_cast<AccessorFn>(*resolved);
        else if (contract.site == "get_global") targets.get_global = reinterpret_cast<AccessorFn>(*resolved);
        else if (contract.site == "get_config") targets.get_config = reinterpret_cast<AccessorFn>(*resolved);
        else if (contract.site == "get_sound_manager") targets.get_sound_manager = reinterpret_cast<AccessorFn>(*resolved);
        else if (contract.site == "get_group_cursor") targets.get_group_cursor = reinterpret_cast<GetGroupCursorFn>(*resolved);
        else return invalid(contract.site);
    }
    const bool enabled = settings.enabled();
    if (contracts != (enabled ? 18u : 4u) || read_targets != (enabled ? 8u : 1u) ||
        !targets.get_config || (enabled && (!targets.loop_tail || !targets.recognition ||
            !targets.score || !targets.get_input_manager || !targets.get_global ||
            !targets.get_sound_manager || !targets.get_group_cursor)))
        return invalid("native_targets");

    // These concrete capabilities must be prepared before any lifecycle hook enables.
    // Input and audio own their ordinary APIs and do not depend on versioning.
    if (!input::PrepareGameplayTransitionTransport(enabled))
        AbortStartup(AbsoluteJudgementFatalPredicate::InputQpcFrequencyInvalidAtStageEntry,
            "stage=transport_prepare qpc_frequency_unavailable=1");
    const auto backend = settings.audio_backend();
    const auto expected_domain = settings.expected_clock_domain();
    if (enabled && backend == audio::AudioBackend::wasapi_exclusive && !expected_domain)
        AbortStartup(AbsoluteJudgementFatalPredicate::AudioBackendUnsupportedForAbsoluteJudgement,
            std::format("stage=capability configured_backend={}", static_cast<std::uint32_t>(backend)));
    if (enabled && settings.input_rate_hz() != 1000)
        AbortStartup(AbsoluteJudgementFatalPredicate::InputTransportRateNot1000,
            std::format("stage=capability configured_rate_hz={}", settings.input_rate_hz()));
    const bool observe_lifecycle = enabled || backend == audio::AudioBackend::asio;
    if (observe_lifecycle && !audio::IsAudioHookCommitted())
        AbortStartup(AbsoluteJudgementFatalPredicate::ExactAudioHookRouteUnavailable,
            "stage=capability audio_hook_committed=0");
    JudgementDiagnostics().SetStartupTargetFps(settings.target_fps());
    InitializeAbsoluteJudgementRuntime(profile->layout, targets, backend, enabled, expected_domain);
    g_layout = profile->layout;
    g_observe_lifecycle = observe_lifecycle;
    return {};
}

void CompleteAbsoluteJudgementStartup(const JudgementSettings& settings) noexcept {
    const bool enabled = settings.enabled();
    if (enabled && (!detail::g_originals.pressed || !detail::g_originals.held ||
        !detail::g_originals.released || !detail::g_originals.direction ||
        !detail::g_originals.held_age || !detail::g_originals.timing_grade))
        AbortStartup(AbsoluteJudgementFatalPredicate::StartupHookTransactionInvalid,
            "stage=publication original_missing=1");
    JudgementDiagnostics().LogStartup({
        .enabled = enabled, .target_fps = settings.target_fps(),
        .input_rate_hz = settings.input_rate_hz(),
        .backend = audio::AudioBackendName(settings.audio_backend()),
        .audio_hook_committed = audio::IsAudioHookCommitted(),
        .installed_site_count = enabled ? 10u : 3u,
        .timing_grade_diagnostic_hook = enabled,
    });
    if (!enabled && settings.target_fps() != 60)
        PLOG_WARNING << "AbsoluteJudgement: stock judgement at non-60 target FPS "
                        "has no FPS-independent judgement guarantee";
}
} // namespace gc::absolute_judgement
