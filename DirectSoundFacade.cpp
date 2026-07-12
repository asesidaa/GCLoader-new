#include "DirectSoundFacade.h"

#include <cstring>
#include <new>
#include <utility>

namespace gc::audio {
namespace {

constexpr DWORD kSupportedSecondaryFlags =
    DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
    DSBCAPS_CTRLPOSITIONNOTIFY |
    DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;

constexpr DWORD kObservedStaticFlags =
    DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
    DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;

} // namespace

SecondarySoundBuffer::SecondarySoundBuffer(
    IAudioEngineServices& engine,
    DWORD flags,
    DWORD buffer_bytes,
    const NormalizedSourceFormat& format,
    std::shared_ptr<AudioSnapshot> snapshot,
    std::shared_ptr<AudioCursorTimeline> timeline) noexcept
    : engine_(engine),
      flags_(flags),
      buffer_bytes_(buffer_bytes),
      format_(format),
      snapshot_(std::move(snapshot)),
      timeline_(std::move(timeline)) {}

SecondarySoundBuffer::~SecondarySoundBuffer() = default;

HRESULT SecondarySoundBuffer::Create(
    IAudioEngineServices& engine,
    const DSBUFFERDESC& descriptor,
    SecondarySoundBuffer** result) noexcept {
    if (result == nullptr) {
        return DSERR_INVALIDPARAM;
    }
    *result = nullptr;

    if (descriptor.dwSize != sizeof(DSBUFFERDESC) ||
        descriptor.lpwfxFormat == nullptr ||
        descriptor.dwBufferBytes == 0 ||
        (descriptor.dwFlags & DSBCAPS_PRIMARYBUFFER) != 0) {
        return DSERR_INVALIDPARAM;
    }
    if ((descriptor.dwFlags & ~kSupportedSecondaryFlags) != 0) {
        return DSERR_CONTROLUNAVAIL;
    }

    NormalizedSourceFormat format{};
    const auto format_result = NormalizeSourceFormat(
        descriptor.lpwfxFormat,
        &format);
    if (FAILED(format_result)) {
        return format_result;
    }
    if (format.block_align == 0 ||
        descriptor.dwBufferBytes % format.block_align != 0) {
        return DSERR_INVALIDPARAM;
    }

    try {
        auto snapshot = std::make_shared<AudioSnapshot>(
            descriptor.dwBufferBytes,
            format.block_align);
        auto timeline = std::make_shared<AudioCursorTimeline>();
        auto* buffer = new (std::nothrow) SecondarySoundBuffer(
            engine,
            descriptor.dwFlags,
            descriptor.dwBufferBytes,
            format,
            std::move(snapshot),
            std::move(timeline));
        if (buffer == nullptr) {
            return DSERR_OUTOFMEMORY;
        }

        const bool observed_usage =
            descriptor.dwFlags == kObservedStaticFlags ||
            descriptor.dwFlags ==
                (kObservedStaticFlags | DSBCAPS_CTRLPOSITIONNOTIFY);
        const auto usage = format.native_rate_pcm16 && observed_usage
            ? VoiceUsage::GameplayNativeCandidate
            : VoiceUsage::General;
        ma_result voice_result = MA_ERROR;
        buffer->voice_ = engine.CreateVoice(
            format,
            buffer->snapshot_,
            buffer->timeline_,
            usage,
            &voice_result);
        if (buffer->voice_ == nullptr || voice_result != MA_SUCCESS) {
            delete buffer;
            return DSERR_OUTOFMEMORY;
        }

        *result = buffer;
        return DS_OK;
    } catch (const std::bad_alloc&) {
        return DSERR_OUTOFMEMORY;
    }
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::QueryInterface(
    REFIID interface_id,
    void** result) {
    if (result == nullptr) {
        return E_POINTER;
    }
    *result = nullptr;
    if (!IsEqualIID(interface_id, IID_IUnknown) &&
        !IsEqualIID(interface_id, IID_IDirectSoundBuffer) &&
        !IsEqualIID(interface_id, IID_IDirectSoundBuffer8)) {
        return E_NOINTERFACE;
    }
    *result = static_cast<IDirectSoundBuffer8*>(this);
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE SecondarySoundBuffer::AddRef() {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SecondarySoundBuffer::Release() {
    const auto remaining =
        references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetCaps(
    LPDSBCAPS caps) {
    if (caps == nullptr || caps->dwSize != sizeof(DSBCAPS)) {
        return DSERR_INVALIDPARAM;
    }
    *caps = {
        .dwSize = sizeof(DSBCAPS),
        .dwFlags = flags_,
        .dwBufferBytes = buffer_bytes_,
        .dwUnlockTransferRate = 0,
        .dwPlayCpuOverhead = 0,
    };
    return DS_OK;
}

std::uint64_t SecondarySoundBuffer::ResolveCurrentSourceFrame() noexcept {
    const auto last = last_reported_source_frame_.load(
        std::memory_order_acquire);
    const auto mixing = voice_->playing();
    const auto audible_until = voice_->audible_until_output_frame();
    if (!mixing && !audible_until.has_value()) {
        return last;
    }

    const auto output_frame = engine_.CurrentOutputFrame();
    if (!output_frame.has_value()) {
        engine_.CountCursorTimelineFailure();
        return last;
    }
    const auto draining = audible_until.has_value() &&
        *output_frame < *audible_until;
    if (!mixing && !draining) {
        return last;
    }
    const auto source_frame = timeline_->ResolveSourceFrame(
        *output_frame,
        epoch_.load(std::memory_order_acquire),
        buffer_bytes_ / format_.block_align);
    if (!source_frame.has_value()) {
        engine_.CountCursorTimelineFailure();
        return last;
    }
    last_reported_source_frame_.store(
        *source_frame,
        std::memory_order_release);
    return *source_frame;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetCurrentPosition(
    LPDWORD play_cursor,
    LPDWORD write_cursor) {
    if (play_cursor == nullptr && write_cursor == nullptr) {
        return DSERR_INVALIDPARAM;
    }
    const auto source_frame = ResolveCurrentSourceFrame();
    if (play_cursor != nullptr) {
        *play_cursor = static_cast<DWORD>(
            SourceFrameToByte(source_frame, format_.block_align));
    }
    if (write_cursor != nullptr) {
        const auto write_frame = ProjectWriteCursorFrame(
            source_frame,
            engine_.endpoint_buffer_frames(),
            format_.sample_rate,
            buffer_bytes_ / format_.block_align);
        *write_cursor = static_cast<DWORD>(
            SourceFrameToByte(write_frame, format_.block_align));
    }
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetFormat(
    LPWAVEFORMATEX destination,
    DWORD allocated,
    LPDWORD written) {
    const auto required = format_.wave_format_size;
    if (written != nullptr) {
        *written = required;
    }
    if (destination == nullptr) {
        return allocated == 0 && written != nullptr
            ? DS_OK
            : DSERR_INVALIDPARAM;
    }
    if (allocated < required) {
        return DSERR_INVALIDPARAM;
    }
    std::memcpy(destination, &format_.wave, required);
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetVolume(LPLONG volume) {
    if (volume == nullptr) {
        return DSERR_INVALIDPARAM;
    }
    *volume = volume_.load(std::memory_order_acquire);
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetPan(LPLONG) {
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetFrequency(LPDWORD) {
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetStatus(LPDWORD status) {
    if (status == nullptr) {
        return DSERR_INVALIDPARAM;
    }
    *status = 0;
    auto audible = voice_->playing();
    if (!audible) {
        const auto audible_until = voice_->audible_until_output_frame();
        if (audible_until.has_value()) {
            const auto output_frame = engine_.CurrentOutputFrame();
            if (output_frame.has_value()) {
                audible = *output_frame < *audible_until;
            } else {
                engine_.CountCursorTimelineFailure();
                audible = true;
            }
        }
    }
    if (audible) {
        *status |= DSBSTATUS_PLAYING;
        if (voice_->looping()) {
            *status |= DSBSTATUS_LOOPING;
        }
    }
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Initialize(
    LPDIRECTSOUND,
    LPCDSBUFFERDESC) {
    return DSERR_ALREADYINITIALIZED;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Lock(
    DWORD offset,
    DWORD byte_count,
    LPVOID* first,
    LPDWORD first_bytes,
    LPVOID* second,
    LPDWORD second_bytes,
    DWORD flags) {
    if (first == nullptr || first_bytes == nullptr ||
        (second == nullptr) != (second_bytes == nullptr)) {
        return DSERR_INVALIDPARAM;
    }
    *first = nullptr;
    *first_bytes = 0;
    if (second != nullptr) {
        *second = nullptr;
        *second_bytes = 0;
    }

    DWORD effective_offset = offset;
    DWORD effective_count = byte_count;
    if ((flags & DSBLOCK_ENTIREBUFFER) != 0) {
        effective_offset = 0;
        effective_count = buffer_bytes_;
    }
    const bool valid_range =
        (flags & ~DSBLOCK_ENTIREBUFFER) == 0 &&
        effective_offset < buffer_bytes_ && effective_count != 0 &&
        effective_count <= buffer_bytes_;
    const bool wraps = valid_range &&
        effective_count > buffer_bytes_ - effective_offset;
    if (wraps && second == nullptr) {
        return DSERR_INVALIDPARAM;
    }

    AudioLockRegions regions{};
    const auto result = snapshot_->Lock(
        offset,
        byte_count,
        flags,
        &regions);
    if (FAILED(result)) {
        return result;
    }
    *first = regions.first;
    *first_bytes = regions.first_bytes;
    if (second != nullptr) {
        *second = regions.second;
        *second_bytes = regions.second_bytes;
    }
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Play(
    DWORD reserved,
    DWORD priority,
    DWORD flags) {
    if (reserved != 0 || priority != 0 ||
        (flags & ~DSBPLAY_LOOPING) != 0) {
        return DSERR_INVALIDPARAM;
    }
    return voice_->Play(
        (flags & DSBPLAY_LOOPING) != 0,
        epoch_.load(std::memory_order_acquire));
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetCurrentPosition(
    DWORD position) {
    if (format_.block_align == 0 || position >= buffer_bytes_ ||
        position % format_.block_align != 0) {
        return DSERR_INVALIDPARAM;
    }
    const auto source_frame = position / format_.block_align;
    const auto epoch = epoch_.fetch_add(
        1,
        std::memory_order_acq_rel) + 1;
    last_reported_source_frame_.store(
        source_frame,
        std::memory_order_release);
    return voice_->Seek(source_frame, epoch);
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetFormat(
    LPCWAVEFORMATEX) {
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetVolume(LONG volume) {
    if (volume < DSBVOLUME_MIN || volume > DSBVOLUME_MAX) {
        return DSERR_INVALIDPARAM;
    }
    volume_.store(volume, std::memory_order_release);
    voice_->SetGain(DirectSoundVolumeToLinearGain(volume));
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetPan(LONG) {
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetFrequency(DWORD) {
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Stop() {
    ResolveCurrentSourceFrame();
    voice_->Stop();
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Unlock(
    LPVOID first,
    DWORD first_bytes,
    LPVOID second,
    DWORD second_bytes) {
    return snapshot_->Unlock(
        first,
        first_bytes,
        second,
        second_bytes);
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Restore() {
    snapshot_->ReclaimRetired();
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetFX(
    DWORD,
    LPDSEFFECTDESC,
    LPDWORD) {
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::AcquireResources(
    DWORD,
    DWORD,
    LPDWORD) {
    return DSERR_CONTROLUNAVAIL;
}

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetObjectInPath(
    REFGUID,
    DWORD,
    REFGUID,
    LPVOID*) {
    return DSERR_CONTROLUNAVAIL;
}

} // namespace gc::audio
