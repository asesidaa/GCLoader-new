#pragma once

#include <Windows.h>
#include <dsound.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace gc::audio {

namespace detail {

template <typename PublishedPointer, typename HazardPointer>
auto CaptureSingleReaderSnapshot(
    PublishedPointer& published,
    HazardPointer& hazard) noexcept
    -> decltype(published.load(std::memory_order_seq_cst)) {
    using Pointer = decltype(
        published.load(std::memory_order_seq_cst));

    for (;;) {
        const Pointer candidate =
            published.load(std::memory_order_seq_cst);
        Pointer empty = nullptr;
        if (!hazard.compare_exchange_strong(
                empty,
                candidate,
                std::memory_order_seq_cst,
                std::memory_order_seq_cst)) {
            return nullptr;
        }
        if (candidate == published.load(std::memory_order_seq_cst)) {
            return candidate;
        }
        hazard.store(nullptr, std::memory_order_seq_cst);
    }
}

} // namespace detail

struct AudioLockRegions {
    void* first{};
    DWORD first_bytes{};
    void* second{};
    DWORD second_bytes{};
};

class AudioSnapshot final {
private:
    struct Snapshot {
        std::vector<std::byte> bytes;
        std::uint64_t generation{};
    };

    struct WritableLock {
        std::unique_ptr<Snapshot> snapshot;
        AudioLockRegions regions;
    };

public:
    class RenderView final {
    public:
        RenderView() noexcept = default;
        ~RenderView();

        RenderView(const RenderView&) = delete;
        RenderView& operator=(const RenderView&) = delete;
        RenderView(RenderView&&) = delete;
        RenderView& operator=(RenderView&&) = delete;

        std::span<const std::byte> bytes() const noexcept;
        std::size_t size() const noexcept;
        std::uint64_t generation() const noexcept;

    private:
        friend class AudioSnapshot;

        RenderView(
            const AudioSnapshot*,
            const Snapshot*) noexcept;
        void Release() noexcept;

        const AudioSnapshot* owner_{};
        const Snapshot* snapshot_{};
    };

    AudioSnapshot(std::uint32_t byte_length, std::uint16_t block_align);

    HRESULT Lock(
        DWORD offset,
        DWORD byte_count,
        DWORD flags,
        AudioLockRegions* regions) noexcept;
    HRESULT Unlock(
        void* first,
        DWORD first_bytes,
        void* second,
        DWORD second_bytes) noexcept;
    RenderView AcquireForRender() const noexcept;
    void ReclaimRetired() noexcept;
    std::uint32_t byte_length() const noexcept;
    std::uint64_t generation() const noexcept;

private:
    void ReclaimRetiredLocked() noexcept;

    const std::uint32_t byte_length_;
    const std::uint16_t block_align_;
    mutable std::atomic<const Snapshot*> render_hazard_{};
    std::atomic<const Snapshot*> published_{};
    mutable std::mutex writer_mutex_;
    std::unique_ptr<Snapshot> published_owner_;
    std::unique_ptr<WritableLock> outstanding_;
    std::vector<std::unique_ptr<Snapshot>> retired_;
};

} // namespace gc::audio
