#pragma once

namespace gc::input {

struct ForegroundTransition {
    bool changed{};
    bool clear_input{};
};

class ForegroundTransitionTracker {
public:
    [[nodiscard]] ForegroundTransition Update(bool foreground) noexcept;

private:
    bool foreground_{true};
};

} // namespace gc::input
