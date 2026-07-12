#include "DirectSoundFacade.h"

#include <ks.h>
#include <ksmedia.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::audio::AudioCursorTimeline;
using gc::audio::AudioSnapshot;
using gc::audio::IAudioEngineServices;
using gc::audio::MiniaudioMixer;
using gc::audio::MixerDiagnosticsSnapshot;
using gc::audio::MixerRenderResult;
using gc::audio::MixerVoice;
using gc::audio::NormalizedSourceFormat;
using gc::audio::SecondarySoundBuffer;
using gc::audio::VoiceUsage;

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
    MixerEngineServices() {
        ma_result result = MA_ERROR;
        mixer_ = MiniaudioMixer::Create(4, nullptr, &result);
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
        return output_frame;
    }

    std::uint32_t endpoint_buffer_frames() const noexcept override {
        return 4;
    }

    void CountCursorTimelineFailure() noexcept override {
        ++cursor_failures;
    }

    MixerRenderResult Render(
        std::uint64_t output_frame_begin,
        std::span<float> output) noexcept {
        return mixer_->Render(output, output_frame_begin);
    }

    MixerDiagnosticsSnapshot diagnostics() const noexcept {
        return mixer_->diagnostics();
    }

    bool initialized{};
    bool fail_voice_creation{};
    std::optional<std::uint64_t> output_frame{};
    std::uint64_t cursor_failures{};
    VoiceUsage last_usage{VoiceUsage::General};
    std::weak_ptr<AudioSnapshot> observed_snapshot;
    std::weak_ptr<AudioCursorTimeline> observed_timeline;

private:
    std::unique_ptr<MiniaudioMixer> mixer_;
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
    DWORD play_cursor{};
    DWORD write_cursor{};
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
            play_cursor == 8 && write_cursor == 24,
        "endpoint-clock play cursor and one-period write cursor");

    failures += Expect(
        buffer->Play(0, 0, DSBPLAY_LOOPING) == DS_OK,
        "repeated Play succeeds");
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
    const auto failures_before = engine.cursor_failures;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
            play_cursor == 16 && write_cursor == 0 &&
            engine.cursor_failures == failures_before + 1,
        "missing clock returns last good cursor with diagnostic");
    engine.output_frame = 999;
    failures += Expect(
        buffer->GetCurrentPosition(&play_cursor, nullptr) == DS_OK &&
            play_cursor == 16 &&
            engine.cursor_failures == failures_before + 2,
        "missing timeline returns last good cursor with diagnostic");

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
            timeline->ResolveSourceFrame(701, 1, 2) == 1 &&
            !timeline->ResolveSourceFrame(702, 1, 2).has_value(),
        "mixer publishes half-open final span ending at output frame 702");

    engine.output_frame = 701;
    DWORD cursor{};
    DWORD status{};
    failures += Expect(
        buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 4 && engine.cursor_failures == 0,
        "queued final span resolves hardware-mapped source bytes");
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK &&
            status == DSBSTATUS_PLAYING,
        "queued final span remains game-visible as playing");

    engine.output_frame = 702;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0,
        "status stops at the final queued output boundary");
    failures += Expect(
        buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 4 && engine.cursor_failures == 0,
        "cursor remains last good after final queued output drains");
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
    stop_engine.output_frame = 802;
    failures += Expect(
        buffer->GetStatus(&status) == DS_OK && status == 0 &&
            buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 4 && stop_engine.cursor_failures == 0,
        "Stop preserves hardware-mapped cursor and clears drain state");
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
    const auto failures_before = engine.cursor_failures;
    failures += Expect(
        buffer->GetCurrentPosition(&cursor, nullptr) == DS_OK &&
            cursor == 4 && engine.cursor_failures == failures_before + 1,
        "seek epoch rejects pre-seek render span");
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

} // namespace

int main() {
    int failures = 0;
    failures += TestConstructionAndValidation();
    failures += TestComIdentityAndReferenceCounts();
    failures += TestCapsAndFormats();
    failures += TestLockUnlockPublication();
    failures += TestVolumePlaybackAndCursors();
    failures += TestNonLoopingFinalSpanDrainsByEndpointClock();
    failures += TestSeekRestoreUnsupportedAndLifetime();
    return failures == 0 ? 0 : 1;
}
