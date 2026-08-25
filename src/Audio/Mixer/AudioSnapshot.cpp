#include "Audio/Mixer/AudioSnapshot.h"

#include <dsound.h>

#include <algorithm>
#include <new>
#include <utility>

namespace gc::audio {

AudioSnapshot::RenderView::RenderView(
    const AudioSnapshot* owner,
    const Snapshot* snapshot) noexcept
    : owner_(owner), snapshot_(snapshot) {}

AudioSnapshot::RenderView::~RenderView() {
    Release();
}

std::span<const std::byte>
AudioSnapshot::RenderView::bytes() const noexcept {
    if (snapshot_ == nullptr) {
        return {};
    }
    return {snapshot_->bytes.data(), snapshot_->bytes.size()};
}

std::size_t AudioSnapshot::RenderView::size() const noexcept {
    return snapshot_ == nullptr ? 0 : snapshot_->bytes.size();
}

std::uint64_t AudioSnapshot::RenderView::generation() const noexcept {
    return snapshot_ == nullptr ? 0 : snapshot_->generation;
}

void AudioSnapshot::RenderView::Release() noexcept {
    if (owner_ != nullptr) {
        owner_->render_hazard_.store(nullptr, std::memory_order_seq_cst);
        owner_ = nullptr;
        snapshot_ = nullptr;
    }
}

AudioSnapshot::AudioSnapshot(
    std::uint32_t byte_length,
    std::uint16_t block_align)
    : byte_length_(byte_length),
      block_align_(block_align),
      published_owner_(std::make_unique<Snapshot>()) {
    published_owner_->bytes.resize(byte_length_);
    published_.store(published_owner_.get(), std::memory_order_seq_cst);
}

HRESULT AudioSnapshot::Lock(
    DWORD offset,
    DWORD byte_count,
    DWORD flags,
    AudioLockRegions* regions) noexcept {
    std::lock_guard lock(writer_mutex_);
    if (outstanding_ != nullptr) {
        return DSERR_ALLOCATED;
    }
    if (regions == nullptr) {
        return DSERR_INVALIDPARAM;
    }

    constexpr DWORD supported_flags = DSBLOCK_ENTIREBUFFER;
    if ((flags & ~supported_flags) != 0) {
        return DSERR_INVALIDPARAM;
    }
    if ((flags & DSBLOCK_ENTIREBUFFER) != 0) {
        offset = 0;
        byte_count = byte_length_;
    }
    if (block_align_ == 0 || byte_length_ == 0 ||
        offset >= byte_length_ || byte_count == 0 ||
        byte_count > byte_length_ || offset % block_align_ != 0 ||
        byte_count % block_align_ != 0) {
        return DSERR_INVALIDPARAM;
    }

    ReclaimRetiredLocked();

    try {
        retired_.reserve(retired_.size() + 1);

        auto writable = std::make_unique<WritableLock>();
        writable->snapshot = std::make_unique<Snapshot>();
        writable->snapshot->bytes = published_owner_->bytes;
        writable->snapshot->generation =
            published_owner_->generation + 1;

        const DWORD first_bytes = std::min<DWORD>(
            byte_count, byte_length_ - offset);
        const DWORD second_bytes = byte_count - first_bytes;
        auto* data = writable->snapshot->bytes.data();
        writable->regions = {
            data + offset,
            first_bytes,
            second_bytes == 0 ? nullptr : data,
            second_bytes,
        };

        *regions = writable->regions;
        outstanding_ = std::move(writable);
    } catch (const std::bad_alloc&) {
        return DSERR_OUTOFMEMORY;
    }

    return DS_OK;
}

HRESULT AudioSnapshot::Unlock(
    const void* first,
    DWORD first_bytes,
    const void* second,
    DWORD second_bytes) noexcept {
    std::lock_guard lock(writer_mutex_);
    if (outstanding_ == nullptr) {
        return DSERR_INVALIDPARAM;
    }

    const auto& expected = outstanding_->regions;
    if (first != expected.first || first_bytes != expected.first_bytes ||
        second != expected.second || second_bytes != expected.second_bytes) {
        return DSERR_INVALIDPARAM;
    }

    ReclaimRetiredLocked();
    auto old = std::move(published_owner_);
    published_owner_ = std::move(outstanding_->snapshot);
    outstanding_.reset();
    published_.store(published_owner_.get(), std::memory_order_seq_cst);
    retired_.push_back(std::move(old));
    ReclaimRetiredLocked();
    return DS_OK;
}

AudioSnapshot::RenderView AudioSnapshot::AcquireForRender() const noexcept {
    const auto* candidate = detail::CaptureSingleReaderSnapshot(
        published_, render_hazard_);
    if (candidate == nullptr) {
        return {};
    }
    return RenderView(this, candidate);
}

void AudioSnapshot::ReclaimRetired() noexcept {
    std::lock_guard lock(writer_mutex_);
    ReclaimRetiredLocked();
}

std::uint32_t AudioSnapshot::byte_length() const noexcept {
    return byte_length_;
}

std::uint64_t AudioSnapshot::generation() const noexcept {
    std::lock_guard lock(writer_mutex_);
    return published_owner_->generation;
}

void AudioSnapshot::ReclaimRetiredLocked() noexcept {
    const auto* hazard = render_hazard_.load(std::memory_order_seq_cst);
    std::erase_if(
        retired_,
        [hazard](const std::unique_ptr<Snapshot>& snapshot) {
            return snapshot.get() != hazard;
        });
}

} // namespace gc::audio
