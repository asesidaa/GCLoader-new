#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace gc::input {

class DigitalLatch {
public:
    static std::expected<DigitalLatch, std::string> Create(
        std::uint32_t press_percent,
        std::uint32_t release_percent) noexcept;

    bool Update(double activation) noexcept;
    void Reset() noexcept;
    bool pressed() const noexcept;

private:
    DigitalLatch(double press_threshold, double release_threshold) noexcept;

    double press_threshold_{};
    double release_threshold_{};
    bool pressed_{};
};

} // namespace gc::input
