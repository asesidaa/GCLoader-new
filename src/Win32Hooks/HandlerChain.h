#pragma once
#include "Win32Hooks/HookDecision.h"
#include <array>
#include <cstddef>
#include <expected>
#include <optional>

namespace gc::win32_hooks {
template <typename Context, typename Result>
using PreCallHandler = PreCallDecision<Result> (*)(void*, Context&) noexcept;
template <typename Context>
using BeforeOriginalObserver = void (*)(void*, const Context&, ObservationState&) noexcept;
template <typename Context, typename Result>
using PostCallObserver = void (*)(void*, const Context&, const CallOutcome<Result>&,
                                 const ObservationState&) noexcept;

// Capacity counts actual registrations (pre + post); observer preparation is
// paired with its post callback so timing starts only after routing succeeds.
template <typename Context, typename Result, std::size_t Capacity>
class HandlerChain final {
public:
    HandlerChain() = default;
    HandlerChain(const HandlerChain&) = delete;
    HandlerChain& operator=(const HandlerChain&) = delete;
    [[nodiscard]] std::expected<void, RegistrationError> AddPre(
        HandlerIdentity identity, void* state, PreCallHandler<Context, Result> callback) noexcept {
        if (const auto error = Check(identity, callback != nullptr); !error) return error;
        pre_[pre_count_++] = {identity, state, callback};
        return {};
    }
    [[nodiscard]] std::expected<void, RegistrationError> AddPost(
        HandlerIdentity identity, void* state, BeforeOriginalObserver<Context> before,
        PostCallObserver<Context, Result> after) noexcept {
        if (const auto error = Check(identity, before && after); !error) return error;
        post_[post_count_++] = {identity, state, before, after};
        return {};
    }
    [[nodiscard]] std::expected<void, RegistrationError> Publish() noexcept {
        if (published_) return std::unexpected(RegistrationError{RegistrationStage::published, {}});
        published_ = true;
        return {};
    }
    [[nodiscard]] bool published() const noexcept { return published_; }
    [[nodiscard]] bool empty() const noexcept { return pre_count_ + post_count_ == 0; }

    template <class Original>
    [[nodiscard]] Result Dispatch(Context& context, DWORD incoming_error, Original&& original) const {
        std::optional<CallOutcome<Result>> outcome;
        for (std::size_t i = 0; i < pre_count_; ++i) {
            SetLastError(incoming_error);
            const auto decision = pre_[i].callback(pre_[i].state, context);
            if (const auto* completed = std::get_if<CompleteCall<Result>>(&decision)) {
                outcome.emplace(completed->result, completed->last_error);
                break;
            }
        }
        std::array<ObservationState, Capacity> observations{};
        if (!outcome) {
            for (std::size_t i = 0; i < post_count_; ++i)
                post_[i].before(post_[i].state, context, observations[i]);
            SetLastError(incoming_error);
            const Result result = original();
            const DWORD last_error = GetLastError();
            outcome.emplace(result, last_error);
        }
        // Observations for completed calls remain inactive. Observers cannot
        // mutate arguments, caller outputs, result or the captured final error.
        for (std::size_t i = 0; i < post_count_; ++i)
            post_[i].after(post_[i].state, context, *outcome, observations[i]);
        SetLastError(outcome->last_error);
        return outcome->result;
    }
private:
    [[nodiscard]] std::expected<void, RegistrationError> Check(
        HandlerIdentity identity, bool callback_valid) const noexcept {
        if (published_) return std::unexpected(RegistrationError{RegistrationStage::published, identity});
        if (!callback_valid || identity.feature.empty() || identity.site.empty())
            return std::unexpected(RegistrationError{RegistrationStage::invalid_handler, identity});
        for (std::size_t i = 0; i < pre_count_; ++i)
            if (pre_[i].identity == identity)
                return std::unexpected(RegistrationError{RegistrationStage::duplicate, identity});
        for (std::size_t i = 0; i < post_count_; ++i)
            if (post_[i].identity == identity)
                return std::unexpected(RegistrationError{RegistrationStage::duplicate, identity});
        if (pre_count_ + post_count_ == Capacity)
            return std::unexpected(RegistrationError{RegistrationStage::capacity, identity});
        return {};
    }
    struct Pre final { HandlerIdentity identity; void* state{}; PreCallHandler<Context, Result> callback{}; };
    struct Post final {
        HandlerIdentity identity; void* state{};
        BeforeOriginalObserver<Context> before{};
        PostCallObserver<Context, Result> after{};
    };
    std::array<Pre, Capacity> pre_{};
    std::array<Post, Capacity> post_{};
    std::size_t pre_count_{};
    std::size_t post_count_{};
    bool published_{};
};
}
