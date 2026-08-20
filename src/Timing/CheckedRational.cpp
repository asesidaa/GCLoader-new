#include "Timing/CheckedRational.h"

#include <limits>
#include <numeric>
#include <optional>

/*
 * Formal review note:
 *
 * Every CheckedRational is canonical: its denominator is positive, zero is
 * 0/1, and every nonzero numerator magnitude is coprime to its denominator.
 * Negative magnitudes are formed and reduced in uint64_t, so INT64_MIN is
 * never negated in the signed domain; magnitude 2^63 is explicitly restored
 * as INT64_MIN.
 *
 * For positive fractions a/b and c/d, write a = qb + r and c = q'd + s.
 * Unequal q and q' decide the comparison.  If q == q' and r,s are nonzero,
 * r/b ? s/d exactly when b/r has the reverse relation to d/s.  Iterating that
 * quotient/remainder identity compares the fractions without cross-products.
 */

namespace gc::timing {
namespace {

constexpr std::uint64_t kNegativeMagnitudeLimit =
        std::uint64_t{1} << 63;

std::uint64_t UnsignedMagnitude(const std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return std::uint64_t{0} - static_cast<std::uint64_t>(value);
}

std::optional<std::int64_t> SignedFromMagnitude(
        const std::uint64_t magnitude,
        const bool negative) noexcept {
    if (!negative) {
        if (magnitude >
            static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(magnitude);
    }

    if (magnitude == kNegativeMagnitudeLimit) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (magnitude < kNegativeMagnitudeLimit) {
        return -static_cast<std::int64_t>(magnitude);
    }
    return std::nullopt;
}

std::optional<std::uint64_t> CheckedMultiplyUnsigned(
        const std::uint64_t left,
        const std::uint64_t right) noexcept {
    if (left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::nullopt;
    }
    return left * right;
}

std::optional<std::int64_t> CheckedMultiplySignedUnsigned(
        const std::int64_t left,
        const std::uint64_t right) noexcept {
    const bool negative = left < 0;
    const std::uint64_t magnitude = UnsignedMagnitude(left);
    const std::uint64_t limit = negative
            ? kNegativeMagnitudeLimit
            : static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max());
    if (magnitude != 0 && right > limit / magnitude) {
        return std::nullopt;
    }
    return SignedFromMagnitude(magnitude * right, negative);
}

std::optional<std::int64_t> CheckedAddSigned(
        const std::int64_t left,
        const std::int64_t right) noexcept {
    if (right > 0 &&
        left > std::numeric_limits<std::int64_t>::max() - right) {
        return std::nullopt;
    }
    if (right < 0 &&
        left < std::numeric_limits<std::int64_t>::min() - right) {
        return std::nullopt;
    }
    return left + right;
}

std::optional<std::int64_t> CheckedSubtractSigned(
        const std::int64_t left,
        const std::int64_t right) noexcept {
    if (right > 0 &&
        left < std::numeric_limits<std::int64_t>::min() + right) {
        return std::nullopt;
    }
    if (right < 0 &&
        left > std::numeric_limits<std::int64_t>::max() + right) {
        return std::nullopt;
    }
    return left - right;
}

int ComparePositiveFractions(std::uint64_t leftNumerator,
                             std::uint64_t leftDenominator,
                             std::uint64_t rightNumerator,
                             std::uint64_t rightDenominator) noexcept {
    bool reverse = false;
    for (;;) {
        const std::uint64_t leftQuotient =
                leftNumerator / leftDenominator;
        const std::uint64_t rightQuotient =
                rightNumerator / rightDenominator;
        if (leftQuotient != rightQuotient) {
            const int comparison = leftQuotient < rightQuotient ? -1 : 1;
            return reverse ? -comparison : comparison;
        }

        const std::uint64_t leftRemainder =
                leftNumerator % leftDenominator;
        const std::uint64_t rightRemainder =
                rightNumerator % rightDenominator;
        if (leftRemainder == 0 || rightRemainder == 0) {
            if (leftRemainder == rightRemainder) {
                return 0;
            }
            const int comparison = leftRemainder == 0 ? -1 : 1;
            return reverse ? -comparison : comparison;
        }

        leftNumerator = leftDenominator;
        leftDenominator = leftRemainder;
        rightNumerator = rightDenominator;
        rightDenominator = rightRemainder;
        reverse = !reverse;
    }
}

struct TruncatedParts final {
    std::int64_t quotient;
    bool hasRemainder;
};

TruncatedParts DivideTowardZero(const std::int64_t numerator,
                                const std::uint64_t denominator) noexcept {
    if (denominator <=
        static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        const auto signedDenominator =
                static_cast<std::int64_t>(denominator);
        return {
                numerator / signedDenominator,
                numerator % signedDenominator != 0,
        };
    }

    const std::uint64_t magnitude = UnsignedMagnitude(numerator);
    const std::uint64_t quotientMagnitude = magnitude / denominator;
    const auto quotient = static_cast<std::int64_t>(quotientMagnitude);
    return {
            numerator < 0 ? -quotient : quotient,
            magnitude % denominator != 0,
    };
}

}

CheckedRational::CheckedRational(const std::int64_t numerator,
                                 const std::uint64_t denominator) noexcept
    : numerator_(numerator), denominator_(denominator) {}

std::expected<CheckedRational, RationalError> CheckedRational::Create(
        const std::int64_t numerator,
        const std::uint64_t denominator) noexcept {
    if (denominator == 0) {
        return std::unexpected(RationalError::ZeroDenominator);
    }
    if (numerator == 0) {
        return CheckedRational{0, 1};
    }

    const bool negative = numerator < 0;
    const std::uint64_t magnitude = UnsignedMagnitude(numerator);
    const std::uint64_t divisor = std::gcd(magnitude, denominator);
    const std::uint64_t reducedMagnitude = magnitude / divisor;
    const std::uint64_t reducedDenominator = denominator / divisor;
    const auto reducedNumerator =
            SignedFromMagnitude(reducedMagnitude, negative);
    if (!reducedNumerator) {
        return std::unexpected(RationalError::Overflow);
    }
    return CheckedRational{*reducedNumerator, reducedDenominator};
}

CheckedRational CheckedRational::Whole(const std::int64_t value) noexcept {
    return CheckedRational{value, 1};
}

std::int64_t CheckedRational::numerator() const noexcept {
    return numerator_;
}

std::uint64_t CheckedRational::denominator() const noexcept {
    return denominator_;
}

// If integer quotients match, comparing positive remainders r/b and s/d is
// equivalent to comparing b/r and d/s with the ordering reversed.
int CheckedRational::Compare(const CheckedRational& other) const noexcept {
    if (numerator_ < 0 && other.numerator_ >= 0) {
        return -1;
    }
    if (numerator_ >= 0 && other.numerator_ < 0) {
        return 1;
    }
    if (numerator_ == 0) {
        return other.numerator_ == 0 ? 0 : -1;
    }
    if (other.numerator_ == 0) {
        return 1;
    }

    const int magnitudeComparison = ComparePositiveFractions(
            UnsignedMagnitude(numerator_),
            denominator_,
            UnsignedMagnitude(other.numerator_),
            other.denominator_);
    return numerator_ < 0 ? -magnitudeComparison : magnitudeComparison;
}

std::expected<CheckedRational, RationalError> CheckedRational::Add(
        const CheckedRational& other) const noexcept {
    const std::uint64_t denominatorGcd =
            std::gcd(denominator_, other.denominator_);
    const std::uint64_t leftFactor =
            other.denominator_ / denominatorGcd;
    const std::uint64_t rightFactor = denominator_ / denominatorGcd;

    const auto leftNumerator =
            CheckedMultiplySignedUnsigned(numerator_, leftFactor);
    const auto rightNumerator =
            CheckedMultiplySignedUnsigned(other.numerator_, rightFactor);
    const auto resultDenominator =
            CheckedMultiplyUnsigned(denominator_, leftFactor);
    if (!leftNumerator || !rightNumerator || !resultDenominator) {
        return std::unexpected(RationalError::Overflow);
    }

    const auto resultNumerator =
            CheckedAddSigned(*leftNumerator, *rightNumerator);
    if (!resultNumerator) {
        return std::unexpected(RationalError::Overflow);
    }
    return Create(*resultNumerator, *resultDenominator);
}

std::expected<CheckedRational, RationalError> CheckedRational::Subtract(
        const CheckedRational& other) const noexcept {
    const std::uint64_t denominatorGcd =
            std::gcd(denominator_, other.denominator_);
    const std::uint64_t leftFactor =
            other.denominator_ / denominatorGcd;
    const std::uint64_t rightFactor = denominator_ / denominatorGcd;

    const auto leftNumerator =
            CheckedMultiplySignedUnsigned(numerator_, leftFactor);
    const auto rightNumerator =
            CheckedMultiplySignedUnsigned(other.numerator_, rightFactor);
    const auto resultDenominator =
            CheckedMultiplyUnsigned(denominator_, leftFactor);
    if (!leftNumerator || !rightNumerator || !resultDenominator) {
        return std::unexpected(RationalError::Overflow);
    }

    const auto resultNumerator =
            CheckedSubtractSigned(*leftNumerator, *rightNumerator);
    if (!resultNumerator) {
        return std::unexpected(RationalError::Overflow);
    }
    return Create(*resultNumerator, *resultDenominator);
}

std::expected<CheckedRational, RationalError> CheckedRational::Multiply(
        const std::int64_t numerator,
        const std::uint64_t denominator) const noexcept {
    if (denominator == 0) {
        return std::unexpected(RationalError::DivisionByZero);
    }

    std::uint64_t leftMagnitude = UnsignedMagnitude(numerator_);
    std::uint64_t leftDenominator = denominator_;
    std::uint64_t rightMagnitude = UnsignedMagnitude(numerator);
    std::uint64_t rightDenominator = denominator;

    const std::uint64_t rightDivisor =
            std::gcd(rightMagnitude, rightDenominator);
    rightMagnitude /= rightDivisor;
    rightDenominator /= rightDivisor;

    const std::uint64_t leftCrossDivisor =
            std::gcd(leftMagnitude, rightDenominator);
    leftMagnitude /= leftCrossDivisor;
    rightDenominator /= leftCrossDivisor;

    const std::uint64_t rightCrossDivisor =
            std::gcd(rightMagnitude, leftDenominator);
    rightMagnitude /= rightCrossDivisor;
    leftDenominator /= rightCrossDivisor;

    const bool negative = (numerator_ < 0) != (numerator < 0);
    const std::uint64_t numeratorLimit = negative
            ? kNegativeMagnitudeLimit
            : static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max());
    if (leftMagnitude != 0 &&
        rightMagnitude > numeratorLimit / leftMagnitude) {
        return std::unexpected(RationalError::Overflow);
    }

    const auto resultDenominator = CheckedMultiplyUnsigned(
            leftDenominator, rightDenominator);
    if (!resultDenominator) {
        return std::unexpected(RationalError::Overflow);
    }

    const auto resultNumerator = SignedFromMagnitude(
            leftMagnitude * rightMagnitude, negative);
    if (!resultNumerator) {
        return std::unexpected(RationalError::Overflow);
    }
    return Create(*resultNumerator, *resultDenominator);
}

std::expected<std::int64_t, RationalError>
CheckedRational::Floor() const noexcept {
    const TruncatedParts parts = DivideTowardZero(numerator_, denominator_);
    if (numerator_ >= 0 || !parts.hasRemainder) {
        return parts.quotient;
    }
    const auto result = CheckedSubtractSigned(parts.quotient, 1);
    if (!result) {
        return std::unexpected(RationalError::Overflow);
    }
    return *result;
}

std::expected<std::int64_t, RationalError>
CheckedRational::Ceil() const noexcept {
    const TruncatedParts parts = DivideTowardZero(numerator_, denominator_);
    if (numerator_ <= 0 || !parts.hasRemainder) {
        return parts.quotient;
    }
    const auto result = CheckedAddSigned(parts.quotient, 1);
    if (!result) {
        return std::unexpected(RationalError::Overflow);
    }
    return *result;
}

std::expected<std::int64_t, RationalError>
CheckedRational::Truncate() const noexcept {
    return DivideTowardZero(numerator_, denominator_).quotient;
}

}
