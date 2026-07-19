#include "Patches/Framerate/FrameratePatchPlan.h"

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

constexpr std::array<FramerateHookContract, 25> kHookContracts{{
    {FramerateHookId::MovieClipGoto, 0x000DEA30,
        Pattern(0x6A, 0xFF, 0x68, 0xC9, 0x38, 0x67, 0x00),
        "MovieClip goto-frame depth guard"},
    {FramerateHookId::MovieClipAdvance, 0x000DF940,
        Pattern(0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x90, 0x4C,
            0x01, 0x00, 0x00),
        "MovieClip authored-60Hz advance"},
    {FramerateHookId::NewsUpdate, 0x00218A50,
        Pattern(0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xED, 0xA1,
            0x67, 0x00),
        "news authored-60Hz update"},
    {FramerateHookId::NoticeUpdate, 0x002544D0,
        Pattern(0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x7F, 0x96,
            0x67, 0x00),
        "notice authored-60Hz update"},
    {FramerateHookId::PaletteCompare, 0x0022BA60,
        Pattern(0x83, 0x78, 0x0C, 0x3C),
        "palette target-rate compare"},
    {FramerateHookId::StageClipFrame, 0x00244054,
        Pattern(0x89, 0x4D, 0xF8),
        "stage clip authored-frame mapping"},
    {FramerateHookId::IfblWait, 0x002309D4,
        Pattern(0x89, 0x4A, 0x3C),
        "IFBL wait duration"},
    {FramerateHookId::IfblLoop, 0x00230AB6,
        Pattern(0x89, 0x4C, 0x90, 0x1C),
        "IFBL loop duration"},
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
    {FramerateHookId::AudioResyncDiagnostic, 0x002401C4,
        Pattern(0x8B, 0x55, 0xF8),
        "audio resync diagnostic"},
    {FramerateHookId::GameplayEffectAdvance, 0x00264E2D,
        Pattern(0xE8, 0x6E, 0xBA, 0xF8, 0xFF),
        "gameplay effect authored-60Hz advance"},
    {FramerateHookId::EffectCadence6, 0x0024063B,
        Pattern(0x85, 0xD2),
        "gameplay effect period-6 cadence"},
    {FramerateHookId::EffectCadence5, 0x002408D7,
        Pattern(0x85, 0xD2),
        "gameplay effect period-5 cadence"},
    {FramerateHookId::EffectCadence4, 0x00240C9C,
        Pattern(0x85, 0xD2),
        "gameplay effect period-4 cadence"},
    {FramerateHookId::EffectCadence16A, 0x00241213,
        Pattern(0x85, 0xD2),
        "gameplay effect period-16 cadence A"},
    {FramerateHookId::EffectCadence16B, 0x0024122F,
        Pattern(0x81, 0xE1, 0x0F, 0x00, 0x00, 0x80),
        "gameplay effect period-16 cadence B"},
    {FramerateHookId::EffectCadence8, 0x00241268,
        Pattern(0x85, 0xC0),
        "gameplay effect period-8 cadence"},
    {FramerateHookId::RemoteCadenceA, 0x002632DB,
        Pattern(0x85, 0xD2),
        "remote gameplay period-4 cadence A"},
    {FramerateHookId::RemoteCadenceB, 0x00263646,
        Pattern(0x85, 0xD2),
        "remote gameplay period-4 cadence B"},
    {FramerateHookId::GameplayBlink, 0x0024A1B9,
        Pattern(0xD1, 0xF8),
        "gameplay authored-frame blink"},
    {FramerateHookId::PlayerPositionCountdown, 0x0024F0C6,
        Pattern(0x83, 0xE9, 0x01),
        "player-position authored-60Hz countdown"},
    {FramerateHookId::OuterFrame, 0x00058B70,
        Pattern(0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x50, 0x24),
        "outer-frame cadence and authored clock"},
}};

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
    if (!repeat_initial || !repeat_next) {
        return std::unexpected(
            FrameratePatchPlanError::ProfileConversion);
    }

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
            "chart seconds-to-frames operand");

    if (!complete || plan.count != plan.writes.size()) {
        return std::unexpected(FrameratePatchPlanError::Capacity);
    }
    return plan;
}

std::span<const FramerateHookContract>
FramerateHookContracts(bool transformed_timing) noexcept {
    const std::span<const FramerateHookContract> contracts{kHookContracts};
    return transformed_timing ? contracts : contracts.last(1);
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
