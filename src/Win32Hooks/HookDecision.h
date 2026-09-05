#pragma once
#include <Windows.h>
#include <cstdint>
#include <string_view>
#include <variant>

namespace gc::win32_hooks {
struct ContinueCall final {};
template <typename Result>
struct CompleteCall final { Result result{}; DWORD last_error{}; };
template <typename Result>
using PreCallDecision = std::variant<ContinueCall, CompleteCall<Result>>;
template <typename Result>
struct CallOutcome final { const Result result{}; const DWORD last_error{}; };
struct HandlerIdentity final {
    std::string_view feature;
    std::string_view site;
    friend bool operator==(const HandlerIdentity&, const HandlerIdentity&) = default;
};
enum class RegistrationStage { invalid_handler, duplicate, capacity, published };
struct RegistrationError final { RegistrationStage stage{}; HandlerIdentity identity; };
// Per-call observer data belongs to the dispatch stack, never a shared handler.
struct ObservationState final { bool active{}; std::uint64_t started_ms{}; };

template <class Result, class Callback>
[[nodiscard]] PreCallDecision<Result> GuardPreCall(Result failure, Callback&& callback) noexcept {
    try { return callback(); }
    catch (...) { return CompleteCall<Result>{failure, ERROR_UNHANDLED_EXCEPTION}; }
}
}
