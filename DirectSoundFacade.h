#pragma once

#include "MiniaudioMixer.h"

#include <atomic>
#include <cstdint>
#include <memory>
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

    std::uint64_t ResolveCurrentSourceFrame() noexcept;

    IAudioEngineServices& engine_;
    const DWORD flags_;
    const DWORD buffer_bytes_;
    const NormalizedSourceFormat format_;
    std::shared_ptr<AudioSnapshot> snapshot_;
    std::shared_ptr<AudioCursorTimeline> timeline_;
    std::unique_ptr<MixerVoice> voice_;
    std::atomic_ulong references_{1};
    std::atomic_long volume_{DSBVOLUME_MAX};
    std::atomic_uint64_t epoch_{1};
    std::atomic_uint64_t last_reported_source_frame_{};
};

} // namespace gc::audio
