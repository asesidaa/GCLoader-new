#include "Audio/DirectSound/DirectSoundFacade.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

namespace {

using gc::audio::AudioCursorTimeline;
using gc::audio::AudioSnapshot;
using gc::audio::CreateDirectSoundDevice;
using gc::audio::IAudioEngineServices;
using gc::audio::MiniaudioMixer;
using gc::audio::MixerVoice;
using gc::audio::NormalizedSourceFormat;
using gc::audio::VoiceUsage;
using gc::audio::kGamePrimaryAverageBytesPerSecond;
using gc::audio::kGamePrimarySampleRate;
using gc::audio::kOutputBitsPerSample;
using gc::audio::kOutputBlockAlign;
using gc::audio::kOutputChannels;

constexpr DWORD kSecondaryFlags =
    DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
    DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expected " << name << '\n';
    return 1;
}

WAVEFORMATEX GamePrimaryFormat() {
    return {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = kOutputChannels,
        .nSamplesPerSec = kGamePrimarySampleRate,
        .nAvgBytesPerSec = kGamePrimaryAverageBytesPerSecond,
        .nBlockAlign = kOutputBlockAlign,
        .wBitsPerSample = kOutputBitsPerSample,
        .cbSize = 0,
    };
}

DSBUFFERDESC PrimaryDescription(
    DWORD flags = DSBCAPS_PRIMARYBUFFER,
    DWORD bytes = 0,
    WAVEFORMATEX* format = nullptr) {
    return {
        .dwSize = sizeof(DSBUFFERDESC),
        .dwFlags = flags,
        .dwBufferBytes = bytes,
        .dwReserved = 0,
        .lpwfxFormat = format,
        .guid3DAlgorithm = GUID_NULL,
    };
}

DSBUFFERDESC SecondaryDescription(WAVEFORMATEX* format) {
    return {
        .dwSize = sizeof(DSBUFFERDESC),
        .dwFlags = kSecondaryFlags,
        .dwBufferBytes = 16,
        .dwReserved = 0,
        .lpwfxFormat = format,
        .guid3DAlgorithm = GUID_NULL,
    };
}

class MixerEngineServices final : public IAudioEngineServices {
public:
    MixerEngineServices() {
        ma_result result = MA_ERROR;
        mixer_ = MiniaudioMixer::Create(
            4, kGamePrimarySampleRate, nullptr, &result);
        initialized = result == MA_SUCCESS && mixer_ != nullptr;
    }

    std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline> timeline,
        VoiceUsage usage,
        ma_result* result) noexcept override {
        ++voice_creations;
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

    std::uint32_t output_sample_rate() const noexcept override {
        return kGamePrimarySampleRate;
    }

    void CountPendingCursorQuery() noexcept override {}

    void CountUnmappedCursorFailure() noexcept override {}

    bool initialized{};
    std::uint32_t voice_creations{};
    std::optional<std::uint64_t> output_frame{};

private:
    std::unique_ptr<MiniaudioMixer> mixer_;
};

HRESULT CreatePriorityDevice(
    MixerEngineServices& engine,
    IDirectSound8** device) {
    const auto create_result = CreateDirectSoundDevice(engine, device);
    if (FAILED(create_result)) {
        return create_result;
    }
    const auto cooperative_result = (*device)->SetCooperativeLevel(
        reinterpret_cast<HWND>(1),
        DSSCL_PRIORITY);
    if (FAILED(cooperative_result)) {
        (*device)->Release();
        *device = nullptr;
    }
    return cooperative_result;
}

HRESULT CreatePrimaryBuffer(
    IDirectSound8* device,
    DSBUFFERDESC& descriptor,
    IDirectSoundBuffer8** result) {
    *result = nullptr;
    IDirectSoundBuffer* base{};
    const auto create_result = device->CreateSoundBuffer(
        &descriptor,
        &base,
        nullptr);
    if (FAILED(create_result)) {
        return create_result;
    }
    const auto query_result = base->QueryInterface(
        IID_IDirectSoundBuffer8,
        reinterpret_cast<void**>(result));
    base->Release();
    return query_result;
}

int TestFactoryIdentityAndCooperativeLevel() {
    MixerEngineServices engine;
    int failures = Expect(engine.initialized, "real four-frame mixer creation");

    failures += Expect(
        CreateDirectSoundDevice(engine, nullptr) == DSERR_INVALIDPARAM,
        "null device factory output rejection");

    IDirectSound8* device{};
    failures += Expect(
        CreateDirectSoundDevice(engine, &device) == DS_OK && device != nullptr,
        "device factory success");
    if (device == nullptr) {
        return failures + 1;
    }
    failures += Expect(device->AddRef() == 2, "factory reference count starts at one");
    failures += Expect(device->Release() == 1, "factory reference restored to one");

    for (const auto* iid : {&IID_IUnknown, &IID_IDirectSound,
                            &IID_IDirectSound8}) {
        void* queried{};
        failures += Expect(
            device->QueryInterface(*iid, &queried) == S_OK &&
                queried == static_cast<void*>(device),
            "device QueryInterface identity");
        if (queried != nullptr) {
            failures += Expect(
                static_cast<IUnknown*>(queried)->Release() == 1,
                "device queried reference release");
        }
    }
    void* unsupported = reinterpret_cast<void*>(1);
    failures += Expect(
        device->QueryInterface(IID_IDirectSoundBuffer8, &unsupported) ==
                E_NOINTERFACE &&
            unsupported == nullptr,
        "device unsupported interface rejection");
    failures += Expect(
        device->QueryInterface(IID_IUnknown, nullptr) == E_POINTER,
        "device null QueryInterface output rejection");

    auto primary = PrimaryDescription();
    IDirectSoundBuffer* buffer = reinterpret_cast<IDirectSoundBuffer*>(1);
    failures += Expect(
        device->CreateSoundBuffer(&primary, &buffer, nullptr) ==
                DSERR_PRIOLEVELNEEDED &&
            buffer == nullptr,
        "buffer creation requires priority cooperative level");
    failures += Expect(
        device->SetCooperativeLevel(nullptr, DSSCL_PRIORITY) ==
            DSERR_INVALIDPARAM,
        "null cooperative window rejection");
    failures += Expect(
        device->SetCooperativeLevel(reinterpret_cast<HWND>(1), DSSCL_NORMAL) ==
            DSERR_PRIOLEVELNEEDED,
        "normal cooperative level rejection");
    failures += Expect(
        device->SetCooperativeLevel(
            reinterpret_cast<HWND>(1), DSSCL_PRIORITY) == DS_OK,
        "observed priority cooperative level acceptance");

    failures += Expect(device->Release() == 0, "device final Release returns zero");
    return failures;
}

int TestPrimaryValidationAndIdentity() {
    MixerEngineServices engine;
    IDirectSound8* device{};
    int failures = Expect(
        CreatePriorityDevice(engine, &device) == DS_OK,
        "priority device creation for primary validation");
    if (device == nullptr) {
        return failures + 1;
    }

    auto valid = PrimaryDescription();
    IDirectSoundBuffer* created = reinterpret_cast<IDirectSoundBuffer*>(1);
    failures += Expect(
        device->CreateSoundBuffer(
            &valid,
            &created,
            reinterpret_cast<IUnknown*>(1)) == DSERR_NOAGGREGATION &&
            created == nullptr,
        "buffer aggregation rejection");

    auto malformed = valid;
    malformed.dwSize = sizeof(DSBUFFERDESC) - 1;
    failures += Expect(
        device->CreateSoundBuffer(&malformed, &created, nullptr) ==
            DSERR_INVALIDPARAM,
        "malformed primary descriptor rejection");
    auto extra_flags = PrimaryDescription(
        DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME);
    failures += Expect(
        device->CreateSoundBuffer(&extra_flags, &created, nullptr) ==
            DSERR_INVALIDPARAM,
        "primary extra capability rejection");
    auto nonzero_bytes = PrimaryDescription(DSBCAPS_PRIMARYBUFFER, 4);
    failures += Expect(
        device->CreateSoundBuffer(&nonzero_bytes, &created, nullptr) ==
            DSERR_INVALIDPARAM,
        "primary source byte storage rejection");
    auto wave = GamePrimaryFormat();
    auto source_format = PrimaryDescription(
        DSBCAPS_PRIMARYBUFFER,
        0,
        &wave);
    failures += Expect(
        device->CreateSoundBuffer(&source_format, &created, nullptr) ==
            DSERR_INVALIDPARAM,
        "primary source format rejection");

    auto nonzero_reserved = valid;
    nonzero_reserved.dwReserved = 1;
    created = reinterpret_cast<IDirectSoundBuffer*>(1);
    const auto reserved_result = device->CreateSoundBuffer(
        &nonzero_reserved,
        &created,
        nullptr);
    failures += Expect(
        reserved_result == DSERR_INVALIDPARAM && created == nullptr,
        "primary nonzero reserved field rejection with null output");
    if (reserved_result != DSERR_INVALIDPARAM) {
        std::cerr << "Observed nonzero dwReserved HRESULT "
                  << reserved_result << ", output null "
                  << (created == nullptr) << '\n';
    }
    if (SUCCEEDED(reserved_result) && created != nullptr) {
        created->Release();
    }

    auto nonnull_3d_algorithm = valid;
    nonnull_3d_algorithm.guid3DAlgorithm = IID_IUnknown;
    created = reinterpret_cast<IDirectSoundBuffer*>(1);
    const auto algorithm_result = device->CreateSoundBuffer(
        &nonnull_3d_algorithm,
        &created,
        nullptr);
    failures += Expect(
        algorithm_result == DSERR_INVALIDPARAM && created == nullptr,
        "primary non-null 3D algorithm rejection with null output");
    if (algorithm_result != DSERR_INVALIDPARAM) {
        std::cerr << "Observed non-null guid3DAlgorithm HRESULT "
                  << algorithm_result << ", output null "
                  << (created == nullptr) << '\n';
    }
    if (SUCCEEDED(algorithm_result) && created != nullptr) {
        created->Release();
    }

    IDirectSoundBuffer8* primary{};
    failures += Expect(
        CreatePrimaryBuffer(device, valid, &primary) == DS_OK &&
            primary != nullptr && engine.voice_creations == 0,
        "metadata-only primary creation without source voice");
    if (primary != nullptr) {
        failures += Expect(primary->AddRef() == 2, "primary AddRef returns two");
        failures += Expect(primary->Release() == 1, "primary Release returns one");
        for (const auto* iid : {&IID_IUnknown, &IID_IDirectSoundBuffer,
                                &IID_IDirectSoundBuffer8}) {
            void* queried{};
            failures += Expect(
                primary->QueryInterface(*iid, &queried) == S_OK &&
                    queried == static_cast<void*>(primary),
                "primary QueryInterface identity");
            if (queried != nullptr) {
                failures += Expect(
                    static_cast<IUnknown*>(queried)->Release() == 1,
                    "primary queried reference release");
            }
        }
        void* unsupported = reinterpret_cast<void*>(1);
        failures += Expect(
            primary->QueryInterface(IID_IDirectSoundNotify, &unsupported) ==
                    E_NOINTERFACE &&
                unsupported == nullptr,
            "primary unsupported interface rejection");
        failures += Expect(
            primary->QueryInterface(IID_IUnknown, nullptr) == E_POINTER,
            "primary null QueryInterface output rejection");
        failures += Expect(primary->Release() == 0, "primary final Release returns zero");
    }
    failures += Expect(device->Release() == 0, "primary device final Release");
    return failures;
}

int TestPrimaryFormatCapabilitiesAndVtable() {
    MixerEngineServices engine;
    IDirectSound8* device{};
    int failures = Expect(
        CreatePriorityDevice(engine, &device) == DS_OK,
        "priority device creation for primary behavior");
    if (device == nullptr) {
        return failures + 1;
    }
    auto descriptor = PrimaryDescription();
    IDirectSoundBuffer8* primary{};
    failures += Expect(
        CreatePrimaryBuffer(device, descriptor, &primary) == DS_OK,
        "primary behavior buffer creation");
    if (primary == nullptr) {
        device->Release();
        return failures + 1;
    }

    auto output = GamePrimaryFormat();
    failures += Expect(
        primary->SetFormat(&output) == DS_OK,
        "exact output primary format acceptance");
    auto game_primary = output;
    game_primary.cbSize = 0x12;
    failures += Expect(
        primary->SetFormat(&game_primary) == DS_OK,
        "game primary cbSize 0x12 compatibility");
    auto unrelated_cb_size = output;
    unrelated_cb_size.cbSize = 1;
    failures += Expect(
        primary->SetFormat(&unrelated_cb_size) == DSERR_BADFORMAT,
        "unrelated primary cbSize rejection");
    auto wrong_rate = output;
    wrong_rate.nSamplesPerSec = 48000;
    wrong_rate.nAvgBytesPerSec = 48000 * wrong_rate.nBlockAlign;
    auto floating = output;
    floating.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    floating.wBitsPerSample = 32;
    floating.nBlockAlign = 8;
    floating.nAvgBytesPerSec = floating.nSamplesPerSec * floating.nBlockAlign;
    auto mono = output;
    mono.nChannels = 1;
    mono.nBlockAlign = 2;
    mono.nAvgBytesPerSec = mono.nSamplesPerSec * mono.nBlockAlign;
    failures += Expect(
        primary->SetFormat(&wrong_rate) == DSERR_BADFORMAT &&
            primary->SetFormat(&floating) == DSERR_BADFORMAT &&
            primary->SetFormat(&mono) == DSERR_BADFORMAT &&
            primary->SetFormat(nullptr) == DSERR_INVALIDPARAM,
        "non-output primary format rejection");

    DWORD required{};
    failures += Expect(
        primary->GetFormat(nullptr, 0, &required) == DS_OK &&
            required == sizeof(WAVEFORMATEX),
        "primary format size query");
    WAVEFORMATEX copied{};
    DWORD written{};
    failures += Expect(
        primary->GetFormat(&copied, sizeof(copied), &written) == DS_OK &&
            written == sizeof(WAVEFORMATEX) &&
            copied.wFormatTag == WAVE_FORMAT_PCM &&
            copied.nChannels == kOutputChannels &&
            copied.nSamplesPerSec == kGamePrimarySampleRate &&
            copied.nAvgBytesPerSec ==
                kGamePrimaryAverageBytesPerSecond &&
            copied.nBlockAlign == kOutputBlockAlign &&
            copied.wBitsPerSample == kOutputBitsPerSample &&
            copied.cbSize == 0,
        "primary GetFormat exact output metadata");
    failures += Expect(
        primary->GetFormat(&copied, sizeof(copied) - 1, &written) ==
            DSERR_INVALIDPARAM,
        "primary undersized format output rejection");

    DSBCAPS caps{.dwSize = sizeof(DSBCAPS)};
    failures += Expect(
        primary->GetCaps(&caps) == DS_OK &&
            caps.dwFlags == DSBCAPS_PRIMARYBUFFER &&
            caps.dwBufferBytes == 0 &&
            caps.dwUnlockTransferRate == 0 &&
            caps.dwPlayCpuOverhead == 0,
        "primary exact capability metadata");
    failures += Expect(
        primary->GetCaps(nullptr) == DSERR_INVALIDPARAM,
        "primary null capabilities rejection");
    DWORD play_cursor = 1;
    DWORD write_cursor = 1;
    DWORD status = 1;
    failures += Expect(
        primary->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK &&
            play_cursor == 0 && write_cursor == 0 &&
            primary->GetStatus(&status) == DS_OK && status == 0,
        "primary zero cursors and status");

    void* first = reinterpret_cast<void*>(1);
    void* second = reinterpret_cast<void*>(1);
    DWORD first_bytes = 1;
    DWORD second_bytes = 1;
    failures += Expect(
        primary->Lock(
            0,
            4,
            &first,
            &first_bytes,
            &second,
            &second_bytes,
            0) == DSERR_INVALIDCALL &&
            primary->Unlock(first, first_bytes, second, second_bytes) ==
                DSERR_INVALIDCALL,
        "primary storage methods reject invalid calls");
    failures += Expect(
        primary->Play(0, 0, 0) == DS_OK &&
            primary->Stop() == DS_OK &&
            primary->Restore() == DS_OK,
        "primary initialization playback no-op success");

    failures += Expect(primary->Release() == 0, "primary behavior final Release");
    failures += Expect(device->Release() == 0, "primary behavior device Release");
    return failures;
}

int TestSecondaryRoutingAndUnsupportedDeviceMethods() {
    MixerEngineServices engine;
    IDirectSound8* device{};
    int failures = Expect(
        CreatePriorityDevice(engine, &device) == DS_OK,
        "priority device creation for secondary routing");
    if (device == nullptr) {
        return failures + 1;
    }

    auto wave = GamePrimaryFormat();
    auto descriptor = SecondaryDescription(&wave);
    IDirectSoundBuffer* secondary{};
    failures += Expect(
        device->CreateSoundBuffer(&descriptor, &secondary, nullptr) == DS_OK &&
            secondary != nullptr && engine.voice_creations == 1,
        "device delegates valid secondary descriptor");
    if (secondary != nullptr) {
        DSBCAPS caps{.dwSize = sizeof(DSBCAPS)};
        failures += Expect(
            secondary->GetCaps(&caps) == DS_OK &&
                caps.dwFlags == descriptor.dwFlags &&
                caps.dwBufferBytes == descriptor.dwBufferBytes,
            "delegated Plan 06 secondary behavior");
        failures += Expect(secondary->Release() == 0, "secondary final Release");
    }

    DSCAPS caps{.dwSize = sizeof(DSCAPS)};
    DWORD value{};
    IDirectSoundBuffer* duplicate{};
    failures += Expect(
        device->GetCaps(&caps) == DSERR_UNSUPPORTED &&
            device->DuplicateSoundBuffer(nullptr, &duplicate) ==
                DSERR_UNSUPPORTED &&
            device->Compact() == DSERR_UNSUPPORTED &&
            device->GetSpeakerConfig(&value) == DSERR_UNSUPPORTED &&
            device->SetSpeakerConfig(0) == DSERR_UNSUPPORTED &&
            device->VerifyCertification(&value) == DSERR_UNSUPPORTED,
        "device unsupported method table");
    failures += Expect(
        device->Initialize(nullptr) == DSERR_ALREADYINITIALIZED,
        "device Initialize reports already initialized");
    failures += Expect(device->Release() == 0, "secondary device final Release");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestFactoryIdentityAndCooperativeLevel();
    failures += TestPrimaryValidationAndIdentity();
    failures += TestPrimaryFormatCapabilitiesAndVtable();
    failures += TestSecondaryRoutingAndUnsupportedDeviceMethods();
    return failures == 0 ? 0 : 1;
}
