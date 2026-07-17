#include "Patches/Framerate/FrameratePatchTransaction.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace gc::framerate {

namespace {

bool ProductionRead(
    std::uintptr_t address,
    std::span<std::byte> destination) noexcept {
    __try {
        std::memcpy(
            destination.data(),
            reinterpret_cast<const void*>(address),
            destination.size());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CopyExecutableBytes(
    std::uintptr_t address,
    std::span<const std::byte> source) noexcept {
    __try {
        std::memcpy(
            reinterpret_cast<void*>(address),
            source.data(),
            source.size());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionWrite(
    std::uintptr_t address,
    std::span<const std::byte> source) noexcept {
    if (source.empty()) {
        return false;
    }

    auto* destination = reinterpret_cast<void*>(address);
    DWORD previous_protection{};
    if (!VirtualProtect(
            destination,
            source.size(),
            PAGE_EXECUTE_READWRITE,
            &previous_protection)) {
        return false;
    }

    const bool copied = CopyExecutableBytes(address, source);
    const bool flushed = copied &&
        FlushInstructionCache(
            GetCurrentProcess(), destination, source.size()) != FALSE;

    DWORD temporary_protection{};
    const bool restored = VirtualProtect(
        destination,
        source.size(),
        previous_protection,
        &temporary_protection) != FALSE;
    return copied && flushed && restored &&
        temporary_protection == PAGE_EXECUTE_READWRITE;
}

} // namespace

FrameratePatchTransaction::FrameratePatchTransaction(
    FramerateMemoryApi memory) noexcept
    : memory_{memory} {
}

std::expected<void, FramerateInstallError>
FrameratePatchTransaction::Install(
    std::span<const CheckedWrite> writes,
    std::span<const HookOperation> hooks) noexcept {
    if (writes.size() > writes_.size() || hooks.size() > hooks_.size()) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::Capacity,
        });
    }
    if (memory_.read == nullptr || memory_.write == nullptr) {
        return std::unexpected(FramerateInstallError{
            .stage = FramerateInstallStage::InvalidDescriptor,
        });
    }

    for (std::size_t index = 0; index < writes.size(); ++index) {
        const auto& write = writes[index];
        if (write.expected.size == 0 ||
            write.expected.size != write.replacement.size ||
            write.expected.size > kMaximumPatternBytes) {
            return std::unexpected(FramerateInstallError{
                .stage = FramerateInstallStage::InvalidDescriptor,
                .operation_index = index,
                .operation_name = write.name,
            });
        }
        std::array<std::byte, kMaximumPatternBytes> actual{};
        const auto destination = std::span{
            actual.data(), static_cast<std::size_t>(write.expected.size)};
        if (!memory_.read(write.address, destination)) {
            return std::unexpected(FramerateInstallError{
                .stage = FramerateInstallStage::PreflightRead,
                .operation_index = index,
                .operation_name = write.name,
            });
        }
        if (!std::equal(
                destination.begin(), destination.end(),
                write.expected.view().begin())) {
            return std::unexpected(FramerateInstallError{
                .stage = FramerateInstallStage::PreflightMismatch,
                .operation_index = index,
                .operation_name = write.name,
            });
        }
    }

    for (std::size_t index = 0; index < hooks.size(); ++index) {
        const auto& hook = hooks[index];
        if (hook.expected.size == 0 ||
            hook.expected.size > kMaximumPatternBytes ||
            hook.install == nullptr || hook.reset == nullptr) {
            return std::unexpected(FramerateInstallError{
                .stage = FramerateInstallStage::InvalidDescriptor,
                .operation_index = index,
                .operation_name = hook.name,
            });
        }
        std::array<std::byte, kMaximumPatternBytes> actual{};
        const auto destination = std::span{
            actual.data(), static_cast<std::size_t>(hook.expected.size)};
        if (!memory_.read(hook.address, destination)) {
            return std::unexpected(FramerateInstallError{
                .stage = FramerateInstallStage::PreflightRead,
                .operation_index = index,
                .operation_name = hook.name,
            });
        }
        if (!std::equal(
                destination.begin(), destination.end(),
                hook.expected.view().begin())) {
            return std::unexpected(FramerateInstallError{
                .stage = FramerateInstallStage::PreflightMismatch,
                .operation_index = index,
                .operation_name = hook.name,
            });
        }
    }

    std::copy(writes.begin(), writes.end(), writes_.begin());
    std::copy(hooks.begin(), hooks.end(), hooks_.begin());
    write_count_ = writes.size();
    hook_count_ = hooks.size();

    for (std::size_t index = 0; index < write_count_; ++index) {
        applied_write_count_ = index + 1;
        if (!memory_.write(
                writes_[index].address,
                writes_[index].replacement.view())) {
            return Fail(
                FramerateInstallStage::DirectWrite,
                index,
                writes_[index].name);
        }
    }

    for (std::size_t index = 0; index < hook_count_; ++index) {
        installed_hook_count_ = index + 1;
        if (!hooks_[index].install(hooks_[index].context)) {
            return Fail(
                FramerateInstallStage::HookInstall,
                index,
                hooks_[index].name);
        }
    }

    committed_ = true;
    return {};
}

bool FrameratePatchTransaction::Rollback() noexcept {
    while (installed_hook_count_ != 0) {
        const auto index = --installed_hook_count_;
        hooks_[index].reset(hooks_[index].context);
    }

    bool writes_restored = true;
    while (applied_write_count_ != 0) {
        const auto index = --applied_write_count_;
        if (!memory_.write(
                writes_[index].address,
                writes_[index].expected.view())) {
            writes_restored = false;
        }
    }

    installed_hook_count_ = 0;
    applied_write_count_ = 0;
    const bool verified = VerifyOriginalState();
    write_count_ = 0;
    hook_count_ = 0;
    committed_ = false;
    return writes_restored && verified;
}

bool FrameratePatchTransaction::PatternMatches(
    std::uintptr_t address,
    const BytePattern& pattern) noexcept {
    if (pattern.size == 0 || pattern.size > kMaximumPatternBytes) {
        return false;
    }
    std::array<std::byte, kMaximumPatternBytes> actual{};
    const auto destination = std::span{
        actual.data(), static_cast<std::size_t>(pattern.size)};
    return memory_.read(address, destination) &&
        std::equal(
            destination.begin(),
            destination.end(),
            pattern.view().begin());
}

bool FrameratePatchTransaction::VerifyOriginalState() noexcept {
    for (std::size_t index = 0; index < write_count_; ++index) {
        if (!PatternMatches(writes_[index].address, writes_[index].expected)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < hook_count_; ++index) {
        if (!PatternMatches(hooks_[index].address, hooks_[index].expected)) {
            return false;
        }
    }
    return true;
}

std::expected<void, FramerateInstallError>
FrameratePatchTransaction::Fail(
    FramerateInstallStage stage,
    std::size_t index,
    const char* name) noexcept {
    const bool rollback_complete = Rollback();
    return std::unexpected(FramerateInstallError{
        .stage = stage,
        .operation_index = index,
        .operation_name = name,
        .rollback_attempted = true,
        .rollback_complete = rollback_complete,
    });
}

FramerateMemoryApi ProductionFramerateMemoryApi() noexcept {
    return {ProductionRead, ProductionWrite};
}

} // namespace gc::framerate
