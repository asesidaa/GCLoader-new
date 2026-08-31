#pragma once

#include "Audio/Mixer/AudioCursorTimeline.h"
#include "Audio/Mixer/AudioSnapshot.h"
#include "Audio/Wasapi/WasapiAudioTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace gc::audio
{
    namespace detail
    {
        struct AudibleDrainRecord
        {
            std::uint64_t exclusive_end_output_frame{};
            std::uint64_t run_token{};
            std::uint64_t epoch{};
        };

        class AudibleDrainPublication
        {
        public:
            // The render callback is the sole publisher; readers never wait.
            void Publish(const AudibleDrainRecord&) noexcept;
            std::optional<std::uint64_t> Observe(
                std::uint64_t current_draining_run,
                std::uint64_t latest_accepted_epoch) const noexcept;

        private:
            std::atomic_uint64_t sequence_{};
            std::atomic_uint64_t exclusive_end_output_frame_{};
            std::atomic_uint64_t run_token_{};
            std::atomic_uint64_t epoch_{};
        };

        struct VoicePlayTransition
        {
            std::uint64_t run_token{};
            bool needs_active_increment{};
        };

        class VoicePlaybackStateMachine
        {
        public:
            VoicePlayTransition BeginPlay() noexcept;
            void CommitPlay(std::uint64_t run_token) noexcept;
            std::uint64_t BeginStop() noexcept;
            void CompleteStop(std::uint64_t run_token) noexcept;
            bool BeginEnd(std::uint64_t run_token) noexcept;
            void CompleteEnd(std::uint64_t run_token) noexcept;
            std::uint64_t CapturePlayingRun() const noexcept;
            std::uint64_t CaptureDrainingRun() const noexcept;
            bool playing() const noexcept;

        private:
            std::atomic_uint64_t packed_state_{};
        };
    } // namespace detail

    enum class MixerRenderFailureSource : std::uint8_t
    {
        None,
        InvalidArguments,
        ReentrantRender,
        EngineRead,
    };

    enum class MixerExactPublicationStage : std::uint8_t
    {
        None,
        InvalidOutputSpan,
        EpochAheadOfOutput,
        EpochOffsetOverflow,
        SourceMappingFailed,
        TimelineRejected,
        EpochAdvanceOverflow,
    };

    struct MixerDiagnosticsSnapshot
    {
        std::uint64_t native_rate_buffers{};
        std::uint64_t sample_format_converted_buffers{};
        std::uint64_t sample_rate_converted_buffers{};
        std::uint64_t native_gameplay_buffers{};
        std::uint32_t active_voices{};
        std::uint32_t maximum_simultaneous_voices{};
        MixerRenderFailureSource first_render_failure_source{};
        ma_result first_engine_read_error{MA_SUCCESS};
        MixerExactPublicationStage first_exact_publication_stage{};
        ExactMappedSpanPublicationFailure first_exact_timeline_failure{};
        std::uint64_t first_exact_timeline_expected{};
        std::uint64_t first_exact_timeline_actual{};
        std::uint64_t first_exact_buffer_instance_id{};
        std::uint64_t first_exact_playback_generation{};
        std::uint64_t first_exact_output_begin{};
        std::uint64_t first_exact_output_frames{};
        std::uint64_t first_exact_epoch_output_frames{};
        std::uint64_t first_exact_epoch_source_start{};
        std::uint64_t first_exact_source_length_frames{};
        std::uint32_t first_exact_output_rate{};
        std::uint32_t first_exact_source_rate{};
    };

    struct MixerRenderResult
    {
        ma_result result{MA_ERROR};
        std::uint64_t frames_read{};
        std::uint32_t active_voices{};
    };

    struct MixerRenderTimeline
    {
        std::uint64_t output_frame_begin{};
        std::uint64_t discontinuity_frames{};
    };

    enum class VoiceUsage : std::uint8_t
    {
        General,
        GameplayNativeCandidate,
    };

    struct MixerVoiceState;
    struct MiniaudioMixerState;

    class MixerVoice final
    {
    public:
        ~MixerVoice();

        MixerVoice(const MixerVoice&) = delete;
        MixerVoice& operator=(const MixerVoice&) = delete;
        MixerVoice(MixerVoice&&) = delete;
        MixerVoice& operator=(MixerVoice&&) = delete;

        HRESULT Play(bool looping, std::uint64_t epoch) noexcept;
        void Stop() noexcept;
        HRESULT Seek(std::uint64_t source_frame, std::uint64_t epoch) noexcept;
        void SetGain(float gain) noexcept;
        [[nodiscard]] bool playing() const noexcept;
        [[nodiscard]] bool looping() const noexcept;
        [[nodiscard]] bool at_end() const noexcept;
        [[nodiscard]] std::optional<std::uint64_t>
        audible_until_output_frame() const noexcept;

    private:
        friend class MiniaudioMixer;

        explicit MixerVoice(std::unique_ptr<MixerVoiceState>) noexcept;

        std::unique_ptr<MixerVoiceState> state_;
    };

    class MiniaudioMixer final
    {
    public:
        ~MiniaudioMixer();

        MiniaudioMixer(const MiniaudioMixer&) = delete;
        MiniaudioMixer& operator=(const MiniaudioMixer&) = delete;
        MiniaudioMixer(MiniaudioMixer&&) = delete;
        MiniaudioMixer& operator=(MiniaudioMixer&&) = delete;

        static std::unique_ptr<MiniaudioMixer> Create(
            std::uint32_t period_frames,
            std::uint32_t output_sample_rate,
            const ma_allocation_callbacks* callbacks,
            ma_result* result) noexcept;
        static std::unique_ptr<MiniaudioMixer> Create(
            std::uint32_t period_frames,
            std::uint32_t output_sample_rate,
            std::shared_ptr<const ma_allocation_callbacks> callbacks,
            ma_result* result) noexcept;
        std::unique_ptr<MixerVoice> CreateVoice(
            const NormalizedSourceFormat& format,
            std::shared_ptr<AudioSnapshot> snapshot,
            std::shared_ptr<AudioCursorTimeline> timeline,
            VoiceUsage usage,
            ma_result* result) noexcept;
        MixerRenderResult Render(
            std::span<float> stereo,
            const MixerRenderTimeline& timeline) noexcept;
        MixerRenderResult RenderSequential(
            std::span<float> stereo) noexcept;
        [[nodiscard]] MixerDiagnosticsSnapshot diagnostics() const noexcept;

    private:
        MixerRenderResult RenderInternal(
            std::span<float> stereo,
            const MixerRenderTimeline* timeline) noexcept;
        static std::unique_ptr<MiniaudioMixer> CreateWithOwner(
            std::uint32_t period_frames,
            std::uint32_t output_sample_rate,
            const ma_allocation_callbacks* callbacks,
            std::shared_ptr<const ma_allocation_callbacks> callback_owner,
            ma_result* result) noexcept;
        explicit MiniaudioMixer(
            std::shared_ptr<MiniaudioMixerState>) noexcept;

        std::shared_ptr<MiniaudioMixerState> state_;
    };

    void ConvertFloatToPcm16(
        std::span<const float> input,
        std::span<std::int16_t> output) noexcept;
} // namespace gc::audio
