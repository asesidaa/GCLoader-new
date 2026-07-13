#pragma once

#include "MiniaudioMixer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace gc::audio {

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
    virtual void CountCursorTimelineFailure() noexcept = 0;
};

HRESULT CreateDirectSoundDevice(
    IAudioEngineServices&, IDirectSound8**) noexcept;

class PrimarySoundBuffer final : public IDirectSoundBuffer8 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS) override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetFormat(
        LPWAVEFORMATEX, DWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG) override;
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG) override;
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE Initialize(
        LPDIRECTSOUND, LPCDSBUFFERDESC) override;
    HRESULT STDMETHODCALLTYPE Lock(
        DWORD, DWORD, LPVOID*, LPDWORD,
        LPVOID*, LPDWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD) override;
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) override;
    HRESULT STDMETHODCALLTYPE SetVolume(LONG) override;
    HRESULT STDMETHODCALLTYPE SetPan(LONG) override;
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override;
    HRESULT STDMETHODCALLTYPE Restore() override;
    HRESULT STDMETHODCALLTYPE SetFX(
        DWORD, LPDSEFFECTDESC, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE AcquireResources(
        DWORD, DWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(
        REFGUID, DWORD, REFGUID, LPVOID*) override;

private:
    friend class DirectSoundDevice;

    PrimarySoundBuffer() = default;
    ~PrimarySoundBuffer() = default;

    std::atomic_ulong references_{1};
};

class DirectSoundDevice final : public IDirectSound8 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(
        LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN) override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS) override;
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(
        LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER*) override;
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override;
    HRESULT STDMETHODCALLTYPE Compact() override;
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override;
    HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override;
    HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD) override;

private:
    friend HRESULT CreateDirectSoundDevice(
        IAudioEngineServices&, IDirectSound8**) noexcept;

    explicit DirectSoundDevice(IAudioEngineServices&) noexcept;
    ~DirectSoundDevice() = default;

    IAudioEngineServices& engine_;
    std::atomic_ulong references_{1};
    std::atomic_bool priority_cooperative_level_{};
};

class SecondarySoundBuffer final : public IDirectSoundBuffer8 {
public:
    static HRESULT Create(
        IAudioEngineServices&,
        const DSBUFFERDESC&,
        SecondarySoundBuffer**) noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS) override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetFormat(
        LPWAVEFORMATEX, DWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG) override;
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG) override;
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE Initialize(
        LPDIRECTSOUND, LPCDSBUFFERDESC) override;
    HRESULT STDMETHODCALLTYPE Lock(
        DWORD, DWORD, LPVOID*, LPDWORD,
        LPVOID*, LPDWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD) override;
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) override;
    HRESULT STDMETHODCALLTYPE SetVolume(LONG) override;
    HRESULT STDMETHODCALLTYPE SetPan(LONG) override;
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override;
    HRESULT STDMETHODCALLTYPE Restore() override;
    HRESULT STDMETHODCALLTYPE SetFX(
        DWORD, LPDSEFFECTDESC, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE AcquireResources(
        DWORD, DWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(
        REFGUID, DWORD, REFGUID, LPVOID*) override;

private:
    SecondarySoundBuffer(
        IAudioEngineServices&,
        DWORD,
        DWORD,
        const NormalizedSourceFormat&,
        std::shared_ptr<AudioSnapshot>,
        std::shared_ptr<AudioCursorTimeline>) noexcept;
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
    std::uint64_t epoch_{1};
    std::uint64_t last_reported_source_frame_{};
};

} // namespace gc::audio
