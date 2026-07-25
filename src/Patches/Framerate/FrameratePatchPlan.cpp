#include "Patches/Framerate/FrameratePatchPlan.h"
#include "Patches/Framerate/FramerateEffectTiming.h"
#include "Patches/Framerate/FramerateMenuTiming.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace gc::framerate {

namespace {

constexpr std::uintptr_t kExpectedExecutableBase = 0x00400000;

template <typename... Values>
constexpr BytePattern Pattern(Values... values) noexcept {
    static_assert(sizeof...(Values) <= kMaximumPatternBytes);
    BytePattern result{};
    result.size = static_cast<std::uint8_t>(sizeof...(Values));
    std::size_t index = 0;
    ((result.bytes[index++] =
          static_cast<std::byte>(static_cast<std::uint8_t>(values))), ...);
    return result;
}

BytePattern ValuePattern(std::uint32_t value) noexcept {
    BytePattern result{};
    result.size = sizeof(value);
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        result.bytes[index] = static_cast<std::byte>(
            (value >> (index * 8U)) & 0xFFU);
    }
    return result;
}

BytePattern InstructionPattern(
    std::initializer_list<std::uint8_t> prefix,
    std::uint32_t immediate) noexcept {
    BytePattern result{};
    for (const auto value : prefix) {
        result.bytes[result.size++] = static_cast<std::byte>(value);
    }
    for (std::size_t index = 0; index < sizeof(immediate); ++index) {
        result.bytes[result.size++] = static_cast<std::byte>(
            (immediate >> (index * 8U)) & 0xFFU);
    }
    return result;
}

bool AddWrite(
    FramerateDirectPatchPlan& plan,
    std::uintptr_t base,
    std::uintptr_t rva,
    BytePattern expected,
    BytePattern replacement,
    const char* name) noexcept {
    if (plan.count >= plan.writes.size()) {
        return false;
    }
    plan.writes[plan.count++] = {
        .address = base + rva,
        .expected = expected,
        .replacement = replacement,
        .name = name,
    };
    return true;
}

constexpr std::array<FramerateHookContract, 10> kPreEffectHookContracts{{
    {FramerateHookId::MovieClipGoto, 0x000DEA30,
        Pattern(0x6A, 0xFF, 0x68, 0xC9, 0x38, 0x67, 0x00),
        "MovieClip goto-frame depth guard"},
    {FramerateHookId::MovieClipAdvance, 0x000DF940,
        Pattern(0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x90, 0x4C,
            0x01, 0x00, 0x00),
        "MovieClip authored-60Hz advance"},
    {FramerateHookId::PaletteCompare, 0x0022BA60,
        Pattern(0x83, 0x78, 0x0C, 0x3C),
        "palette target-rate compare"},
    {FramerateHookId::StageClipFrame, 0x00244054,
        Pattern(0x89, 0x4D, 0xF8),
        "stage clip authored-frame mapping"},
    {FramerateHookId::IfblWait, 0x002309D4,
        Pattern(0x89, 0x4A, 0x3C),
        "IFBL wait duration"},
    {FramerateHookId::StageBgmPreload, 0x0021001A,
        Pattern(0x83, 0xC0, 0x01),
        "stage BGM authored-60Hz preload"},
    {FramerateHookId::TuneCountdownCompare, 0x002648F7,
        Pattern(0x83, 0xBA, 0x14, 0x1D, 0x00, 0x00, 0x78),
        "tune countdown compare"},
    {FramerateHookId::AudioSkipMargin, 0x0024018F,
        Pattern(0x8B, 0x45, 0xF4),
        "audio skip-margin floor"},
    {FramerateHookId::AudioSkipInterval, 0x002401BD,
        Pattern(0xF7, 0x79, 0x3C),
        "audio skip-interval duration"},
    {FramerateHookId::AudioResyncPolicy, 0x002401C4,
        Pattern(
            0x8B, 0x55, 0xF8,
            0x52,
            0xE8, 0x33, 0x02, 0xFD, 0xFF,
            0x8B, 0xC8,
            0xE8, 0x2C, 0x12, 0xFD, 0xFF,
            0x5E, 0x8B, 0xE5, 0x5D, 0xC3),
        "WASAPI interval-only audio resync policy"},
}};

constexpr std::array<FramerateHookContract, 2> kPostEffectHookContracts{{
    {FramerateHookId::NavigatorAdvance, 0x001B6310,
        Pattern(0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08,
            0x89, 0x4D, 0xFC, 0x8B, 0x45, 0xFC,
            0x8B, 0x48, 0x60),
        "shared navigator authored-60Hz advance"},
    {FramerateHookId::OuterFrame, 0x00058B70,
        Pattern(0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x50, 0x24),
        "outer-frame cap validation and deterministic authored phase"},
}};

const std::array<FramerateHookContract, kMaximumFramerateHooks>&
AllHookContracts() noexcept {
    static_assert(kMaximumFramerateHooks == 53);
    static const auto contracts = [] {
        std::array<FramerateHookContract, kMaximumFramerateHooks> result{};
        std::size_t index = 0;
        for (const auto& contract : kPreEffectHookContracts) {
            result[index++] = contract;
        }
        for (const auto& contract : FramerateEffectHookContracts()) {
            result[index++] = contract;
        }
        for (const auto& site : FramerateMenuTimingHookSites()) {
            result[index++] = site.contract;
        }
        for (const auto& contract : kPostEffectHookContracts) {
            result[index++] = contract;
        }
        return result;
    }();
    return contracts;
}

} // namespace

std::expected<FramerateDirectPatchPlan, FrameratePatchPlanError>
BuildFramerateDirectPatchPlan(
    std::uintptr_t executable_base,
    const FramerateProfile& profile,
    std::uint64_t target_fps_operand) noexcept {
    if (executable_base != kExpectedExecutableBase) {
        return std::unexpected(
            FrameratePatchPlanError::UnexpectedImageBase);
    }
    if (target_fps_operand >
        std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            FrameratePatchPlanError::OperandAddressOutOfRange);
    }

    FramerateDirectPatchPlan plan{};
    if (profile.native_timing()) {
        return plan;
    }

    const auto repeat_initial = profile.ScaleDurationFrames(16);
    const auto repeat_next = profile.ScaleDurationFrames(8);
    const auto menu_repeat_interval = profile.ScaleDurationFrames(3);
    if (!repeat_initial || !repeat_next || !menu_repeat_interval) {
        return std::unexpected(
            FrameratePatchPlanError::ProfileConversion);
    }
    plan.menu_repeat_initial = repeat_initial.value();
    plan.menu_repeat_interval = menu_repeat_interval.value();

    const auto frame_ms = ValuePattern(
        std::bit_cast<std::uint32_t>(profile.frame_milliseconds()));
    const auto frame_seconds = ValuePattern(
        std::bit_cast<std::uint32_t>(profile.frame_seconds()));
    const auto smoothing = ValuePattern(
        std::bit_cast<std::uint32_t>(profile.render_smoothing_step()));
    const auto decay = ValuePattern(
        std::bit_cast<std::uint32_t>(profile.render_offset_decay_step()));
    const auto target_operand =
        static_cast<std::uint32_t>(target_fps_operand);

    const bool complete =
        AddWrite(
            plan, executable_base, 0x002FC0A0,
            ValuePattern(std::bit_cast<std::uint32_t>(1000.0F / 60.0F)),
            frame_ms,
            "gameplay frame milliseconds") &&
        AddWrite(
            plan, executable_base, 0x002F4604,
            ValuePattern(std::bit_cast<std::uint32_t>(1000.0F / 60.0F)),
            frame_ms,
            "visual frame milliseconds") &&
        AddWrite(
            plan, executable_base, 0x002FC280,
            ValuePattern(std::bit_cast<std::uint32_t>(1.0F / 60.0F)),
            frame_seconds,
            "gameplay frame seconds") &&
        AddWrite(
            plan, executable_base, 0x002E8F00,
            ValuePattern(std::bit_cast<std::uint32_t>(4.0F)),
            smoothing,
            "render smoothing step") &&
        AddWrite(
            plan, executable_base, 0x002E8F04,
            ValuePattern(std::bit_cast<std::uint32_t>(5.0F)),
            decay,
            "render offset-decay step") &&
        AddWrite(
            plan, executable_base, 0x00055CCC,
            Pattern(0xC7, 0x00, 0x10, 0x00, 0x00, 0x00),
            InstructionPattern(
                {0xC7, 0x00},
                static_cast<std::uint32_t>(repeat_initial.value())),
            "XIO repeat initial duration") &&
        AddWrite(
            plan, executable_base, 0x00055CDD,
            Pattern(0xC7, 0x00, 0x08, 0x00, 0x00, 0x00),
            InstructionPattern(
                {0xC7, 0x00},
                static_cast<std::uint32_t>(repeat_next.value())),
            "XIO repeat next duration") &&
        AddWrite(
            plan, executable_base, 0x0005F843,
            Pattern(0xC7, 0x86, 0xD4, 0x02, 0x00, 0x00,
                0x10, 0x00, 0x00, 0x00),
            InstructionPattern(
                {0xC7, 0x86, 0xD4, 0x02, 0x00, 0x00},
                static_cast<std::uint32_t>(repeat_initial.value())),
            "native keyboard repeat initial duration") &&
        AddWrite(
            plan, executable_base, 0x0005F84D,
            Pattern(0xC7, 0x86, 0xD8, 0x02, 0x00, 0x00,
                0x08, 0x00, 0x00, 0x00),
            InstructionPattern(
                {0xC7, 0x86, 0xD8, 0x02, 0x00, 0x00},
                static_cast<std::uint32_t>(repeat_next.value())),
            "native keyboard repeat next duration") &&
        AddWrite(
            plan, executable_base, 0x002645EE,
            Pattern(0xC7, 0x80, 0x14, 0x1D, 0x00, 0x00,
                0x78, 0x00, 0x00, 0x00),
            InstructionPattern(
                {0xC7, 0x80, 0x14, 0x1D, 0x00, 0x00},
                profile.two_second_frames()),
            "gameplay countdown duration") &&
        AddWrite(
            plan, executable_base, 0x00249A5E,
            Pattern(0xB8, 0x78, 0x00, 0x00, 0x00),
            InstructionPattern({0xB8}, profile.two_second_frames()),
            "render EAX countdown duration") &&
        AddWrite(
            plan, executable_base, 0x00249A73,
            Pattern(0xBA, 0x78, 0x00, 0x00, 0x00),
            InstructionPattern({0xBA}, profile.two_second_frames()),
            "render EDX countdown duration") &&
        AddWrite(
            plan, executable_base, 0x0022BACF,
            Pattern(0xD8, 0x2D, 0xAC, 0xBB, 0x6F, 0x00),
            InstructionPattern({0xD8, 0x2D}, target_operand),
            "palette normalizer operand one") &&
        AddWrite(
            plan, executable_base, 0x0022BAD5,
            Pattern(0xD8, 0x35, 0xAC, 0xBB, 0x6F, 0x00),
            InstructionPattern({0xD8, 0x35}, target_operand),
            "palette normalizer operand two") &&
        AddWrite(
            plan, executable_base, 0x00262CB6,
            Pattern(0xD8, 0x0D, 0xAC, 0xBB, 0x6F, 0x00),
            InstructionPattern({0xD8, 0x0D}, target_operand),
            "chart seconds-to-frames operand") &&
        AddWrite(
            plan, executable_base, 0x00382CE8,
            ValuePattern(16U),
            ValuePattern(static_cast<std::uint32_t>(
                plan.menu_repeat_initial)),
            "non-song menu repeat initial duration") &&
        AddWrite(
            plan, executable_base, 0x00382CEC,
            ValuePattern(3U),
            ValuePattern(static_cast<std::uint32_t>(
                plan.menu_repeat_interval)),
            "non-song menu repeat interval");

    if (!complete || plan.count != plan.writes.size()) {
        return std::unexpected(FrameratePatchPlanError::Capacity);
    }
    return plan;
}

std::span<const FramerateHookContract>
FramerateHookContracts(bool transformed_timing) noexcept {
    const std::span<const FramerateHookContract> contracts{
        AllHookContracts()};
    return transformed_timing ? contracts : contracts.last(1);
}

FramerateHookPlan BuildFramerateHookPlan(
    bool transformed_timing,
    bool wasapi_audio_committed) noexcept {
    FramerateHookPlan plan{};
    for (const auto& contract : AllHookContracts()) {
        const bool selected =
            contract.id == FramerateHookId::OuterFrame ||
            (contract.id == FramerateHookId::AudioResyncPolicy
                 ? wasapi_audio_committed
                 : transformed_timing);
        if (selected) {
            plan.contracts[plan.count++] = contract;
        }
    }
    return plan;
}

std::uint32_t ApplyCmp32Flags(
    std::uint32_t existing,
    std::uint32_t left,
    std::uint32_t right) noexcept {
    constexpr std::uint32_t kCarry = 0x001;
    constexpr std::uint32_t kParity = 0x004;
    constexpr std::uint32_t kAuxiliary = 0x010;
    constexpr std::uint32_t kZero = 0x040;
    constexpr std::uint32_t kSign = 0x080;
    constexpr std::uint32_t kOverflow = 0x800;
    constexpr std::uint32_t kMask =
        kCarry | kParity | kAuxiliary | kZero | kSign | kOverflow;

    const std::uint32_t result = left - right;
    const bool even_parity =
        (std::popcount(result & 0xFFU) & 1U) == 0;
    std::uint32_t flags = existing & ~kMask;
    flags |= left < right ? kCarry : 0;
    flags |= even_parity ? kParity : 0;
    flags |= ((left ^ right ^ result) & 0x10U) != 0 ? kAuxiliary : 0;
    flags |= result == 0 ? kZero : 0;
    flags |= (result & 0x80000000U) != 0 ? kSign : 0;
    flags |= ((left ^ right) & (left ^ result) & 0x80000000U) != 0
        ? kOverflow
        : 0;
    return flags;
}

} // namespace gc::framerate
