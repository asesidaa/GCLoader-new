#include "Patches/Framerate/FramerateDiagnostics.h"

#include <plog/Log.h>

// ReSharper disable once CppUnusedIncludeDirective
#include <cmath>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdlib>
#include <format>
#include <sstream>
#include <string>

namespace gc::framerate
{
    namespace
    {
        void ProductionLogInfo(const char* text)
        {
            PLOG_INFO << text;
        }

        void ProductionLogWarning(const char* text)
        {
            PLOG_WARNING << text;
        }

        void ProductionLogError(const char* text)
        {
            PLOG_ERROR << text;
        }

        void ProductionShowError(const char* text)
        {
            MessageBoxA(
                nullptr,
                text,
                "GCLoader framerate error",
                MB_OK | MB_ICONERROR);
        }

        void ProductionTerminateProcess(DWORD code)
        {
            TerminateProcess(GetCurrentProcess(), code);
        }

        void ProductionFailFast()
        {
            RaiseFailFastException(nullptr, nullptr, 0);
            std::abort();
        }

        template <typename Action>
        void InvokeNoexcept(Action&& action) noexcept
        {
            try
            {
                action();
            }
            catch (...)
            {
            }
        }

        void PublishFatal(
            const std::string& log,
            std::string_view modal,
            DWORD exit_code,
            std::atomic_bool& publication_latch,
            const FrameratePlatformActions& actions) noexcept
        {
            bool expected = false;
            if (!publication_latch.compare_exchange_strong(expected, true))
            {
                return;
            }

            InvokeNoexcept([&]
            {
                if (actions.log_error != nullptr)
                {
                    actions.log_error(log.c_str());
                }
            });
            const std::string modal_text{modal};
            InvokeNoexcept([&]
            {
                if (actions.show_error != nullptr)
                {
                    actions.show_error(modal_text.c_str());
                }
            });
            InvokeNoexcept([&]
            {
                if (actions.terminate_process != nullptr)
                {
                    actions.terminate_process(exit_code);
                }
            });
            InvokeNoexcept([&]
            {
                if (actions.fail_fast != nullptr)
                {
                    actions.fail_fast();
                }
            });
        }
    } // namespace

    FrameratePlatformActions ProductionFrameratePlatformActions() noexcept
    {
        return {
            ProductionLogInfo,
            ProductionLogWarning,
            ProductionLogError,
            ProductionShowError,
            ProductionTerminateProcess,
            ProductionFailFast,
        };
    }

    void ReportFramerateStartup(
        const FramerateProfile& profile,
        const FramerateStartupPatchSummary& summary,
        const FrameratePlatformActions& actions) noexcept
    {
        InvokeNoexcept([&]
        {
            std::ostringstream stream;
            stream << "FrameratePatch: startup"
                << " target_fps=" << profile.target_fps()
                << " frame_milliseconds=" << profile.frame_milliseconds()
                << " frame_seconds=" << profile.frame_seconds()
                << " mode="
                << (profile.native_timing() ? "native" : "transformed")
                << " authored_clock="
                << (profile.native_timing()
                        ? "native_bypass"
                        : "deterministic_phase")
                << " direct_writes=" << summary.direct_write_count
                << " hooks=" << summary.hook_count
                << " menu_repeat=" << summary.menu_repeat_initial
                << "/" << summary.menu_repeat_interval
                << " authored_frame_ms="
                << summary.authored_frame_milliseconds
                << " news_notice_updates=native"
                << " ifbl_loops=original"
                << " player_decrement=native"
                << " countdown_asset=authored60"
                << " player_duration=dynamic_scaled"
                << " effect_timing="
                << (profile.native_timing()
                        ? "native_bypass"
                        : "producer_boundary")
                << " effect_manifest_rows="
                << summary.effect_timing.timing_sites
                << " effect_registration_sites="
                << summary.effect_timing.registration_sites
                << " effect_duration_queries="
                << summary.effect_timing.duration_queries
                << " effect_hooks="
                << summary.effect_timing.hook_contracts
                << " effect_manager_gated="
                << summary.effect_timing.manager_gated
                << " effect_already_authored="
                << summary.effect_timing.already_authored
                << " effect_reset_or_constant="
                << summary.effect_timing.reset_or_constant
                << " effect_child_inherited="
                << summary.effect_timing.child_inherited
                << " effect_non_ctune_out_of_scope="
                << summary.effect_timing.non_ctune_out_of_scope
                << " two_second_frames=" << profile.two_second_frames()
                << " palette_frame_cap=" << profile.palette_frame_cap()
                << " render_smoothing_step="
                << profile.render_smoothing_step()
                << " render_offset_decay_step="
                << profile.render_offset_decay_step()
                << " gameplay_validated="
                << (profile.gameplay_validated() ? "true" : "false")
                << " warmup_seconds=5"
                << " window_seconds=2"
                << " tolerance_percent=3"
                << " required_streak=3"
                << " built_in_limiter=unpatched"
                << " external_cap_must_equal_target=true";
            if (actions.log_info != nullptr)
            {
                actions.log_info(stream.str().c_str());
            }
        });

        if (!profile.gameplay_validated())
        {
            InvokeNoexcept([&]
            {
                const auto message = std::format(
                    "FrameratePatch: target_fps={} is formula-driven and not "
                    "individually gameplay-validated",
                    profile.target_fps());
                if (actions.log_warning != nullptr)
                {
                    actions.log_warning(message.c_str());
                }
            });
        }
    }

    std::string FormatFramerateEffectRuntimeStats(
        const FramerateEffectRuntimeStats& stats)
    {
        return std::format(
            " effect_flow_item={} effect_tutorial_elapsed={} "
            "effect_chart_preroll={} effect_player_modulo={}",
            stats.flow_item_mappings,
            stats.tutorial_elapsed_mappings,
            stats.chart_preroll_scalings,
            stats.player_modulo_mappings);
    }

    bool ShouldSuggestIntervalModeOne(
        std::uint32_t target_fps,
        double measured_fps) noexcept
    {
        if (target_fps <= 60 || !std::isfinite(measured_fps))
        {
            return false;
        }
        return std::fabs(measured_fps - 60.0) / 60.0 <= 0.03;
    }

    void ReportFramerateMismatch(
        const FramerateObservation& observation,
        std::atomic_bool& publication_latch,
        const FrameratePlatformActions& actions) noexcept
    {
        std::ostringstream log;
        log << "FrameratePatch: external cap validation failed"
            << " target_fps=" << observation.target_fps
            << " measured_fps=" << observation.measured_fps
            << " relative_error=" << observation.relative_error
            << " interval_count=" << observation.interval_count
            << " failed_windows=" << observation.mismatching_streak
            << " storage_overflowed="
            << (observation.storage_overflowed ? "true" : "false");

        std::ostringstream modal;
        modal << "GCLoader measured " << observation.measured_fps
            << " FPS, but target_fps is " << observation.target_fps << ".\n\n"
            << "GCLoader does not apply a frame cap. Configure your driver or RTSS "
            "cap to exactly match target_fps, ensure the system can sustain it, "
            "then restart the game.";
        if (ShouldSuggestIntervalModeOne(
            observation.target_fps, observation.measured_fps))
        {
            modal << "\n\nThe game appears to be held near its built-in 60 FPS limit. "
                "Set IntervalMode = 1, keep the external cap enabled, and restart.";
        }

        PublishFatal(
            log.str(),
            modal.str(),
            ERROR_INVALID_DATA,
            publication_latch,
            actions);
    }

    void ReportFramerateClockFailure(
        std::uint32_t target_fps,
        std::atomic_bool& publication_latch,
        const FrameratePlatformActions& actions) noexcept
    {
        const auto clock_log = std::format(
            "FrameratePatch: QPC cadence clock failed target_fps={}", target_fps);
        constexpr std::string_view clock_modal =
            "GCLoader could not measure the external frame cap because the "
            "high-resolution clock failed. Restart the game; if the error repeats, "
            "keep target_fps at 60 and report the loader log.";
        PublishFatal(
            clock_log,
            clock_modal,
            ERROR_INVALID_DATA,
            publication_latch,
            actions);
    }

    void ReportFramerateRuntimeFailure(
        std::string_view detail,
        std::atomic_bool& publication_latch,
        const FrameratePlatformActions& actions) noexcept
    {
        const auto runtime_log = std::format(
            "FrameratePatch: runtime timing conversion failed detail={}", detail);
        constexpr std::string_view runtime_modal =
            "GCLoader encountered an unsafe runtime framerate conversion and stopped "
            "the game to avoid mixed timing domains. Restart the game and report the "
            "loader log.";
        PublishFatal(
            runtime_log,
            runtime_modal,
            ERROR_INVALID_DATA,
            publication_latch,
            actions);
    }

    void ReportFramerateInitializationFailure(
        std::string_view detail,
        std::atomic_bool& publication_latch,
        const FrameratePlatformActions& actions) noexcept
    {
        const auto initialization_log = std::format(
            "FrameratePatch: initialization failed detail={}", detail);
        constexpr std::string_view initialization_modal =
            "GCLoader could not install the complete framerate patch set and stopped "
            "the game. The loader log contains the failed stage and rollback status.";
        PublishFatal(
            initialization_log,
            initialization_modal,
            ERROR_DLL_INIT_FAILED,
            publication_latch,
            actions);
    }
} // namespace gc::framerate
