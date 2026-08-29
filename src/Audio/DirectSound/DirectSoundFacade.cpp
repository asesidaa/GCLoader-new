#include "Audio/DirectSound/DirectSoundFacade.h"
#include "Audio/AudioBackendController.h"
#include "Audio/DirectSound/GameplayAudioCursorObservation.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

namespace gc::audio
{
    namespace
    {
        constexpr DWORD kSupportedSecondaryFlags =
            DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
            DSBCAPS_CTRLPOSITIONNOTIFY |
            DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;

        constexpr DWORD kObservedStaticFlags =
            DSBCAPS_STATIC | DSBCAPS_CTRLVOLUME |
            DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCDEFER;

        std::atomic_uint64_t g_next_buffer_instance_id{1};

        [[noreturn]] void ExactInvariantFatal() noexcept
        {
            std::abort();
        }

        std::uint64_t NextBufferInstanceId() noexcept
        {
            auto current = g_next_buffer_instance_id.load(
                std::memory_order_seq_cst);
            for (;;)
            {
                if (current == 0 ||
                    current == std::numeric_limits<std::uint64_t>::max())
                {
                    ExactInvariantFatal();
                }
                if (g_next_buffer_instance_id.compare_exchange_weak(
                    current,
                    current + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_seq_cst))
                {
                    return current;
                }
            }
        }

        WAVEFORMATEX GamePrimaryWaveFormat() noexcept
        {
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

        std::uint64_t NextPlaybackGeneration(
            std::uint64_t generation,
            bool exact_history_enabled) noexcept
        {
            if (exact_history_enabled &&
                generation == std::numeric_limits<std::uint64_t>::max())
            {
                ExactInvariantFatal();
            }
            return generation == std::numeric_limits<std::uint64_t>::max()
                       ? 1
                       : generation + 1;
        }
    } // namespace

    HRESULT CreateDirectSoundDevice(
        IAudioEngineController& engine,
        IDirectSound8** result) noexcept
    {
        if (result == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        *result = nullptr;

        auto* device = new(std::nothrow) DirectSoundDevice(engine);
        if (device == nullptr)
        {
            return DSERR_OUTOFMEMORY;
        }
        *result = device;
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::QueryInterface(
        REFIID interface_id,
        void** result) noexcept
    {
        if (result == nullptr)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (!IsEqualIID(interface_id, IID_IUnknown) &&
            !IsEqualIID(interface_id, IID_IDirectSoundBuffer) &&
            !IsEqualIID(interface_id, IID_IDirectSoundBuffer8))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IDirectSoundBuffer8*>(this);
        AddRef();
        return S_OK;
    }

ULONG STDMETHODCALLTYPE PrimarySoundBuffer::AddRef() noexcept
    {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

ULONG STDMETHODCALLTYPE PrimarySoundBuffer::Release() noexcept
    {
        const auto remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
        {
            delete this;
        }
        return remaining;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetCaps(LPDSBCAPS caps) noexcept
    {
        if (caps == nullptr || caps->dwSize != sizeof(DSBCAPS))
        {
            return DSERR_INVALIDPARAM;
        }
        *caps = {
            .dwSize = sizeof(DSBCAPS),
            .dwFlags = DSBCAPS_PRIMARYBUFFER,
            .dwBufferBytes = 0,
            .dwUnlockTransferRate = 0,
            .dwPlayCpuOverhead = 0,
        };
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetCurrentPosition(
        LPDWORD play_cursor,
        LPDWORD write_cursor) noexcept
    {
        if (play_cursor == nullptr && write_cursor == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        if (play_cursor != nullptr)
        {
            *play_cursor = 0;
        }
        if (write_cursor != nullptr)
        {
            *write_cursor = 0;
        }
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetFormat(
        LPWAVEFORMATEX destination,
        DWORD allocated,
        LPDWORD written) noexcept
    {
        constexpr DWORD required = sizeof(WAVEFORMATEX);
        if (written != nullptr)
        {
            *written = required;
        }
        if (destination == nullptr)
        {
            return allocated == 0 && written != nullptr
                       ? DS_OK
                       : DSERR_INVALIDPARAM;
        }
        if (allocated < required)
        {
            return DSERR_INVALIDPARAM;
        }
        *destination = GamePrimaryWaveFormat();
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetVolume(LPLONG) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetPan(LPLONG) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetFrequency(LPDWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetStatus(LPDWORD status) noexcept
    {
        if (status == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        *status = 0;
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::Initialize(
        LPDIRECTSOUND,
        LPCDSBUFFERDESC) noexcept
    {
        return DSERR_ALREADYINITIALIZED;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::Lock(
        DWORD,
        DWORD,
        LPVOID*,
        LPDWORD,
        LPVOID*,
        LPDWORD,
        DWORD) noexcept
    {
        return DSERR_INVALIDCALL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::Play(DWORD, DWORD, DWORD) noexcept
    {
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::SetCurrentPosition(DWORD) noexcept
    {
        return DSERR_INVALIDCALL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::SetFormat(
        LPCWAVEFORMATEX format) noexcept
    {
        if (format == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        return IsExactGamePrimaryFormat(*format) ? DS_OK : DSERR_BADFORMAT;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::SetVolume(LONG) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::SetPan(LONG) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::SetFrequency(DWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::Stop() noexcept
    {
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::Unlock(
        LPVOID,
        DWORD,
        LPVOID,
        DWORD) noexcept
    {
        return DSERR_INVALIDCALL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::Restore() noexcept
    {
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::SetFX(
        DWORD,
        LPDSEFFECTDESC,
        LPDWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::AcquireResources(
        DWORD,
        DWORD,
        LPDWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE PrimarySoundBuffer::GetObjectInPath(
        REFGUID,
        DWORD,
        REFGUID,
        LPVOID*) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

    DirectSoundDevice::DirectSoundDevice(
        IAudioEngineController& engine) noexcept
        : engine_(engine)
    {
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::QueryInterface(
        REFIID interface_id,
        void** result) noexcept
    {
        if (result == nullptr)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (!IsEqualIID(interface_id, IID_IUnknown) &&
            !IsEqualIID(interface_id, IID_IDirectSound) &&
            !IsEqualIID(interface_id, IID_IDirectSound8))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IDirectSound8*>(this);
        AddRef();
        return S_OK;
    }

ULONG STDMETHODCALLTYPE DirectSoundDevice::AddRef() noexcept
    {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

ULONG STDMETHODCALLTYPE DirectSoundDevice::Release() noexcept
    {
        const auto remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
        {
            delete this;
        }
        return remaining;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::CreateSoundBuffer(
        LPCDSBUFFERDESC descriptor,
        LPDIRECTSOUNDBUFFER* result,
        LPUNKNOWN outer) noexcept
    {
        if (result == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        *result = nullptr;
        if (outer != nullptr)
        {
            return DSERR_NOAGGREGATION;
        }
        if (descriptor == nullptr || descriptor->dwSize != sizeof(DSBUFFERDESC))
        {
            return DSERR_INVALIDPARAM;
        }
        if (!priority_cooperative_level_.load(std::memory_order_acquire))
        {
            return DSERR_PRIOLEVELNEEDED;
        }

        if ((descriptor->dwFlags & DSBCAPS_PRIMARYBUFFER) != 0)
        {
            if (descriptor->dwFlags != DSBCAPS_PRIMARYBUFFER ||
                descriptor->dwBufferBytes != 0 ||
                descriptor->dwReserved != 0 ||
                descriptor->lpwfxFormat != nullptr ||
                !IsEqualGUID(descriptor->guid3DAlgorithm, GUID_NULL))
            {
                return DSERR_INVALIDPARAM;
            }
            auto* primary = new(std::nothrow) PrimarySoundBuffer();
            if (primary == nullptr)
            {
                return DSERR_OUTOFMEMORY;
            }
            *result = static_cast<IDirectSoundBuffer*>(primary);
            return DS_OK;
        }

        SecondarySoundBuffer* secondary{};
        const auto create_result = SecondarySoundBuffer::Create(
            engine_,
            *descriptor,
            &secondary);
        if (FAILED(create_result))
        {
            return create_result;
        }
        *result = static_cast<IDirectSoundBuffer*>(secondary);
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::GetCaps(LPDSCAPS) noexcept
    {
        return DSERR_UNSUPPORTED;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::DuplicateSoundBuffer(
        LPDIRECTSOUNDBUFFER,
        LPDIRECTSOUNDBUFFER*) noexcept
    {
        return DSERR_UNSUPPORTED;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::SetCooperativeLevel(
        HWND window,
        DWORD level) noexcept
    {
        if (window == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        if (level != DSSCL_PRIORITY)
        {
            return DSERR_PRIOLEVELNEEDED;
        }
        if (priority_cooperative_level_.load(std::memory_order_acquire))
        {
            return DS_OK;
        }
        if (FAILED(engine_.StartForWindow(window)))
        {
            return DSERR_NODRIVER;
        }
        priority_cooperative_level_.store(true, std::memory_order_release);
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::Compact() noexcept
    {
        return DSERR_UNSUPPORTED;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::GetSpeakerConfig(LPDWORD) noexcept
    {
        return DSERR_UNSUPPORTED;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::SetSpeakerConfig(DWORD) noexcept
    {
        return DSERR_UNSUPPORTED;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::Initialize(LPCGUID) noexcept
    {
        return DSERR_ALREADYINITIALIZED;
    }

HRESULT STDMETHODCALLTYPE DirectSoundDevice::VerifyCertification(LPDWORD) noexcept
    {
        return DSERR_UNSUPPORTED;
    }

    SecondarySoundBuffer::SecondarySoundBuffer(
        IAudioEngineServices& engine,
        DWORD flags,
        DWORD buffer_bytes,
        const NormalizedSourceFormat& format,
        std::shared_ptr<AudioSnapshot> snapshot,
        std::shared_ptr<AudioCursorTimeline> timeline,
        std::uint64_t buffer_instance_id) noexcept
        : engine_(engine),
          flags_(flags),
          buffer_bytes_(buffer_bytes),
          format_(format),
          snapshot_(std::move(snapshot)),
          timeline_(std::move(timeline)),
          buffer_instance_id_(buffer_instance_id)
    {
    }

    SecondarySoundBuffer::~SecondarySoundBuffer()
    {
        if (voice_ == nullptr)
        {
            return;
        }

        voice_.reset();
        // Pinned miniaudio authority: ma_node_uninit() first performs a full
        // detach, and miniaudio.h states that detach waits for local node
        // processing to finish. MixerVoice destruction therefore proves that the
        // sole audio writer is quiesced before Release becomes the next writer.
        if (timeline_->HasExactPlaybackHistory() &&
            !timeline_->CloseExactWriterAfterQuiescence())
        {
            ExactInvariantFatal();
        }
    }

    HRESULT SecondarySoundBuffer::Create(
        IAudioEngineServices& engine,
        const DSBUFFERDESC& descriptor,
        SecondarySoundBuffer** result) noexcept
    {
        if (result == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        *result = nullptr;

        if (descriptor.dwSize != sizeof(DSBUFFERDESC) ||
            descriptor.lpwfxFormat == nullptr ||
            descriptor.dwBufferBytes == 0 ||
            descriptor.dwReserved != 0 ||
            !IsEqualGUID(descriptor.guid3DAlgorithm, GUID_NULL) ||
            (descriptor.dwFlags & DSBCAPS_PRIMARYBUFFER) != 0)
        {
            return DSERR_INVALIDPARAM;
        }
        if ((descriptor.dwFlags & ~kSupportedSecondaryFlags) != 0)
        {
            return DSERR_CONTROLUNAVAIL;
        }

        NormalizedSourceFormat format{};
        const auto format_result = NormalizeSourceFormat(
            descriptor.lpwfxFormat,
            &format);
        if (FAILED(format_result))
        {
            return format_result;
        }
        if (format.block_align == 0 ||
            descriptor.dwBufferBytes % format.block_align != 0)
        {
            return DSERR_INVALIDPARAM;
        }

        try
        {
            const auto buffer_instance_id = NextBufferInstanceId();
            auto snapshot = std::make_shared<AudioSnapshot>(
                descriptor.dwBufferBytes,
                format.block_align);
            auto timeline = std::make_shared<AudioCursorTimeline>();
            auto* buffer = new(std::nothrow) SecondarySoundBuffer(
                engine,
                descriptor.dwFlags,
                descriptor.dwBufferBytes,
                format,
                std::move(snapshot),
                std::move(timeline),
                buffer_instance_id);
            if (buffer == nullptr)
            {
                return DSERR_OUTOFMEMORY;
            }
            if (!buffer->timeline_->AssignBufferInstanceId(
                buffer_instance_id))
            {
                delete buffer;
                ExactInvariantFatal();
            }

            const bool observed_usage =
                descriptor.dwFlags == kObservedStaticFlags ||
                descriptor.dwFlags ==
                (kObservedStaticFlags | DSBCAPS_CTRLPOSITIONNOTIFY);
            const auto usage = format.game_native_pcm16 && observed_usage
                                   ? VoiceUsage::GameplayNativeCandidate
                                   : VoiceUsage::General;
            ma_result voice_result = MA_ERROR;
            buffer->voice_ = engine.CreateVoice(
                format,
                buffer->snapshot_,
                buffer->timeline_,
                usage,
                &voice_result);
            if (buffer->voice_ == nullptr || voice_result != MA_SUCCESS)
            {
                delete buffer;
                return DSERR_OUTOFMEMORY;
            }

            *result = buffer;
            return DS_OK;
        }
        catch (const std::bad_alloc&)
        {
            return DSERR_OUTOFMEMORY;
        }
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::QueryInterface(
        REFIID interface_id,
        void** result) noexcept
    {
        if (result == nullptr)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (!IsEqualIID(interface_id, IID_IUnknown) &&
            !IsEqualIID(interface_id, IID_IDirectSoundBuffer) &&
            !IsEqualIID(interface_id, IID_IDirectSoundBuffer8))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IDirectSoundBuffer8*>(this);
        AddRef();
        return S_OK;
    }

ULONG STDMETHODCALLTYPE SecondarySoundBuffer::AddRef() noexcept
    {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

ULONG STDMETHODCALLTYPE SecondarySoundBuffer::Release() noexcept
    {
        const auto remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
        {
            delete this;
        }
        return remaining;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetCaps(
        LPDSBCAPS caps) noexcept
    {
        if (caps == nullptr || caps->dwSize != sizeof(DSBCAPS))
        {
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

    std::uint64_t
    SecondarySoundBuffer::ResolveCurrentSourceFrameLocked() noexcept
    {
        const auto last = last_reported_source_frame_;
        const auto has_exact_history = timeline_->HasExactPlaybackHistory();
        const auto publish_exact = [this, has_exact_history](
            GameplayAudioCursorState state,
            std::uint64_t output_frame,
            std::uint64_t source_frame_unwrapped) noexcept
        {
            PublishGameplayAudioCursorObservation({
                .state = state,
                .source_frame_unwrapped = source_frame_unwrapped,
                .source_sample_rate = format_.sample_rate,
                .buffer_instance_id = buffer_instance_id_,
                .timeline_generation = has_exact_history
                                           ? timeline_->exact_timeline_generation()
                                           : 0,
                .playback_generation = playback_generation_,
                .origin = playback_origin_,
                .output_frame = output_frame,
                .exact_history = has_exact_history ? timeline_ : nullptr,
            });
        };
        const auto mixing = voice_->playing();
        const auto audible_until = voice_->audible_until_output_frame();
        if (!mixing && !audible_until.has_value())
        {
            publish_exact(GameplayAudioCursorState::Inactive, 0, last);
            return last;
        }

        const auto output_frame = engine_.CurrentOutputFrame();
        if (!output_frame.has_value())
        {
            if (has_exact_history)
            {
                publish_exact(GameplayAudioCursorState::Pending, 0, last);
            }
            return last;
        }
        const auto draining = audible_until.has_value() &&
            *output_frame < *audible_until;
        if (!mixing && !draining)
        {
            publish_exact(
                GameplayAudioCursorState::Inactive, *output_frame, last);
            return last;
        }
        GameplayAudioCursorState exact_state = GameplayAudioCursorState::Pending;
        if (has_exact_history &&
            *output_frame <= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()))
        {
            const auto exact = timeline_->ResolveExactSourceFrame(
                gc::timing::CheckedRational::Whole(
                    static_cast<std::int64_t>(*output_frame)));
            if (exact.status == ExactClockStatus::Resolved &&
                exact.playback_generation == playback_generation_)
            {
                exact_state = GameplayAudioCursorState::Exact;
            }
        }
        const auto resolution = timeline_->ResolveSourceFrame(
            *output_frame,
            playback_generation_,
            buffer_bytes_ / format_.block_align);
        if (resolution.kind == AudioCursorResolutionKind::PendingGeneration)
        {
            engine_.CountPendingCursorQuery();
            if (has_exact_history)
            {
                publish_exact(exact_state, *output_frame, last);
            }
            return last;
        }
        if (resolution.kind == AudioCursorResolutionKind::Unmapped)
        {
            engine_.CountUnmappedCursorFailure();
            if (has_exact_history)
            {
                publish_exact(exact_state, *output_frame, last);
            }
            return last;
        }
        last_reported_source_frame_ = resolution.source_frame;
        publish_exact(
            has_exact_history ? exact_state : GameplayAudioCursorState::Exact,
            *output_frame,
            resolution.source_frame_unwrapped);
        return resolution.source_frame;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetCurrentPosition(
        LPDWORD play_cursor,
        LPDWORD write_cursor) noexcept
    {
        if (play_cursor == nullptr && write_cursor == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        std::lock_guard control_lock(control_mutex_);
        const auto source_frame = ResolveCurrentSourceFrameLocked();
        if (play_cursor != nullptr)
        {
            *play_cursor = static_cast<DWORD>(
                SourceFrameToByte(source_frame, format_.block_align));
        }
        if (write_cursor != nullptr)
        {
            const auto write_frame = ProjectWriteCursorFrame(
                source_frame,
                engine_.endpoint_buffer_frames(),
                engine_.output_sample_rate(),
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
        LPDWORD written) noexcept
    {
        const auto required = format_.wave_format_size;
        if (written != nullptr)
        {
            *written = required;
        }
        if (destination == nullptr)
        {
            return allocated == 0 && written != nullptr
                       ? DS_OK
                       : DSERR_INVALIDPARAM;
        }
        if (allocated < required)
        {
            return DSERR_INVALIDPARAM;
        }
        std::memcpy(destination, &format_.wave, required);
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetVolume(LPLONG volume) noexcept
    {
        if (volume == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        *volume = volume_.load(std::memory_order_acquire);
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetPan(LPLONG) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetFrequency(LPDWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetStatus(LPDWORD status) noexcept
    {
        if (status == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }
        std::lock_guard control_lock(control_mutex_);
        *status = 0;
        auto audible = voice_->playing();
        if (!audible)
        {
            const auto audible_until = voice_->audible_until_output_frame();
            if (audible_until.has_value())
            {
                const auto output_frame = engine_.CurrentOutputFrame();
                if (output_frame.has_value())
                {
                    audible = *output_frame < *audible_until;
                }
                else
                {
                    audible = true;
                }
            }
        }
        if (audible)
        {
            *status |= DSBSTATUS_PLAYING;
            if (voice_->looping())
            {
                *status |= DSBSTATUS_LOOPING;
            }
        }
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Initialize(
        LPDIRECTSOUND,
        LPCDSBUFFERDESC) noexcept
    {
        return DSERR_ALREADYINITIALIZED;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Lock(
        DWORD offset,
        DWORD byte_count,
        LPVOID* first,
        LPDWORD first_bytes,
        LPVOID* second,
        LPDWORD second_bytes,
        DWORD flags) noexcept
    {
        if (first == nullptr || first_bytes == nullptr ||
            (second == nullptr) != (second_bytes == nullptr))
        {
            return DSERR_INVALIDPARAM;
        }
        *first = nullptr;
        *first_bytes = 0;
        if (second != nullptr)
        {
            *second = nullptr;
            *second_bytes = 0;
        }

        DWORD effective_offset = offset;
        DWORD effective_count = byte_count;
        if ((flags & DSBLOCK_ENTIREBUFFER) != 0)
        {
            effective_offset = 0;
            effective_count = buffer_bytes_;
        }
        const bool valid_range =
            (flags & ~DSBLOCK_ENTIREBUFFER) == 0 &&
            effective_offset < buffer_bytes_ && effective_count != 0 &&
            effective_count <= buffer_bytes_;
        const bool wraps = valid_range &&
            effective_count > buffer_bytes_ - effective_offset;
        if (wraps && second == nullptr)
        {
            return DSERR_INVALIDPARAM;
        }

        AudioLockRegions regions{};
        const auto result = snapshot_->Lock(
            offset,
            byte_count,
            flags,
            &regions);
        if (FAILED(result))
        {
            return result;
        }
        *first = regions.first;
        *first_bytes = regions.first_bytes;
        if (second != nullptr)
        {
            *second = regions.second;
            *second_bytes = regions.second_bytes;
        }
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Play(
        DWORD reserved,
        DWORD priority,
        DWORD flags) noexcept
    {
        if (reserved != 0 || priority != 0 ||
            (flags & ~DSBPLAY_LOOPING) != 0)
        {
            return DSERR_INVALIDPARAM;
        }
        std::lock_guard control_lock(control_mutex_);
        const auto generation = NextPlaybackGeneration(
            playback_generation_, timeline_->HasExactPlaybackHistory());
        const auto anchor = voice_->at_end()
                                ? std::uint64_t{0}
                                : last_reported_source_frame_;
        const auto result = voice_->Play(
            (flags & DSBPLAY_LOOPING) != 0,
            generation);
        if (SUCCEEDED(result))
        {
            playback_generation_ = generation;
            playback_origin_ = ExactPlaybackOrigin::Play;
            last_reported_source_frame_ = anchor;
            if (timeline_->HasExactPlaybackHistory() &&
                !timeline_->ExpectExactPlaybackGeneration(generation))
            {
                ExactInvariantFatal();
            }
        }
        return result;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetCurrentPosition(
        DWORD position) noexcept
    {
        if (format_.block_align == 0 || position >= buffer_bytes_ ||
            position % format_.block_align != 0)
        {
            return DSERR_INVALIDPARAM;
        }
        std::lock_guard control_lock(control_mutex_);
        const auto source_frame = position / format_.block_align;
        const auto generation = NextPlaybackGeneration(
            playback_generation_, timeline_->HasExactPlaybackHistory());
        const auto result = voice_->Seek(source_frame, generation);
        if (SUCCEEDED(result))
        {
            playback_generation_ = generation;
            playback_origin_ = ExactPlaybackOrigin::Seek;
            last_reported_source_frame_ = source_frame;
            if (timeline_->HasExactPlaybackHistory() &&
                !timeline_->ExpectExactPlaybackGeneration(generation))
            {
                ExactInvariantFatal();
            }
        }
        return result;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetFormat(
        LPCWAVEFORMATEX) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetVolume(LONG volume) noexcept
    {
        if (volume < DSBVOLUME_MIN || volume > DSBVOLUME_MAX)
        {
            return DSERR_INVALIDPARAM;
        }
        volume_.store(volume, std::memory_order_release);
        voice_->SetGain(DirectSoundVolumeToLinearGain(volume));
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetPan(LONG) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetFrequency(DWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Stop() noexcept
    {
        std::lock_guard control_lock(control_mutex_);
        ResolveCurrentSourceFrameLocked();
        voice_->Stop();
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Unlock(
        LPVOID first,
        DWORD first_bytes,
        LPVOID second,
        DWORD second_bytes) noexcept
    {
        return snapshot_->Unlock(
            first,
            first_bytes,
            second,
            second_bytes);
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::Restore() noexcept
    {
        snapshot_->ReclaimRetired();
        return DS_OK;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::SetFX(
        DWORD,
        LPDSEFFECTDESC,
        LPDWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::AcquireResources(
        DWORD,
        DWORD,
        LPDWORD) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }

HRESULT STDMETHODCALLTYPE SecondarySoundBuffer::GetObjectInPath(
        REFGUID,
        DWORD,
        REFGUID,
        LPVOID*) noexcept
    {
        return DSERR_CONTROLUNAVAIL;
    }
} // namespace gc::audio
