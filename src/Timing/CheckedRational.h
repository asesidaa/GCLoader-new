#pragma once

#include <cstdint>
#include <expected>

namespace gc::timing {

enum class RationalError : std::uint8_t {
    ZeroDenominator,
    Overflow,
    DivisionByZero,
};

class CheckedRational final {
public:
    static std::expected<CheckedRational, RationalError> Create(
            std::int64_t numerator,
            std::uint64_t denominator) noexcept;
    static CheckedRational Whole(std::int64_t value) noexcept;

    [[nodiscard]] std::int64_t numerator() const noexcept;
    [[nodiscard]] std::uint64_t denominator() const noexcept;
    [[nodiscard]] int Compare(const CheckedRational&) const noexcept;
    [[nodiscard]] std::expected<CheckedRational, RationalError>
    Add(const CheckedRational&) const noexcept;
    [[nodiscard]] std::expected<CheckedRational, RationalError>
    Subtract(const CheckedRational&) const noexcept;
    [[nodiscard]] std::expected<CheckedRational, RationalError>
    Multiply(std::int64_t numerator,
             std::uint64_t denominator) const noexcept;
    [[nodiscard]] std::expected<std::int64_t, RationalError>
    Floor() const noexcept;
    [[nodiscard]] std::expected<std::int64_t, RationalError>
    Ceil() const noexcept;
    [[nodiscard]] std::expected<std::int64_t, RationalError>
    Truncate() const noexcept;

private:
    CheckedRational(std::int64_t numerator,
                    std::uint64_t denominator) noexcept;

    std::int64_t numerator_;
    std::uint64_t denominator_;
};

}
