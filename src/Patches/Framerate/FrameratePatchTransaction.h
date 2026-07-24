#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace gc::framerate {

inline constexpr std::size_t kMaximumPatternBytes = 32;
inline constexpr std::size_t kMaximumFramerateWrites = 17;
inline constexpr std::size_t kMaximumFramerateHooks = 46;

struct BytePattern {
    std::array<std::byte, kMaximumPatternBytes> bytes{};
    std::uint8_t size{};

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return {bytes.data(), size};
    }

    friend bool operator==(const BytePattern&, const BytePattern&) = default;
};

struct CheckedWrite {
    std::uintptr_t address{};
    BytePattern expected{};
    BytePattern replacement{};
    const char* name{};
};

struct HookOperation {
    std::uintptr_t address{};
    BytePattern expected{};
    const char* name{};
    void* context{};
    bool (*install)(void*) noexcept{};
    void (*reset)(void*) noexcept{};
};

struct FramerateMemoryApi {
    bool (*read)(std::uintptr_t, std::span<std::byte>) noexcept;
    bool (*write)(std::uintptr_t, std::span<const std::byte>) noexcept;
};

enum class FramerateInstallStage {
    None,
    Capacity,
    InvalidDescriptor,
    PreflightRead,
    PreflightMismatch,
    DirectWrite,
    HookInstall,
};

struct FramerateInstallError {
    FramerateInstallStage stage{FramerateInstallStage::None};
    std::size_t operation_index{};
    const char* operation_name{};
    bool rollback_attempted{};
    bool rollback_complete{};
};

class FrameratePatchTransaction {
public:
    explicit FrameratePatchTransaction(FramerateMemoryApi memory) noexcept;

    [[nodiscard]] std::expected<void, FramerateInstallError> Install(
        std::span<const CheckedWrite> writes,
        std::span<const HookOperation> hooks) noexcept;

    [[nodiscard]] bool Rollback() noexcept;
    [[nodiscard]] bool committed() const noexcept { return committed_; }

private:
    [[nodiscard]] bool PatternMatches(
        std::uintptr_t address,
        const BytePattern& pattern) noexcept;
    [[nodiscard]] bool VerifyOriginalState() noexcept;
    [[nodiscard]] std::expected<void, FramerateInstallError> Fail(
        FramerateInstallStage stage,
        std::size_t index,
        const char* name) noexcept;

    FramerateMemoryApi memory_{};
    std::array<CheckedWrite, kMaximumFramerateWrites> writes_{};
    std::array<HookOperation, kMaximumFramerateHooks> hooks_{};
    std::size_t write_count_{};
    std::size_t hook_count_{};
    std::size_t applied_write_count_{};
    std::size_t installed_hook_count_{};
    bool committed_{};
};

[[nodiscard]] FramerateMemoryApi ProductionFramerateMemoryApi() noexcept;

} // namespace gc::framerate
