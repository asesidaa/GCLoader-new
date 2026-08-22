#include "Audio/ExactOutputClock.h"

#include <atomic>
#include <limits>
#include <mutex>

namespace gc::audio {

namespace {

constexpr std::memory_order kExactClockAtomicOrder =
    std::memory_order_seq_cst;

struct ActiveExactOutputClockRegistry final {
    std::mutex mutex;
    std::weak_ptr<ExactOutputClock> provider;
    std::uint64_t endpoint_generation{};
};

ActiveExactOutputClockRegistry g_active_provider;
std::atomic<std::uint64_t> g_next_endpoint_generation{1};

} // namespace

std::string_view ExactOutputClockDomainName(
    ExactOutputClockDomain domain) noexcept {
    switch (domain) {
    case ExactOutputClockDomain::WasapiQpc:
        return "wasapi_qpc";
    case ExactOutputClockDomain::AsioMultimediaMilliseconds:
        return "asio_multimedia_ms";
    default:
        return "invalid";
    }
}

std::shared_ptr<const ExactOutputClock>
AcquireExactOutputClock() noexcept {
    std::lock_guard lock(g_active_provider.mutex);
    auto provider = g_active_provider.provider.lock();
    if (provider == nullptr ||
        provider->info().endpoint_generation !=
            g_active_provider.endpoint_generation) {
        return nullptr;
    }
    return provider;
}

namespace detail {

std::uint64_t NextExactOutputClockGeneration() noexcept {
    auto generation =
        g_next_endpoint_generation.load(kExactClockAtomicOrder);
    for (;;) {
        if (generation == 0 ||
            generation == std::numeric_limits<std::uint64_t>::max()) {
            return 0;
        }
        if (g_next_endpoint_generation.compare_exchange_weak(
                generation,
                generation + 1,
                kExactClockAtomicOrder,
                kExactClockAtomicOrder)) {
            return generation;
        }
    }
}

bool RegisterExactOutputClock(
    const std::shared_ptr<ExactOutputClock>& provider) noexcept {
    if (provider == nullptr) {
        return false;
    }
    const auto generation = provider->info().endpoint_generation;
    if (generation == 0) {
        return false;
    }

    std::lock_guard lock(g_active_provider.mutex);
    auto previous = g_active_provider.provider.lock();
    if (g_active_provider.endpoint_generation == generation) {
        return previous == provider;
    }
    if (previous != nullptr) {
        previous->Invalidate();
    }
    g_active_provider.provider = provider;
    g_active_provider.endpoint_generation = generation;
    return true;
}

void UnregisterExactOutputClock(
    std::uint64_t expected_generation) noexcept {
    std::lock_guard lock(g_active_provider.mutex);
    if (g_active_provider.endpoint_generation != expected_generation) {
        return;
    }
    g_active_provider.provider.reset();
    g_active_provider.endpoint_generation = 0;
}

} // namespace detail

} // namespace gc::audio
