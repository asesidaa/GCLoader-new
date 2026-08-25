#pragma once

#include "Audio/Mixer/MiniaudioMixer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace gc::audio {

class IAudioEngineController;

class IAudioEngineServices {
public:
    virtual ~IAudioEngineServices() = default;
    virtual std::unique_ptr<MixerVoice> CreateVoice(
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        VoiceUsage,
        ma_result*) noexcept = 0;
    virtual std::optional<std::uint64_t>
        CurrentOutputFrame() noexcept = 0;
    virtual std::uint32_t endpoint_buffer_frames() const noexcept = 0;
    virtual std::uint32_t output_sample_rate() const noexcept = 0;
    virtual void CountPendingCursorQuery() noexcept = 0;
    virtual void CountUnmappedCursorFailure() noexcept = 0;
};

HRESULT CreateDirectSoundDevice(
    IAudioEngineController&, IDirectSound8**) noexcept;

class PrimarySoundBuffer final : public IDirectSoundBuffer8 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFormat(
        LPWAVEFORMATEX, DWORD, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG) noexcept override;
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(
        LPDIRECTSOUND, LPCDSBUFFERDESC) noexcept override;
    HRESULT STDMETHODCALLTYPE Lock(
        DWORD, DWORD, LPVOID*, LPDWORD,
        LPVOID*, LPDWORD, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) noexcept override;
    HRESULT STDMETHODCALLTYPE SetVolume(LONG) noexcept override;
    HRESULT STDMETHODCALLTYPE SetPan(LONG) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Stop() noexcept override;
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Restore() noexcept override;
    HRESULT STDMETHODCALLTYPE SetFX(
        DWORD, LPDSEFFECTDESC, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE AcquireResources(
        DWORD, DWORD, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(
        REFGUID, DWORD, REFGUID, LPVOID*) noexcept override;

private:
    friend class DirectSoundDevice;

    PrimarySoundBuffer() = default;
    ~PrimarySoundBuffer() = default;

    std::atomic_ulong references_{1};
};

class DirectSoundDevice final : public IDirectSound8 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(
        LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS) noexcept override;
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(
        LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER*) noexcept override;
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Compact() noexcept override;
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) noexcept override;
    HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD) noexcept override;

private:
    friend HRESULT CreateDirectSoundDevice(
        IAudioEngineController&, IDirectSound8**) noexcept;

    explicit DirectSoundDevice(IAudioEngineController&) noexcept;
    ~DirectSoundDevice() = default;

    IAudioEngineController& engine_;
    std::atomic_ulong references_{1};
    std::atomic_bool priority_cooperative_level_{};
};

class SecondarySoundBuffer final : public IDirectSoundBuffer8 {
public:
    static HRESULT Create(
        IAudioEngineServices&,
        const DSBUFFERDESC&,
        SecondarySoundBuffer**) noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFormat(
        LPWAVEFORMATEX, DWORD, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG) noexcept override;
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(
        LPDIRECTSOUND, LPCDSBUFFERDESC) noexcept override;
    HRESULT STDMETHODCALLTYPE Lock(
        DWORD, DWORD, LPVOID*, LPDWORD,
        LPVOID*, LPDWORD, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) noexcept override;
    HRESULT STDMETHODCALLTYPE SetVolume(LONG) noexcept override;
    HRESULT STDMETHODCALLTYPE SetPan(LONG) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Stop() noexcept override;
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE Restore() noexcept override;
    HRESULT STDMETHODCALLTYPE SetFX(
        DWORD, LPDSEFFECTDESC, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE AcquireResources(
        DWORD, DWORD, LPDWORD) noexcept override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(
        REFGUID, DWORD, REFGUID, LPVOID*) noexcept override;

private:
    SecondarySoundBuffer(
        IAudioEngineServices&,
        DWORD,
        DWORD,
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>,
        std::uint64_t buffer_instance_id) noexcept;
    ~SecondarySoundBuffer();

    std::uint64_t ResolveCurrentSourceFrameLocked() noexcept;

    IAudioEngineServices& engine_;
    const DWORD flags_;
    const DWORD buffer_bytes_;
    const NormalizedSourceFormat format_;
    std::shared_ptr<AudioSnapshot> snapshot_;
    std::shared_ptr<AudioCursorTimeline> timeline_;
    std::unique_ptr<MixerVoice> voice_;
    std::mutex control_mutex_;
    std::atomic_ulong references_{1};
    std::atomic_long volume_{DSBVOLUME_MAX};
    const std::uint64_t buffer_instance_id_{};
    std::uint64_t playback_generation_{};
    ExactPlaybackOrigin playback_origin_{ExactPlaybackOrigin::Play};
    std::uint64_t last_reported_source_frame_{};
};

} // namespace gc::audio
