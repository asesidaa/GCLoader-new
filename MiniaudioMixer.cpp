#include "MiniaudioMixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

namespace gc::audio {
namespace {

struct MixerRenderContext {
    std::uint64_t id{};
    std::uint64_t output_frame_begin{};
    std::uint32_t frame_count{};
};

thread_local MixerRenderContext* current_render_context{};

void VoiceNodeProcess(
    ma_node* node,
    const float** frames_in,
    ma_uint32* frame_count_in,
    float** frames_out,
    ma_uint32* frame_count_out);

ma_node_vtable voice_node_vtable{
    VoiceNodeProcess,
    nullptr,
    0,
    1,
    0,
};

struct VoiceNode {
    ma_node_base base{};
    MixerVoiceState* state{};
};

static_assert(offsetof(VoiceNode, base) == 0);

HRESULT ResultToHresult(ma_result result) noexcept {
    if (result == MA_SUCCESS) {
        return DS_OK;
    }
    if (result == MA_OUT_OF_MEMORY) {
        return DSERR_OUTOFMEMORY;
    }
    if (result == MA_INVALID_ARGS || result == MA_OUT_OF_RANGE) {
        return DSERR_INVALIDPARAM;
    }
    return DSERR_GENERIC;
}

std::uint64_t CeilScale(
    std::uint64_t value,
    std::uint32_t numerator,
    std::uint32_t denominator) noexcept {
    if (denominator == 0) {
        return 0;
    }
    return (value * numerator + denominator - 1) / denominator;
}

bool ScaleFloor(
    std::uint64_t value,
    std::uint64_t numerator,
    std::uint64_t denominator,
    std::uint64_t* result) noexcept {
    if (denominator == 0 || result == nullptr) {
        return false;
    }
    const auto quotient = value / denominator;
    const auto remainder = value % denominator;
    if (numerator != 0 && quotient >
        std::numeric_limits<std::uint64_t>::max() / numerator) {
        return false;
    }
    const auto whole = quotient * numerator;
    const auto fractional = (remainder * numerator) / denominator;
    if (fractional >
        std::numeric_limits<std::uint64_t>::max() - whole) {
        return false;
    }
    *result = whole + fractional;
    return true;
}

} // namespace

struct MiniaudioMixerState {
    ma_engine engine{};
    std::uint32_t period_frames{};
    std::atomic_uint64_t render_id{};
    std::atomic_uint64_t native_rate_buffers{};
    std::atomic_uint64_t sample_format_converted_buffers{};
    std::atomic_uint64_t sample_rate_converted_buffers{};
    std::atomic_uint64_t native_gameplay_buffers{};
    std::atomic_uint32_t active_voices{};
    std::atomic_uint32_t maximum_simultaneous_voices{};
    bool initialized{};

    ~MiniaudioMixerState() {
        if (initialized) {
            ma_engine_uninit(&engine);
        }
    }

    void VoiceStarted() noexcept {
        const auto active =
            active_voices.fetch_add(1, std::memory_order_seq_cst) + 1;
        auto maximum = maximum_simultaneous_voices.load(
            std::memory_order_seq_cst);
        while (maximum < active &&
               !maximum_simultaneous_voices.compare_exchange_weak(
                   maximum,
                   active,
                   std::memory_order_seq_cst,
                   std::memory_order_seq_cst)) {
        }
    }

    void VoiceStopped() noexcept {
        active_voices.fetch_sub(1, std::memory_order_seq_cst);
    }
};

namespace {

enum class VoicePlaybackPhase : std::uint8_t {
    Stopped,
    Starting,
    Playing,
    Stopping,
    Ending,
    Ended,
};

constexpr std::uint64_t kPlaybackPhaseBits = 3;
constexpr std::uint64_t kPlaybackPhaseMask =
    (std::uint64_t{1} << kPlaybackPhaseBits) - 1;
constexpr std::uint64_t kMaximumPlaybackRun =
    std::numeric_limits<std::uint64_t>::max() >> kPlaybackPhaseBits;

static_assert(
    static_cast<std::uint64_t>(VoicePlaybackPhase::Ended) <=
    kPlaybackPhaseMask);
static_assert(std::atomic_uint64_t::is_always_lock_free);

constexpr std::uint64_t PackPlaybackState(
    VoicePlaybackPhase phase,
    std::uint64_t run_token) noexcept {
    return (run_token << kPlaybackPhaseBits) |
        static_cast<std::uint64_t>(phase);
}

constexpr VoicePlaybackPhase PlaybackPhase(
    std::uint64_t packed) noexcept {
    return static_cast<VoicePlaybackPhase>(packed & kPlaybackPhaseMask);
}

constexpr std::uint64_t PlaybackRun(std::uint64_t packed) noexcept {
    return packed >> kPlaybackPhaseBits;
}

constexpr std::uint64_t NextPlaybackRun(
    std::uint64_t current) noexcept {
    return current == kMaximumPlaybackRun ? 1 : current + 1;
}

} // namespace

detail::VoicePlayTransition
detail::VoicePlaybackStateMachine::BeginPlay() noexcept {
    for (;;) {
        auto current = packed_state_.load(std::memory_order_seq_cst);
        const auto phase = PlaybackPhase(current);
        if (phase == VoicePlaybackPhase::Playing ||
            phase == VoicePlaybackPhase::Stopped ||
            phase == VoicePlaybackPhase::Ended) {
            const auto run_token = NextPlaybackRun(PlaybackRun(current));
            const auto starting = PackPlaybackState(
                VoicePlaybackPhase::Starting,
                run_token);
            if (packed_state_.compare_exchange_weak(
                    current,
                    starting,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst)) {
                return {
                    run_token,
                    phase != VoicePlaybackPhase::Playing,
                };
            }
            continue;
        }
        std::this_thread::yield();
    }
}

void detail::VoicePlaybackStateMachine::CommitPlay(
    std::uint64_t run_token) noexcept {
    auto expected = PackPlaybackState(
        VoicePlaybackPhase::Starting,
        run_token);
    packed_state_.compare_exchange_strong(
        expected,
        PackPlaybackState(VoicePlaybackPhase::Playing, run_token),
        std::memory_order_seq_cst,
        std::memory_order_seq_cst);
}

std::uint64_t detail::VoicePlaybackStateMachine::BeginStop() noexcept {
    for (;;) {
        auto current = packed_state_.load(std::memory_order_seq_cst);
        const auto phase = PlaybackPhase(current);
        if (phase == VoicePlaybackPhase::Stopped ||
            phase == VoicePlaybackPhase::Ended) {
            return 0;
        }
        if (phase == VoicePlaybackPhase::Playing) {
            const auto run_token = PlaybackRun(current);
            if (packed_state_.compare_exchange_weak(
                    current,
                    PackPlaybackState(
                        VoicePlaybackPhase::Stopping,
                        run_token),
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst)) {
                return run_token;
            }
            continue;
        }
        std::this_thread::yield();
    }
}

void detail::VoicePlaybackStateMachine::CompleteStop(
    std::uint64_t run_token) noexcept {
    auto expected = PackPlaybackState(
        VoicePlaybackPhase::Stopping,
        run_token);
    packed_state_.compare_exchange_strong(
        expected,
        PackPlaybackState(VoicePlaybackPhase::Stopped, run_token),
        std::memory_order_seq_cst,
        std::memory_order_seq_cst);
}

bool detail::VoicePlaybackStateMachine::BeginEnd(
    std::uint64_t run_token) noexcept {
    if (run_token == 0) {
        return false;
    }
    auto expected = PackPlaybackState(
        VoicePlaybackPhase::Playing,
        run_token);
    return packed_state_.compare_exchange_strong(
        expected,
        PackPlaybackState(VoicePlaybackPhase::Ending, run_token),
        std::memory_order_seq_cst,
        std::memory_order_seq_cst);
}

void detail::VoicePlaybackStateMachine::CompleteEnd(
    std::uint64_t run_token) noexcept {
    auto expected = PackPlaybackState(
        VoicePlaybackPhase::Ending,
        run_token);
    packed_state_.compare_exchange_strong(
        expected,
        PackPlaybackState(VoicePlaybackPhase::Ended, run_token),
        std::memory_order_seq_cst,
        std::memory_order_seq_cst);
}

std::uint64_t detail::VoicePlaybackStateMachine::CapturePlayingRun()
    const noexcept {
    const auto current = packed_state_.load(std::memory_order_seq_cst);
    return PlaybackPhase(current) == VoicePlaybackPhase::Playing
        ? PlaybackRun(current)
        : 0;
}

bool detail::VoicePlaybackStateMachine::playing() const noexcept {
    const auto phase = PlaybackPhase(
        packed_state_.load(std::memory_order_seq_cst));
    return phase == VoicePlaybackPhase::Playing ||
        phase == VoicePlaybackPhase::Ending;
}

struct MixerVoiceState {
    struct SeekMailbox {
        std::mutex writer_mutex;
        std::atomic_uint64_t sequence{};
        std::atomic_uint64_t source_frame{};
        std::atomic_uint64_t epoch{1};

        void Publish(
            std::uint64_t frame,
            std::uint64_t new_epoch) noexcept {
            std::lock_guard lock(writer_mutex);
            PublishLocked(frame, new_epoch);
        }

        void PublishForPlay(
            std::uint64_t fallback_frame,
            std::uint64_t new_epoch,
            std::uint64_t applied_sequence) noexcept {
            std::lock_guard lock(writer_mutex);
            const auto current_sequence = sequence.load(
                std::memory_order_seq_cst);
            const auto frame = current_sequence != applied_sequence
                ? source_frame.load(std::memory_order_seq_cst)
                : fallback_frame;
            PublishLocked(frame, new_epoch);
        }

    private:
        void PublishLocked(
            std::uint64_t frame,
            std::uint64_t new_epoch) noexcept {
            const auto stable = sequence.load(std::memory_order_seq_cst);
            const auto writing = (stable & 1U) == 0 ? stable + 1 : stable + 2;
            sequence.store(writing, std::memory_order_seq_cst);
            source_frame.store(frame, std::memory_order_seq_cst);
            epoch.store(new_epoch, std::memory_order_seq_cst);
            sequence.store(writing + 1, std::memory_order_seq_cst);
        }
    } seek_mailbox;

    VoiceNode node{};
    ma_data_converter converter{};
    std::shared_ptr<MiniaudioMixerState> mixer;
    NormalizedSourceFormat format{};
    std::shared_ptr<AudioSnapshot> snapshot;
    std::shared_ptr<AudioCursorTimeline> timeline;
    std::vector<std::byte> input_scratch;
    std::uint64_t input_scratch_frames{};
    std::uint64_t source_length_frames{};
    std::atomic_uint64_t cursor{};
    std::atomic_uint64_t applied_seek_sequence{};
    detail::VoicePlaybackStateMachine playback;
    std::atomic_bool looping{};
    std::atomic_bool ended{};
    std::atomic_uint64_t audible_until_output_frame{};
    std::mutex control_mutex;
    std::uint64_t epoch{1};
    std::uint64_t epoch_source_start{};
    std::uint64_t epoch_output_frames{};
    std::uint64_t last_render_id{};
    std::uint64_t render_output_offset{};
    bool converter_initialized{};
    bool node_initialized{};
    bool node_attached{};

    ~MixerVoiceState() {
        if (node_attached) {
            ma_node_detach_output_bus(&node.base, 0);
        }
        if (node_initialized) {
            ma_node_uninit(
                &node.base,
                &mixer->engine.allocationCallbacks);
        }
        if (converter_initialized) {
            ma_data_converter_uninit(
                &converter,
                &mixer->engine.allocationCallbacks);
        }
    }

    bool ReadStableSeek(
        std::uint64_t* sequence_out,
        std::uint64_t* frame_out,
        std::uint64_t* epoch_out) noexcept {
        const auto before = seek_mailbox.sequence.load(
            std::memory_order_seq_cst);
        if ((before & 1U) != 0) {
            return false;
        }
        const auto frame = seek_mailbox.source_frame.load(
            std::memory_order_seq_cst);
        const auto new_epoch = seek_mailbox.epoch.load(
            std::memory_order_seq_cst);
        const auto after = seek_mailbox.sequence.load(
            std::memory_order_seq_cst);
        if (before != after || (after & 1U) != 0) {
            return false;
        }
        *sequence_out = after;
        *frame_out = frame;
        *epoch_out = new_epoch;
        return true;
    }

    void EndPlayback(
        std::uint64_t run_token,
        std::uint64_t final_output_end) noexcept {
        if (!playback.BeginEnd(run_token)) {
            return;
        }
        audible_until_output_frame.store(
            final_output_end,
            std::memory_order_seq_cst);
        ma_node_set_state(&node.base, ma_node_state_stopped);
        ended.store(true, std::memory_order_seq_cst);
        mixer->VoiceStopped();
        playback.CompleteEnd(run_token);
    }
};

namespace {

void Silence(float* output, std::uint32_t frames) noexcept {
    std::fill_n(output, static_cast<std::size_t>(frames) * kOutputChannels, 0.0F);
}

bool MappedSourceFrame(
    const MixerVoiceState& voice,
    std::uint64_t epoch_output_frame,
    std::uint64_t* source_frame) noexcept {
    std::uint64_t scaled{};
    if (!ScaleFloor(
            epoch_output_frame,
            voice.format.sample_rate,
            kOutputSampleRate,
            &scaled) ||
        scaled > std::numeric_limits<std::uint64_t>::max() -
            voice.epoch_source_start) {
        return false;
    }
    *source_frame = voice.epoch_source_start + scaled;
    return true;
}

bool SegmentMatchesCumulativeMapping(
    const MixerVoiceState& voice,
    std::uint64_t epoch_output_begin,
    std::uint64_t length) noexcept {
    std::uint64_t source_begin{};
    std::uint64_t source_end{};
    if (length == 0 || epoch_output_begin >
            std::numeric_limits<std::uint64_t>::max() - length ||
        !MappedSourceFrame(voice, epoch_output_begin, &source_begin) ||
        !MappedSourceFrame(
            voice,
            epoch_output_begin + length,
            &source_end)) {
        return false;
    }
    const auto source_length = source_end - source_begin;
    for (std::uint64_t offset = 1; offset < length; ++offset) {
        std::uint64_t expected{};
        std::uint64_t interpolated{};
        if (!MappedSourceFrame(
                voice,
                epoch_output_begin + offset,
                &expected) ||
            !ScaleFloor(
                offset,
                source_length,
                length,
                &interpolated) ||
            expected != source_begin + interpolated) {
            return false;
        }
    }
    return true;
}

bool PublishMappedSpans(
    MixerVoiceState& voice,
    std::uint64_t output_begin,
    std::uint64_t output_frames,
    bool loop_wrapped,
    bool source_ended) noexcept {
    const auto divisor = std::gcd<std::uint64_t>(
        voice.format.sample_rate,
        kOutputSampleRate);
    const auto reduced_output_period = kOutputSampleRate / divisor;
    std::uint64_t output_offset{};

    while (output_offset < output_frames) {
        if (voice.epoch_output_frames >
            std::numeric_limits<std::uint64_t>::max() - output_offset) {
            return false;
        }
        const auto cumulative_begin =
            voice.epoch_output_frames + output_offset;
        const auto remaining = output_frames - output_offset;
        const auto phase = cumulative_begin % reduced_output_period;

        std::uint64_t segment_length{};
        if (phase == 0 && remaining >= reduced_output_period) {
            segment_length = remaining -
                remaining % reduced_output_period;
        } else {
            segment_length = std::min<std::uint64_t>(
                remaining,
                phase == 0
                    ? reduced_output_period - 1
                    : reduced_output_period - phase);
            while (segment_length > 1 &&
                   !SegmentMatchesCumulativeMapping(
                       voice,
                       cumulative_begin,
                       segment_length)) {
                --segment_length;
            }
        }

        std::uint64_t source_begin{};
        std::uint64_t source_end{};
        if (segment_length == 0 ||
            !MappedSourceFrame(
                voice,
                cumulative_begin,
                &source_begin) ||
            !MappedSourceFrame(
                voice,
                cumulative_begin + segment_length,
                &source_end)) {
            return false;
        }

        auto* const timeline = voice.timeline.get();
        timeline->Publish({
            output_begin + output_offset,
            output_begin + output_offset + segment_length,
            source_begin,
            source_end,
            voice.epoch,
            loop_wrapped,
            source_ended && segment_length == remaining,
        });
        output_offset += segment_length;
    }

    if (voice.epoch_output_frames >
        std::numeric_limits<std::uint64_t>::max() - output_frames) {
        return false;
    }
    voice.epoch_output_frames += output_frames;
    return true;
}

void VoiceNodeProcess(
    ma_node* node,
    const float** frames_in,
    ma_uint32* frame_count_in,
    float** frames_out,
    ma_uint32* frame_count_out) {
    (void)frames_in;
    (void)frame_count_in;

    auto& voice = *reinterpret_cast<VoiceNode*>(node)->state;
    const auto requested = *frame_count_out;
    auto* output = frames_out[0];
    const auto playback_run = voice.playback.CapturePlayingRun();
    Silence(output, requested);

    if (playback_run == 0) {
        voice.render_output_offset += requested;
        return;
    }

    const auto* render = current_render_context;
    if (render == nullptr || requested > render->frame_count) {
        return;
    }

    if (voice.last_render_id != render->id) {
        voice.last_render_id = render->id;
        voice.render_output_offset = 0;
    }

    std::uint64_t seek_sequence{};
    std::uint64_t seek_frame{};
    std::uint64_t seek_epoch{};
    if (!voice.ReadStableSeek(
            &seek_sequence,
            &seek_frame,
            &seek_epoch)) {
        voice.render_output_offset += requested;
        return;
    }

    const auto applied = voice.applied_seek_sequence.load(
        std::memory_order_seq_cst);
    if (seek_sequence != applied) {
        if (seek_frame >= voice.source_length_frames ||
            ma_data_converter_reset(&voice.converter) != MA_SUCCESS) {
            voice.render_output_offset += requested;
            return;
        }
        voice.cursor.store(seek_frame, std::memory_order_seq_cst);
        voice.epoch_source_start = seek_frame;
        voice.epoch_output_frames = 0;
        voice.epoch = seek_epoch;
        voice.ended.store(false, std::memory_order_seq_cst);
        voice.last_render_id = render->id;
        voice.render_output_offset = 0;
        voice.applied_seek_sequence.store(
            seek_sequence,
            std::memory_order_seq_cst);
    }

    if (voice.ended.load(std::memory_order_seq_cst)) {
        voice.render_output_offset += requested;
        return;
    }

    std::uint64_t required_input{};
    if (ma_data_converter_get_required_input_frame_count(
            &voice.converter,
            requested,
            &required_input) != MA_SUCCESS ||
        required_input > voice.input_scratch_frames) {
        voice.render_output_offset += requested;
        return;
    }

    const auto source_begin = voice.cursor.load(std::memory_order_seq_cst);
    const bool is_looping = voice.looping.load(std::memory_order_seq_cst);
    std::uint64_t copied{};
    auto position = source_begin;
    bool copied_wrap{};

    {
        auto* const snapshot = voice.snapshot.get();
        const auto view = snapshot->AcquireForRender();
        if (view.size() < snapshot->byte_length()) {
            voice.render_output_offset += requested;
            return;
        }

        while (copied < required_input) {
            if (position == voice.source_length_frames) {
                if (!is_looping) {
                    break;
                }
                position = 0;
                copied_wrap = true;
            }

            const auto chunk = std::min<std::uint64_t>(
                voice.source_length_frames - position,
                required_input - copied);
            std::memcpy(
                voice.input_scratch.data() +
                    copied * voice.format.block_align,
                view.bytes().data() +
                    position * voice.format.block_align,
                static_cast<std::size_t>(
                    chunk * voice.format.block_align));
            position += chunk;
            copied += chunk;
        }
    }

    auto consumed = copied;
    std::uint64_t produced = requested;
    const auto conversion_result = ma_data_converter_process_pcm_frames(
        &voice.converter,
        voice.input_scratch.data(),
        &consumed,
        output,
        &produced);
    if (conversion_result != MA_SUCCESS) {
        Silence(output, requested);
        voice.render_output_offset += requested;
        return;
    }
    if (produced < requested) {
        Silence(
            output + produced * kOutputChannels,
            static_cast<std::uint32_t>(requested - produced));
    }

    bool loop_wrapped{};
    std::uint64_t new_position{};
    if (is_looping) {
        const auto total = source_begin + consumed;
        loop_wrapped = copied_wrap || total >= voice.source_length_frames;
        new_position = total % voice.source_length_frames;
    } else {
        new_position = std::min<std::uint64_t>(
            source_begin + consumed,
            voice.source_length_frames);
    }
    voice.cursor.store(new_position, std::memory_order_seq_cst);

    const bool source_ended =
        !is_looping && new_position == voice.source_length_frames;
    const auto remaining_output = render->frame_count -
        std::min<std::uint64_t>(
            voice.render_output_offset,
            render->frame_count);
    const auto represented_output = std::min<std::uint64_t>(
        produced,
        CeilScale(consumed, kOutputSampleRate, voice.format.sample_rate));
    const auto published_output = std::min<std::uint64_t>(
        represented_output,
        remaining_output);
    std::uint64_t final_output_end{};
    if (published_output != 0) {
        const auto output_begin = render->output_frame_begin +
            voice.render_output_offset;
        const auto published = PublishMappedSpans(
            voice,
            output_begin,
            published_output,
            loop_wrapped,
            source_ended);
        if (published && source_ended) {
            final_output_end = output_begin + published_output;
        }
    }

    voice.render_output_offset += requested;
    if (source_ended) {
        voice.EndPlayback(playback_run, final_output_end);
    }
}

} // namespace

MixerVoice::MixerVoice(
    std::unique_ptr<MixerVoiceState> state) noexcept
    : state_(std::move(state)) {}

MixerVoice::~MixerVoice() {
    Stop();
}

HRESULT MixerVoice::Play(
    bool should_loop,
    std::uint64_t epoch) noexcept {
    if (state_ == nullptr) {
        return DSERR_UNINITIALIZED;
    }

    std::lock_guard control_lock(state_->control_mutex);
    ma_node_set_state(&state_->node.base, ma_node_state_stopped);
    state_->audible_until_output_frame.store(
        0,
        std::memory_order_seq_cst);

    const auto play = state_->playback.BeginPlay();

    state_->looping.store(should_loop, std::memory_order_seq_cst);
    const auto cursor = state_->cursor.load(std::memory_order_seq_cst);
    const auto fallback =
        state_->ended.load(std::memory_order_seq_cst) ||
            cursor >= state_->source_length_frames
        ? 0
        : cursor;
    state_->seek_mailbox.PublishForPlay(
        fallback,
        epoch,
        state_->applied_seek_sequence.load(std::memory_order_seq_cst));
    if (play.needs_active_increment) {
        state_->mixer->VoiceStarted();
    }
    state_->playback.CommitPlay(play.run_token);

    const auto result = ma_node_set_state(
        &state_->node.base,
        ma_node_state_started);
    if (result != MA_SUCCESS) {
        const auto stopped_run = state_->playback.BeginStop();
        if (stopped_run != 0) {
            state_->mixer->VoiceStopped();
            state_->playback.CompleteStop(stopped_run);
        }
        return ResultToHresult(result);
    }
    return DS_OK;
}

void MixerVoice::Stop() noexcept {
    if (state_ == nullptr) {
        return;
    }
    std::lock_guard control_lock(state_->control_mutex);
    ma_node_set_state(&state_->node.base, ma_node_state_stopped);
    const auto stopped_run = state_->playback.BeginStop();
    if (stopped_run != 0) {
        state_->mixer->VoiceStopped();
        state_->playback.CompleteStop(stopped_run);
    }
    state_->audible_until_output_frame.store(
        0,
        std::memory_order_seq_cst);
}

HRESULT MixerVoice::Seek(
    std::uint64_t source_frame,
    std::uint64_t epoch) noexcept {
    if (state_ == nullptr) {
        return DSERR_UNINITIALIZED;
    }
    if (source_frame >= state_->source_length_frames) {
        return DSERR_INVALIDPARAM;
    }
    state_->seek_mailbox.Publish(source_frame, epoch);
    state_->audible_until_output_frame.store(
        0,
        std::memory_order_seq_cst);
    return DS_OK;
}

void MixerVoice::SetGain(float gain) noexcept {
    if (state_ != nullptr) {
        ma_node_set_output_bus_volume(&state_->node.base, 0, gain);
    }
}

bool MixerVoice::playing() const noexcept {
    return state_ != nullptr &&
        state_->playback.playing();
}

bool MixerVoice::looping() const noexcept {
    return state_ != nullptr &&
        state_->looping.load(std::memory_order_seq_cst);
}

bool MixerVoice::at_end() const noexcept {
    return state_ != nullptr &&
        state_->ended.load(std::memory_order_seq_cst);
}

std::optional<std::uint64_t>
MixerVoice::audible_until_output_frame() const noexcept {
    if (state_ == nullptr) {
        return std::nullopt;
    }
    const auto output_end = state_->audible_until_output_frame.load(
        std::memory_order_seq_cst);
    return output_end == 0
        ? std::nullopt
        : std::optional<std::uint64_t>(output_end);
}

MiniaudioMixer::MiniaudioMixer(
    std::shared_ptr<MiniaudioMixerState> state) noexcept
    : state_(std::move(state)) {}

MiniaudioMixer::~MiniaudioMixer() = default;

std::unique_ptr<MiniaudioMixer> MiniaudioMixer::Create(
    std::uint32_t period_frames,
    const ma_allocation_callbacks* callbacks,
    ma_result* result) noexcept {
    if (result != nullptr) {
        *result = MA_INVALID_ARGS;
    }
    if (period_frames == 0) {
        return nullptr;
    }

    try {
        auto state = std::make_shared<MiniaudioMixerState>();
        state->period_frames = period_frames;

        auto config = ma_engine_config_init();
        config.noDevice = MA_TRUE;
        config.channels = kOutputChannels;
        config.sampleRate = kOutputSampleRate;
        config.periodSizeInFrames = period_frames;
        config.defaultVolumeSmoothTimeInPCMFrames = 0;
        config.monoExpansionMode = ma_mono_expansion_mode_duplicate;
        if (callbacks != nullptr) {
            config.allocationCallbacks = *callbacks;
        }

        const auto init_result = ma_engine_init(&config, &state->engine);
        if (init_result != MA_SUCCESS) {
            if (result != nullptr) {
                *result = init_result;
            }
            return nullptr;
        }
        state->initialized = true;

        auto mixer = std::unique_ptr<MiniaudioMixer>(
            new MiniaudioMixer(std::move(state)));
        if (result != nullptr) {
            *result = MA_SUCCESS;
        }
        return mixer;
    } catch (const std::bad_alloc&) {
        if (result != nullptr) {
            *result = MA_OUT_OF_MEMORY;
        }
        return nullptr;
    }
}

std::unique_ptr<MixerVoice> MiniaudioMixer::CreateVoice(
    const NormalizedSourceFormat& format,
    std::shared_ptr<AudioSnapshot> snapshot,
    std::shared_ptr<AudioCursorTimeline> timeline,
    VoiceUsage usage,
    ma_result* result) noexcept {
    if (result != nullptr) {
        *result = MA_INVALID_ARGS;
    }
    if (state_ == nullptr || snapshot == nullptr || timeline == nullptr ||
        format.block_align == 0 || snapshot->byte_length() == 0 ||
        snapshot->byte_length() % format.block_align != 0) {
        return nullptr;
    }

    try {
        auto voice_state = std::make_unique<MixerVoiceState>();
        voice_state->mixer = state_;
        voice_state->format = format;
        voice_state->snapshot = std::move(snapshot);
        voice_state->timeline = std::move(timeline);
        voice_state->source_length_frames =
            voice_state->snapshot->byte_length() / format.block_align;
        voice_state->node.state = voice_state.get();

        auto converter_config = ma_data_converter_config_init(
            format.miniaudio_format,
            ma_format_f32,
            format.channels,
            kOutputChannels,
            format.sample_rate,
            kOutputSampleRate);
        converter_config.resampling.algorithm = ma_resample_algorithm_linear;
        converter_config.resampling.linear.lpfOrder = 0;
        auto init_result = ma_data_converter_init(
            &converter_config,
            &state_->engine.allocationCallbacks,
            &voice_state->converter);
        if (init_result != MA_SUCCESS) {
            if (result != nullptr) {
                *result = init_result;
            }
            return nullptr;
        }
        voice_state->converter_initialized = true;

        init_result = ma_data_converter_get_required_input_frame_count(
            &voice_state->converter,
            state_->period_frames,
            &voice_state->input_scratch_frames);
        if (init_result == MA_SUCCESS) {
            voice_state->input_scratch_frames +=
                ma_data_converter_get_input_latency(
                    &voice_state->converter);
        }
        if (init_result != MA_SUCCESS ||
            voice_state->input_scratch_frames == 0 ||
            voice_state->input_scratch_frames >
                std::numeric_limits<std::size_t>::max() /
                    format.block_align) {
            if (result != nullptr) {
                *result = init_result == MA_SUCCESS
                    ? MA_TOO_BIG
                    : init_result;
            }
            return nullptr;
        }
        voice_state->input_scratch.resize(
            static_cast<std::size_t>(
                voice_state->input_scratch_frames * format.block_align));

        auto node_config = ma_node_config_init();
        node_config.vtable = &voice_node_vtable;
        node_config.initialState = ma_node_state_stopped;
        const ma_uint32 output_channels = kOutputChannels;
        node_config.pOutputChannels = &output_channels;
        init_result = ma_node_init(
            ma_engine_get_node_graph(&state_->engine),
            &node_config,
            &state_->engine.allocationCallbacks,
            &voice_state->node.base);
        if (init_result != MA_SUCCESS) {
            if (result != nullptr) {
                *result = init_result;
            }
            return nullptr;
        }
        voice_state->node_initialized = true;

        init_result = ma_node_attach_output_bus(
            &voice_state->node.base,
            0,
            ma_node_graph_get_endpoint(
                ma_engine_get_node_graph(&state_->engine)),
            0);
        if (init_result != MA_SUCCESS) {
            if (result != nullptr) {
                *result = init_result;
            }
            return nullptr;
        }
        voice_state->node_attached = true;

        state_->native_rate_buffers.fetch_add(
            format.sample_rate == kOutputSampleRate ? 1 : 0,
            std::memory_order_seq_cst);
        state_->sample_format_converted_buffers.fetch_add(
            format.sample_format_converted ? 1 : 0,
            std::memory_order_seq_cst);
        state_->sample_rate_converted_buffers.fetch_add(
            format.sample_rate_converted ? 1 : 0,
            std::memory_order_seq_cst);
        state_->native_gameplay_buffers.fetch_add(
            usage == VoiceUsage::GameplayNativeCandidate &&
                    format.native_rate_pcm16
                ? 1
                : 0,
            std::memory_order_seq_cst);

        auto voice = std::unique_ptr<MixerVoice>(
            new MixerVoice(std::move(voice_state)));
        if (result != nullptr) {
            *result = MA_SUCCESS;
        }
        return voice;
    } catch (const std::bad_alloc&) {
        if (result != nullptr) {
            *result = MA_OUT_OF_MEMORY;
        }
        return nullptr;
    }
}

MixerRenderResult MiniaudioMixer::Render(
    std::span<float> stereo,
    std::uint64_t output_frame_begin) noexcept {
    if (state_ == nullptr ||
        stereo.size() !=
            static_cast<std::size_t>(state_->period_frames) *
                kOutputChannels) {
        return {MA_INVALID_ARGS, 0};
    }
    if (current_render_context != nullptr) {
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        return {MA_INVALID_OPERATION, 0};
    }

    MixerRenderContext context{
        state_->render_id.fetch_add(1, std::memory_order_relaxed) + 1,
        output_frame_begin,
        state_->period_frames,
    };
    current_render_context = &context;
    ma_uint64 frames_read{};
    const auto result = ma_engine_read_pcm_frames(
        &state_->engine,
        stereo.data(),
        state_->period_frames,
        &frames_read);
    current_render_context = nullptr;

    if (frames_read < state_->period_frames) {
        std::fill(
            stereo.begin() + static_cast<std::ptrdiff_t>(
                frames_read * kOutputChannels),
            stereo.end(),
            0.0F);
    }
    return {result, frames_read};
}

MixerDiagnosticsSnapshot MiniaudioMixer::diagnostics() const noexcept {
    if (state_ == nullptr) {
        return {};
    }
    return {
        state_->native_rate_buffers.load(std::memory_order_seq_cst),
        state_->sample_format_converted_buffers.load(
            std::memory_order_seq_cst),
        state_->sample_rate_converted_buffers.load(
            std::memory_order_seq_cst),
        state_->native_gameplay_buffers.load(std::memory_order_seq_cst),
        state_->active_voices.load(std::memory_order_seq_cst),
        state_->maximum_simultaneous_voices.load(
            std::memory_order_seq_cst),
    };
}

void ConvertFloatToPcm16(
    std::span<const float> input,
    std::span<std::int16_t> output) noexcept {
    const auto count = std::min(input.size(), output.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto sample = std::clamp(input[index], -1.0F, 1.0F);
        output[index] = sample <= -1.0F
            ? static_cast<std::int16_t>(-32768)
            : static_cast<std::int16_t>(
                  std::lround(sample * 32767.0F));
    }
}

} // namespace gc::audio
