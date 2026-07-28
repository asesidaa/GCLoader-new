#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/DirectSound/GameplayAudioCursorObservation.h"

#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::audio::AudioCursorTimeline;
using gc::audio::AudioCursorResolutionKind;
using gc::audio::AudioSnapshot;
using gc::audio::GameplayAudioCursorState;
using gc::audio::IAudioEngineServices;
using gc::audio::MiniaudioMixer;
using gc::audio::MixerDiagnosticsSnapshot;
using gc::audio::MixerRenderResult;
using gc::audio::MixerVoice;
using gc::audio::NormalizedSourceFormat;
using gc::audio::SecondarySoundBuffer;
using gc::audio::ScopedGameplayAudioCursorQuery;
using gc::audio::VoiceUsage;
using gc::audio::kGamePrimarySampleRate;

constexpr DWORD kStaticFlags =
    DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
    DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;
constexpr DWORD kStreamingFlags =
    kStaticFlags | DSBCAPS_CTRLPOSITIONNOTIFY;

static_assert(kStaticFlags == 0x50082);
static_assert(kStreamingFlags == 0x50182);

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

WAVEFORMATEX PcmFormat(
    DWORD sample_rate = 44100,
    WORD channels = 2,
    WORD bits = 16) {
    const auto block_align = static_cast<WORD>(channels * (bits / 8));
    return {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = channels,
        .nSamplesPerSec = sample_rate,
        .nAvgBytesPerSec = sample_rate * block_align,
        .nBlockAlign = block_align,
        .wBitsPerSample = bits,
        .cbSize = 0,
    };
}

WAVEFORMATEXTENSIBLE ExtensibleFormat() {
    WAVEFORMATEXTENSIBLE format{};
    format.Format = PcmFormat(48000, 2, 24);
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.cbSize = 22;
    format.Samples.wValidBitsPerSample = 24;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return format;
}

DSBUFFERDESC BufferDescription(
    WAVEFORMATEX* format,
    DWORD byte_count = 32,
    DWORD flags = kStaticFlags) {
    return {
        .dwSize = sizeof(DSBUFFERDESC),
        .dwFlags = flags,
        .dwBufferBytes = byte_count,
        .dwReserved = 0,
        .lpwfxFormat = format,
        .guid3DAlgorithm = GUID_NULL,
    };
}

class MixerEngineServices final : public IAudioEngineServices {
public:
    explicit MixerEngineServices(
        std::uint32_t output_sample_rate = kGamePrimarySampleRate,
        std::uint32_t endpoint_frames = 4)
        : output_sample_rate_(output_sample_rate),
          endpoint_frames_(endpoint_frames) {
        ma_result result = MA_ERROR;
        mixer_ = MiniaudioMixer::Create(
            endpoint_frames_, output_sample_rate_, nullptr, &result);
        initialized = result == MA_SUCCESS && mixer_ != nullptr;
    }

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline> timeline,
        VoiceUsage usage,
        ma_result* result) noexcept override {
        observed_snapshot = snapshot;
        observed_timeline = timeline;
        last_usage = usage;
        if (fail_voice_creation) {
            if (result != nullptr) {
                *result = MA_OUT_OF_MEMORY;
            }
            return nullptr;
        }
        return mixer_->CreateVoice(
            format,
            std::move(snapshot),
            std::move(timeline),
            usage,
            result);
    }

    std::optional<std::uint64_t> CurrentOutputFrame() noexcept override {
        std::unique_lock lock(output_frame_read_mutex_);
        if (block_next_output_frame_read_) {
            output_frame_read_entered_ = true;
            output_frame_read_condition_.notify_all();
            output_frame_read_condition_.wait(lock, [this] {
                return release_output_frame_read_;
            });
            block_next_output_frame_read_ = false;
        }
        return output_frame;
    }

    std::uint32_t endpoint_buffer_frames() const noexcept override {
        return endpoint_frames_;
    }

    std::uint32_t output_sample_rate() const noexcept override {
        return output_sample_rate_;
    }

    void CountPendingCursorQuery() noexcept override {
        ++pending_cursor_queries;
    }

    void CountUnmappedCursorFailure() noexcept override {
        ++unmapped_cursor_failures;
    }

    MixerRenderResult Render(
        std::uint64_t output_frame_begin,
        std::span<float> output) noexcept {
        return mixer_->Render(
            output,
            gc::audio::MixerRenderTimeline{output_frame_begin, 0});
    }

    MixerDiagnosticsSnapshot diagnostics() const noexcept {
        return mixer_->diagnostics();
    }

    void BlockNextOutputFrameRead() {
        std::lock_guard lock(output_frame_read_mutex_);
        block_next_output_frame_read_ = true;
        output_frame_read_entered_ = false;
        release_output_frame_read_ = false;
    }

    bool WaitForBlockedOutputFrameRead() {
        std::unique_lock lock(output_frame_read_mutex_);
        return output_frame_read_condition_.wait_for(
            lock,
            std::chrono::seconds(2),
            [this] { return output_frame_read_entered_; });
    }

    void ReleaseOutputFrameRead() {
        std::lock_guard lock(output_frame_read_mutex_);
        release_output_frame_read_ = true;
        output_frame_read_condition_.notify_all();
    }

    bool initialized{};
    bool fail_voice_creation{};
    std::optional<std::uint64_t> output_frame{};
    std::uint64_t pending_cursor_queries{};
    std::uint64_t unmapped_cursor_failures{};
    VoiceUsage last_usage{VoiceUsage::General};
    std::weak_ptr<AudioSnapshot> observed_snapshot;
    std::weak_ptr<AudioCursorTimeline> observed_timeline;

private:
    std::uint32_t output_sample_rate_{};
    std::uint32_t endpoint_frames_{};
    std::unique_ptr<MiniaudioMixer> mixer_;
    std::mutex output_frame_read_mutex_;
    std::condition_variable output_frame_read_condition_;
    bool block_next_output_frame_read_{};
    bool output_frame_read_entered_{};
    bool release_output_frame_read_{};
};

HRESULT CreateBuffer(
    MixerEngineServices& engine,
    DSBUFFERDESC& descriptor,
    IDirectSoundBuffer8** result) {
    *result = nullptr;
    SecondarySoundBuffer* concrete{};
    const auto hr = SecondarySoundBuffer::Create(
        engine,
        descriptor,
        &concrete);
    if (SUCCEEDED(hr)) {
        *result = static_cast<IDirectSoundBuffer8*>(concrete);
    }
    return hr;
}

HRESULT FillBuffer(
    IDirectSoundBuffer8* buffer,
    std::span<const std::int16_t> samples) {
    void* first{};
    void* second{};
    DWORD first_bytes{};
    DWORD second_bytes{};
    auto hr = buffer->Lock(
        0,
        static_cast<DWORD>(samples.size_bytes()),
        &first,
        &first_bytes,
        &second,
        &second_bytes,
        0);
    if (FAILED(hr)) {
        return hr;
    }
    std::memcpy(first, samples.data(), first_bytes);
    if (second_bytes != 0) {
        std::memcpy(
            second,
            reinterpret_cast<const std::byte*>(samples.data()) + first_bytes,
            second_bytes);
    }
    return buffer->Unlock(first, first_bytes, second, second_bytes);
}

int TestConstructionAndValidation() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    int failures = Expect(engine.initialized, "real four-frame mixer creation");

    for (const auto flags : {kStaticFlags, kStreamingFlags}) {
        auto descriptor = BufferDescription(&wave, 32, flags);
        IDirectSoundBuffer8* buffer{};
        failures += Expect(
            CreateBuffer(engine, descriptor, &buffer) == DS_OK &&
                buffer != nullptr,
            "observed secondary descriptor creation");
        failures += Expect(
            engine.last_usage == VoiceUsage::GameplayNativeCandidate,
            "observed native descriptor gameplay-candidate classification");
        if (buffer != nullptr) {
            failures += Expect(buffer->Release() == 0, "created buffer release");
        }
    }

    auto exceptional_wave = PcmFormat(22050);
    auto exceptional = BufferDescription(&exceptional_wave);
    IDirectSoundBuffer8* exceptional_buffer{};
    failures += Expect(
        CreateBuffer(engine, exceptional, &exceptional_buffer) == DS_OK &&
            engine.last_usage == VoiceUsage::General,
        "resampled source general-use classification");
    if (exceptional_buffer != nullptr) {
        exceptional_buffer->Release();
    }

    auto descriptor = BufferDescription(&wave);
    SecondarySoundBuffer* concrete = reinterpret_cast<SecondarySoundBuffer*>(1);
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, nullptr) ==
            DSERR_INVALIDPARAM,
        "null factory result rejection");
    descriptor.dwSize = sizeof(DSBUFFERDESC) - 1;
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_INVALIDPARAM && concrete == nullptr,
        "malformed descriptor rejection");
    descriptor = BufferDescription(nullptr);
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_INVALIDPARAM && concrete == nullptr,
        "null source format rejection");
    descriptor = BufferDescription(&wave, 0);
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_INVALIDPARAM,
        "zero byte buffer rejection");
    descriptor = BufferDescription(&wave, 18);
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_INVALIDPARAM,
        "unaligned byte buffer rejection");
    descriptor = BufferDescription(&wave, 32, DSBCAPS_PRIMARYBUFFER);
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_INVALIDPARAM,
        "primary descriptor rejection by secondary factory");
    descriptor = BufferDescription(&wave);
    descriptor.dwReserved = 1;
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_INVALIDPARAM && concrete == nullptr,
        "secondary descriptor reserved field rejection");
    descriptor = BufferDescription(&wave);
    descriptor.guid3DAlgorithm = IID_IUnknown;
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_INVALIDPARAM && concrete == nullptr,
        "secondary descriptor 3D algorithm rejection");
    descriptor = BufferDescription(&wave, 32, kStaticFlags | DSBCAPS_CTRL3D);
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_CONTROLUNAVAIL,
        "unsupported 3D capability rejection");

    descriptor = BufferDescription(&wave);
    engine.fail_voice_creation = true;
    failures += Expect(
        SecondarySoundBuffer::Create(engine, descriptor, &concrete) ==
            DSERR_OUTOFMEMORY && concrete == nullptr,
        "voice initialization failure mapping without half object");
    return failures;
}

int TestComIdentityAndReferenceCounts() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "COM identity buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    failures += Expect(buffer->AddRef() == 2, "AddRef returns two");
    failures += Expect(buffer->Release() == 1, "Release returns one");

    for (const auto* iid : {&IID_IUnknown, &IID_IDirectSoundBuffer,
                            &IID_IDirectSoundBuffer8}) {
        void* queried{};
        failures += Expect(
            buffer->QueryInterface(*iid, &queried) == S_OK &&
                queried == static_cast<void*>(buffer),
            "supported QueryInterface preserves identity");
        if (queried != nullptr) {
            failures += Expect(
                static_cast<IUnknown*>(queried)->Release() == 1,
                "QueryInterface reference released to one");
        }
    }

    void* unsupported = reinterpret_cast<void*>(1);
    failures += Expect(
        buffer->QueryInterface(IID_IDirectSoundNotify, &unsupported) ==
            E_NOINTERFACE && unsupported == nullptr,
        "notification interface remains unsupported");
    failures += Expect(
        buffer->QueryInterface(IID_IDirectSound3DBuffer, &unsupported) ==
            E_NOINTERFACE && unsupported == nullptr,
        "3D interface remains unsupported");
    failures += Expect(
        buffer->QueryInterface(IID_IUnknown, nullptr) == E_POINTER,
        "null QueryInterface output rejected");
    failures += Expect(buffer->Release() == 0, "final COM Release returns zero");
    return failures;
}

int TestCapsAndFormats() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 32, kStreamingFlags);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "caps and PCM format buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    DSBCAPS caps{.dwSize = sizeof(DSBCAPS)};
    failures += Expect(
        buffer->GetCaps(&caps) == DS_OK &&
            caps.dwFlags == descriptor.dwFlags &&
            caps.dwBufferBytes == descriptor.dwBufferBytes,
        "GetCaps preserves flags and game-facing bytes");
    failures += Expect(
        buffer->GetCaps(nullptr) == DSERR_INVALIDPARAM,
        "GetCaps null rejection");

    DWORD required{};
    failures += Expect(
        buffer->GetFormat(nullptr, 0, &required) == DS_OK &&
            required == sizeof(WAVEFORMATEX),
        "GetFormat PCM size query");
    WAVEFORMATEX copied{};
    DWORD written{};
    failures += Expect(
        buffer->GetFormat(&copied, sizeof(copied), &written) == DS_OK &&
            written == sizeof(WAVEFORMATEX) &&
            std::memcmp(&copied, &wave, sizeof(wave)) == 0,
        "GetFormat preserves PCM source bytes");
    failures += Expect(
        buffer->GetFormat(&copied, sizeof(copied) - 1, &written) ==
            DSERR_INVALIDPARAM,
        "GetFormat rejects undersized destination");
    buffer->Release();

    auto extended = ExtensibleFormat();
    descriptor = BufferDescription(
        reinterpret_cast<WAVEFORMATEX*>(&extended),
        48);
    buffer = nullptr;
    failures += Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "extensible format buffer creation");
    WAVEFORMATEXTENSIBLE extended_copy{};
    written = 0;
    if (buffer != nullptr) {
        failures += Expect(
            buffer->GetFormat(
                reinterpret_cast<WAVEFORMATEX*>(&extended_copy),
                sizeof(extended_copy),
                &written) == DS_OK &&
                written == sizeof(WAVEFORMATEXTENSIBLE) &&
                std::memcmp(&extended_copy, &extended, sizeof(extended)) == 0,
            "GetFormat preserves extensible source bytes");
        buffer->Release();
    }
    return failures;
}

int TestLockUnlockPublication() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 16);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "lock buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    void* first{};
    void* second{};
    DWORD first_bytes{};
    DWORD second_bytes{};
    failures += Expect(
        buffer->Lock(
            2, 4, &first, &first_bytes, &second, &second_bytes, 0) ==
            DSERR_INVALIDPARAM,
        "unaligned COM Lock surfaces snapshot error");
    failures += Expect(
        buffer->Lock(
            12, 8, &first, &first_bytes, &second, &second_bytes, 0) == DS_OK &&
            first_bytes == 4 && second_bytes == 4 &&
            first != nullptr && second != nullptr,
        "wrapped COM Lock returns four plus four bytes");
    std::memset(first, 1, first_bytes);
    std::memset(second, 1, second_bytes);
    const auto locked_first = first;
    const auto locked_second = second;
    const auto locked_first_bytes = first_bytes;
    const auto locked_second_bytes = second_bytes;
    void* rejected_first{};
    void* rejected_second{};
    DWORD rejected_first_bytes{};
    DWORD rejected_second_bytes{};
    failures += Expect(
        buffer->Lock(
            0,
            4,
            &rejected_first,
            &rejected_first_bytes,
            &rejected_second,
            &rejected_second_bytes,
            0) ==
            DSERR_ALLOCATED,
        "second COM Lock surfaces outstanding-lock error");
    failures += Expect(
        buffer->Unlock(
            locked_first,
            locked_first_bytes + 4,
            locked_second,
            locked_second_bytes) ==
            DSERR_INVALIDPARAM,
        "mismatched COM Unlock surfaces snapshot error");
    failures += Expect(
        buffer->Unlock(
            locked_first,
            locked_first_bytes,
            locked_second,
            locked_second_bytes) == DS_OK,
        "matching COM Unlock publishes snapshot");

    failures += Expect(buffer->Play(0, 0, 0) == DS_OK, "published buffer play");
    std::array<float, 8> output{};
    const auto rendered = engine.Render(0, output);
    failures += Expect(
        rendered.result == MA_SUCCESS &&
            std::any_of(output.begin(), output.end(), [](float value) {
                return value != 0.0F;
            }),
        "published wrapped bytes reach real mixer output");
    buffer->Release();
    return failures;
}

int TestVolumePlaybackAndCursors() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 32);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "playback buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    LONG volume{};
    failures += Expect(
        buffer->SetVolume(-600) == DS_OK &&
            buffer->GetVolume(&volume) == DS_OK && volume == -600,
        "DirectSound volume round trip");
    failures += Expect(
        buffer->SetVolume(1) == DSERR_INVALIDPARAM &&
            buffer->SetVolume(-10001) == DSERR_INVALIDPARAM,
        "out-of-range DirectSound volume rejection");
    failures += Expect(
        buffer->GetVolume(nullptr) == DSERR_INVALIDPARAM,
        "null volume output rejection");

    const std::array<std::int16_t, 16> samples{
        1000, 1000, 2000, 2000, 3000, 3000, 4000, 4000,
        5000, 5000, 6000, 6000, 7000, 7000, 8000, 8000,
    };
    failures += Expect(FillBuffer(buffer, samples) == DS_OK, "PCM publication");
    failures += Expect(
        buffer->Play(1, 0, 0) == DSERR_INVALIDPARAM &&
            buffer->Play(0, 1, 0) == DSERR_INVALIDPARAM &&
            buffer->Play(0, 0, 2) == DSERR_INVALIDPARAM,
        "Play reserved and unsupported flag rejection");
    failures += Expect(
        buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK,
        "looping Play succeeds");
    engine.output_frame = 100;
    DWORD play_cursor{};
    DWORD write_cursor{};
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
            play_cursor == 0 && write_cursor == 16 &&
            engine.pending_cursor_queries == 1 &&
            engine.unmapped_cursor_failures == 0,
        "new play generation immediately returns its stable anchor");
    DWORD status{};
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK &&
            status == (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING),
        "looping Play status bits");

    std::array<float, 8> first_output{};
    failures += Expect(
        engine.Render(100, first_output).result == MA_SUCCESS,
        "first real mixer render");
    engine.output_frame = 102;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
            play_cursor == 8 && write_cursor == 24,
        "endpoint-clock play cursor and one-period write cursor");

    failures += Expect(
        buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK,
        "repeated Play succeeds");
    engine.output_frame = 104;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, nullptr) == DS_OK &&
            play_cursor == 8 && engine.pending_cursor_queries == 2 &&
            engine.unmapped_cursor_failures == 0,
        "repeated Play creates a distinct pending generation");
    std::array<float, 8> second_output{};
    failures += Expect(
        engine.Render(104, second_output).result == MA_SUCCESS,
        "post-repeat real mixer render");
    engine.output_frame = 104;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, nullptr) == DS_OK &&
            play_cursor == 16,
        "repeated Play does not rewind source cursor");
    const auto diagnostics = engine.diagnostics();
    failures += Expect(
        diagnostics.active_voices == 1 &&
            diagnostics.maximum_simultaneous_voices == 1,
        "repeated Play does not double-count active voice");

    engine.output_frame.reset();
    const auto pending_before = engine.pending_cursor_queries;
    const auto unmapped_before = engine.unmapped_cursor_failures;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
            play_cursor == 16 && write_cursor == 0 &&
            engine.pending_cursor_queries == pending_before &&
            engine.unmapped_cursor_failures == unmapped_before,
        "missing clock returns last good cursor without facade diagnostic");
    engine.output_frame = 999;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, nullptr) == DS_OK &&
            play_cursor == 16 &&
            engine.pending_cursor_queries == pending_before &&
            engine.unmapped_cursor_failures == unmapped_before + 1,
        "uncovered active timeline increments only unmapped failures");

    engine.output_frame = 106;
    failures += Expect(buffer->Stop() == DS_OK, "Stop succeeds");
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0,
        "Stop clears playing and looping status");
    engine.output_frame = 999;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, nullptr) == DS_OK &&
            play_cursor == 24,
        "Stop preserves last endpoint-clock cursor");
    buffer->Release();
    return failures;
}

int Test48kEndpointWriteCursorProjection() {
    constexpr std::uint32_t endpoint_rate = 48'000;
    constexpr std::uint32_t endpoint_frames = 480;
    constexpr std::uint32_t source_frames = 1'000;
    MixerEngineServices engine(endpoint_rate, endpoint_frames);
    auto wave = PcmFormat(kGamePrimarySampleRate, 2, 16);
    const auto source_block_align = wave.nBlockAlign;
    auto descriptor = BufferDescription(
        &wave,
        source_frames * source_block_align);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        engine.initialized &&
            CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "48 kHz endpoint cursor buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    const std::vector<std::int16_t> samples(source_frames * 2, 8'192);
    failures += Expect(
        FillBuffer(buffer, samples) == DS_OK &&
            buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK,
        "48 kHz endpoint cursor buffer publication");
    std::vector<float> output(endpoint_frames * 2);
    failures += Expect(
        engine.Render(0, output).result == MA_SUCCESS,
        "48 kHz endpoint resolves a 44.1 kHz play span");
    engine.output_frame = 0;
    DWORD play_cursor{};
    DWORD write_cursor{};
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
            write_cursor ==
                ((play_cursor / source_block_align + 441) % source_frames) *
                    source_block_align,
        "48 kHz endpoint write cursor projects a 10 ms 44.1 kHz source lead");
    buffer->Release();
    return failures;
}

int TestNonLoopingFinalSpanDrainsByEndpointClock() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 8);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "short nonlooping buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    const std::array<std::int16_t, 4> samples{
        1000, 1000, 2000, 2000,
    };
    failures += Expect(
        FillBuffer(buffer, samples) == DS_OK &&
            buffer->Play(0, 0, 0) == DS_OK,
        "short nonlooping publication and play");
    std::array<float, 8> output{};
    failures += Expect(
        engine.Render(700, output).result == MA_SUCCESS,
        "short nonlooping final-period render");
    const auto diagnostics = engine.diagnostics();
    failures += Expect(
        diagnostics.active_voices == 0,
        "logical mixer activity ends after final span publication");

    const auto timeline = engine.observed_timeline.lock();
    failures += Expect(
        timeline != nullptr &&
            timeline->ResolveSourceFrame(701, 1, 2).kind ==
                AudioCursorResolutionKind::Resolved &&
            timeline->ResolveSourceFrame(701, 1, 2).source_frame == 1 &&
            timeline->ResolveSourceFrame(702, 1, 2).kind ==
                AudioCursorResolutionKind::Unmapped,
        "mixer publishes half-open final span ending at output frame 702");

    engine.output_frame = 701;
    DWORD cursor{};
    DWORD status{};
    {
        ScopedGameplayAudioCursorQuery draining;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
                cursor == 4 && engine.pending_cursor_queries == 0 &&
                engine.unmapped_cursor_failures == 0,
            "queued final span resolves hardware-mapped source bytes");
        const auto observation = draining.Consume();
        failures += Expect(
            observation.has_value() &&
                observation->state == GameplayAudioCursorState::Exact &&
                observation->source_frame_unwrapped == 1 &&
                observation->source_sample_rate == 44'100 &&
                observation->playback_generation == 1 &&
                observation->output_frame == 701,
            "an audible draining span to remain an exact gameplay cursor");
    }
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK &&
            status == DSBSTATUS_PLAYING,
        "queued final span remains game-visible as playing");

    engine.output_frame = 702;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0,
        "status stops at the final queued output boundary");
    {
        ScopedGameplayAudioCursorQuery drained;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
                cursor == 4 && engine.pending_cursor_queries == 0 &&
                engine.unmapped_cursor_failures == 0,
            "cursor remains last good after final queued output drains");
        const auto observation = drained.Consume();
        failures += Expect(
            observation.has_value() &&
                observation->state == GameplayAudioCursorState::Inactive &&
                observation->playback_generation == 1 &&
                observation->output_frame == 702,
            "a fully drained span to publish inactive gameplay state");
    }
    buffer->Release();

    MixerEngineServices stop_engine;
    buffer = nullptr;
    failures += Expect(
        CreateBuffer(stop_engine, descriptor, &buffer) == DS_OK &&
            FillBuffer(buffer, samples) == DS_OK &&
            buffer->Play(0, 0, 0) == DS_OK,
        "short nonlooping stop-case setup");
    output.fill(0.0F);
    failures += Expect(
        stop_engine.Render(800, output).result == MA_SUCCESS,
        "short nonlooping stop-case final render");
    stop_engine.output_frame = 801;
    failures += Expect(
        buffer->Stop() == DS_OK,
        "explicit Stop during queued final span succeeds");
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0,
        "explicit Stop immediately invalidates queued drain status");
    stop_engine.output_frame = 802;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0 &&
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 4 && stop_engine.pending_cursor_queries == 0 &&
            stop_engine.unmapped_cursor_failures == 0,
        "Stop preserves hardware-mapped cursor and clears drain state");
    buffer->Release();
    return failures;
}

int TestResetBufferIgnoresStaleFinalDrainRecord() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 8);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "stale-drain COM buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    const std::array<std::int16_t, 4> samples{
        1000, 1000, 2000, 2000,
    };
    failures += Expect(
        FillBuffer(buffer, samples) == DS_OK &&
            buffer->Play(0, 0, 0) == DS_OK,
        "stale-drain old epoch setup");
    std::array<float, 8> output{};
    failures += Expect(
        engine.Render(900, output).result == MA_SUCCESS &&
            buffer->SetCurrentPosition(0) == DS_OK,
        "accepted seek invalidates old final drain epoch");

    engine.output_frame = 901;
    DWORD cursor = 99;
    DWORD status = 99;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0,
        "stale old-epoch drain cannot report COM playing status");
    failures += Expect(
        buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 0 && engine.pending_cursor_queries == 0 &&
            engine.unmapped_cursor_failures == 0,
        "stale old-epoch drain cannot resolve old span or add fallback failure");

    failures += Expect(
        buffer->Play(0, 0, 0) == DS_OK,
        "new epoch playback starts without stale drain");
    output.fill(0.0F);
    failures += Expect(
        engine.Render(904, output).result == MA_SUCCESS,
        "new epoch final span render");
    engine.output_frame = 905;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK &&
            status == DSBSTATUS_PLAYING &&
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 4 && engine.pending_cursor_queries == 0 &&
            engine.unmapped_cursor_failures == 0,
        "new epoch final drain alone drives COM status and cursor");
    buffer->Release();
    return failures;
}

int TestSeekRestoreUnsupportedAndLifetime() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 16);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "seek buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    const std::array<std::int16_t, 8> samples{
        1000, 1000, 2000, 2000, 3000, 3000, 4000, 4000,
    };
    failures += Expect(FillBuffer(buffer, samples) == DS_OK, "seek PCM publication");
    failures += Expect(
        buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK,
        "seek buffer play");
    std::array<float, 8> output{};
    engine.Render(100, output);
    failures += Expect(
        buffer->SetCurrentPosition(2) == DSERR_INVALIDPARAM &&
            buffer->SetCurrentPosition(16) == DSERR_INVALIDPARAM,
        "unaligned and end seek rejection");
    failures += Expect(
        buffer->SetCurrentPosition(4) == DS_OK,
        "aligned in-range seek succeeds");
    engine.output_frame = 100;
    DWORD cursor{};
    const auto pending_before = engine.pending_cursor_queries;
    const auto unmapped_before = engine.unmapped_cursor_failures;
    failures += Expect(
        buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 4 &&
            engine.pending_cursor_queries == pending_before + 1 &&
            engine.unmapped_cursor_failures == unmapped_before,
        "accepted seek reports its anchor as a pending generation");
    output.fill(0.0F);
    engine.Render(200, output);
    engine.output_frame = 201;
    failures += Expect(
        buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK && cursor == 8,
        "seeked epoch maps new render span");

    LONG pan = 123;
    DWORD frequency = 123;
    DWORD effect_result = 123;
    void* object = reinterpret_cast<void*>(1);
    failures += Expect(
        buffer->Initialize(nullptr, &descriptor) == DSERR_ALREADYINITIALIZED,
        "Initialize reports already initialized");
    failures += Expect(
        buffer->SetFormat(&wave) == DSERR_CONTROLUNAVAIL,
        "secondary SetFormat unsupported");
    failures += Expect(
        buffer->GetPan(&pan) == DSERR_CONTROLUNAVAIL &&
            buffer->SetPan(0) == DSERR_CONTROLUNAVAIL,
        "pan controls unsupported");
    failures += Expect(
        buffer->GetFrequency(&frequency) == DSERR_CONTROLUNAVAIL &&
            buffer->SetFrequency(44100) == DSERR_CONTROLUNAVAIL,
        "frequency controls unsupported");
    failures += Expect(
        buffer->SetFX(0, nullptr, nullptr) == DSERR_CONTROLUNAVAIL &&
            buffer->AcquireResources(0, 0, &effect_result) ==
                DSERR_CONTROLUNAVAIL &&
            buffer->GetObjectInPath(
                GUID_All_Objects,
                0,
                IID_IUnknown,
                &object) == DSERR_CONTROLUNAVAIL,
        "effects and object-path controls unsupported");
    failures += Expect(
        buffer->Restore() == DS_OK,
        "Restore reclaims snapshots and succeeds");

    const auto snapshot_lifetime = engine.observed_snapshot;
    const auto timeline_lifetime = engine.observed_timeline;
    failures += Expect(
        !snapshot_lifetime.expired() && !timeline_lifetime.expired(),
        "buffer and real voice share live source owners");
    failures += Expect(buffer->Release() == 0, "lifetime buffer final Release");
    failures += Expect(
        snapshot_lifetime.expired() && timeline_lifetime.expired(),
        "buffer voice and shared sources tear down together");
    return failures;
}

int TestConcurrentSeeksAreOneFacadeTransactionAtATime() {
    using namespace std::chrono_literals;

    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 24);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "concurrent seek transaction buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    const std::array<std::int16_t, 12> samples{
        1000, 1000, 2000, 2000, 3000, 3000,
        4000, 4000, 5000, 5000, 6000, 6000,
    };
    std::array<float, 8> output{};
    failures += Expect(
        FillBuffer(buffer, samples) == DS_OK &&
            buffer->Play(0, 0, 0) == DS_OK &&
            engine.Render(100, output).result == MA_SUCCESS,
        "concurrent seek transaction starts from a mapped active span");
    engine.output_frame = 102;
    engine.BlockNextOutputFrameRead();

    DWORD blocked_cursor{};
    auto reader = std::async(std::launch::async, [&] {
        return buffer->GetCurrentPosition(&blocked_cursor, nullptr);
    });
    const auto reader_entered = engine.WaitForBlockedOutputFrameRead();
    failures += Expect(
        reader_entered,
        "cursor reader reaches the blocked endpoint-clock query");

    auto first_seek = std::async(std::launch::async, [&] {
        return buffer->SetCurrentPosition(8);
    });
    auto second_seek = std::async(std::launch::async, [&] {
        return buffer->SetCurrentPosition(16);
    });
    const auto first_wait = first_seek.wait_for(100ms);
    const auto second_wait = second_seek.wait_for(100ms);
    failures += Expect(
        first_wait == std::future_status::timeout &&
            second_wait == std::future_status::timeout,
        "both seeks wait behind the in-flight facade read transaction");

    engine.ReleaseOutputFrameRead();
    failures += Expect(
        reader.get() == DS_OK && blocked_cursor == 8 &&
            first_seek.get() == DS_OK && second_seek.get() == DS_OK,
        "blocked cursor and both serialized seeks complete successfully");

    output.fill(0.0F);
    failures += Expect(
        engine.Render(200, output).result == MA_SUCCESS,
        "latest serialized seek renders to its terminal span");
    std::size_t audible_frames{};
    for (std::size_t frame = 0; frame < output.size() / 2; ++frame) {
        if (output[frame * 2] != 0.0F ||
            output[frame * 2 + 1] != 0.0F) {
            ++audible_frames;
        }
    }
    failures += Expect(
        audible_frames == 2 || audible_frames == 4,
        "serialized seek winner has a complete two- or four-frame tail");

    const auto drain_end = 200 + audible_frames;
    DWORD status{};
    DWORD cursor{};
    engine.output_frame = drain_end - 1;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK &&
            status == DSBSTATUS_PLAYING &&
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 20 && engine.pending_cursor_queries == 0 &&
            engine.unmapped_cursor_failures == 0,
        "serialized seek epoch resolves the final audible source frame");
    engine.output_frame = drain_end;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0 &&
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 20 && engine.pending_cursor_queries == 0 &&
            engine.unmapped_cursor_failures == 0,
        "serialized seek epoch drains without a cursor fallback");
    buffer->Release();
    return failures;
}

int TestConcurrentPlayAndSeekShareFacadeTransactionOrder() {
    using namespace std::chrono_literals;

    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 24);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "play-seek transaction buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    const std::array<std::int16_t, 12> samples{
        1000, 1000, 2000, 2000, 3000, 3000,
        4000, 4000, 5000, 5000, 6000, 6000,
    };
    std::array<float, 8> output{};
    failures += Expect(
        FillBuffer(buffer, samples) == DS_OK &&
            buffer->Play(0, 0, 0) == DS_OK &&
            engine.Render(300, output).result == MA_SUCCESS,
        "play-seek transaction starts from a mapped active span");
    engine.output_frame = 302;
    engine.BlockNextOutputFrameRead();

    auto stopper = std::async(std::launch::async, [&] {
        return buffer->Stop();
    });
    const auto stopper_entered = engine.WaitForBlockedOutputFrameRead();
    failures += Expect(
        stopper_entered,
        "Stop reaches the blocked endpoint-clock query");

    auto player = std::async(std::launch::async, [&] {
        return buffer->Play(0, 0, 0);
    });
    auto seeker = std::async(std::launch::async, [&] {
        return buffer->SetCurrentPosition(8);
    });
    const auto play_wait = player.wait_for(100ms);
    const auto seek_wait = seeker.wait_for(100ms);
    failures += Expect(
        play_wait == std::future_status::timeout &&
            seek_wait == std::future_status::timeout,
        "Play and seek wait behind the in-flight Stop transaction");

    engine.ReleaseOutputFrameRead();
    failures += Expect(
        stopper.get() == DS_OK && player.get() == DS_OK &&
            seeker.get() == DS_OK,
        "Stop, Play, and seek complete in facade transaction order");

    output.fill(0.0F);
    failures += Expect(
        engine.Render(400, output).result == MA_SUCCESS &&
            std::all_of(output.begin(), output.end(), [](float sample) {
                return sample != 0.0F;
            }),
        "ordered Play and seek render the complete final tail");
    DWORD status{};
    DWORD cursor{};
    engine.output_frame = 403;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK &&
            status == DSBSTATUS_PLAYING &&
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 20 && engine.pending_cursor_queries == 0 &&
            engine.unmapped_cursor_failures == 0,
        "ordered Play and seek resolve the final audible source frame");
    engine.output_frame = 404;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0 &&
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 20 && engine.pending_cursor_queries == 0 &&
            engine.unmapped_cursor_failures == 0,
        "ordered Play and seek drain without a cursor fallback");
    buffer->Release();
    return failures;
}

int TestGameplayCursorObservationFollowsFacadeResolution() {
    MixerEngineServices engine;
    auto wave = PcmFormat();
    auto descriptor = BufferDescription(&wave, 32);
    IDirectSoundBuffer8* buffer{};
    int failures = Expect(
        CreateBuffer(engine, descriptor, &buffer) == DS_OK,
        "gameplay cursor observation buffer creation");
    if (buffer == nullptr) {
        return failures + 1;
    }

    const std::array<std::int16_t, 16> samples{
        1'000, 1'000, 2'000, 2'000,
        3'000, 3'000, 4'000, 4'000,
        5'000, 5'000, 6'000, 6'000,
        7'000, 7'000, 8'000, 8'000,
    };
    failures += Expect(
        FillBuffer(buffer, samples) == DS_OK &&
            buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK,
        "gameplay cursor observation playback setup");

    engine.output_frame = 100;
    DWORD cursor{};
    {
        ScopedGameplayAudioCursorQuery pending;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
                cursor == 0 &&
                !pending.Consume().has_value(),
            "a pending timeline to preserve the DS cursor without publication");
    }

    std::array<float, 8> output{};
    failures += Expect(
        engine.Render(100, output).result == MA_SUCCESS &&
            engine.Render(104, output).result == MA_SUCCESS &&
            engine.Render(108, output).result == MA_SUCCESS,
        "looping gameplay cursor source render");
    engine.output_frame = 110;
    {
        ScopedGameplayAudioCursorQuery exact;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
                cursor == 8,
            "exact gameplay query to preserve wrapped DirectSound bytes");
        const auto observation = exact.Consume();
        failures += Expect(
            observation.has_value() &&
                observation->state == GameplayAudioCursorState::Exact &&
                observation->source_frame_unwrapped == 10 &&
                observation->source_sample_rate == 44'100 &&
                observation->playback_generation == 1 &&
                observation->output_frame == 110,
            "exact gameplay query to publish the unwrapped source cursor");
    }

    engine.output_frame.reset();
    {
        ScopedGameplayAudioCursorQuery missing_clock;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
                cursor == 8 &&
                !missing_clock.Consume().has_value(),
            "a missing endpoint frame to publish no exact observation");
    }

    engine.output_frame = 999;
    {
        ScopedGameplayAudioCursorQuery unmapped;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
                cursor == 8 &&
                !unmapped.Consume().has_value(),
            "an unmapped active span to publish no exact observation");
    }

    failures += Expect(
        buffer->SetCurrentPosition(8) == DS_OK,
        "accepted gameplay cursor seek");
    engine.output_frame = 200;
    {
        ScopedGameplayAudioCursorQuery seek_pending;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
                cursor == 8 &&
                !seek_pending.Consume().has_value(),
            "a new seek generation to remain unpublished until rendered");
    }

    output.fill(0.0F);
    failures += Expect(
        engine.Render(200, output).result == MA_SUCCESS,
        "seeked gameplay cursor generation render");
    engine.output_frame = 202;
    {
        ScopedGameplayAudioCursorQuery exact_after_seek;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK,
            "seeked exact cursor to retain DS_OK");
        const auto observation = exact_after_seek.Consume();
        failures += Expect(
            observation.has_value() &&
                observation->state == GameplayAudioCursorState::Exact &&
                observation->playback_generation == 2 &&
                observation->output_frame == 202,
            "accepted seek generation to reach exact publication");
    }

    failures += Expect(buffer->Stop() == DS_OK, "observed buffer stop");
    {
        ScopedGameplayAudioCursorQuery inactive;
        failures += Expect(
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK,
            "inactive gameplay cursor to retain DS_OK");
        const auto observation = inactive.Consume();
        failures += Expect(
            observation.has_value() &&
                observation->state == GameplayAudioCursorState::Inactive &&
                observation->source_sample_rate == 44'100 &&
                observation->playback_generation == 2,
            "stopped gameplay cursor to publish inactive state");
    }

    buffer->Release();
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestConstructionAndValidation();
    failures += TestComIdentityAndReferenceCounts();
    failures += TestCapsAndFormats();
    failures += TestLockUnlockPublication();
    failures += TestVolumePlaybackAndCursors();
    failures += Test48kEndpointWriteCursorProjection();
    failures += TestNonLoopingFinalSpanDrainsByEndpointClock();
    failures += TestResetBufferIgnoresStaleFinalDrainRecord();
    failures += TestSeekRestoreUnsupportedAndLifetime();
    failures += TestConcurrentSeeksAreOneFacadeTransactionAtATime();
    failures += TestConcurrentPlayAndSeekShareFacadeTransactionOrder();
    failures += TestGameplayCursorObservationFollowsFacadeResolution();
    return failures == 0 ? 0 : 1;
}
