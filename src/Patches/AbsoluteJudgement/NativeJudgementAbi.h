#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gc::absolute_judgement::native_abi {

static_assert(sizeof(void*) == sizeof(std::uint32_t));

inline constexpr std::uintptr_t kStageBeginRva = 0x2629A0;
inline constexpr std::uintptr_t kStageEndRva = 0x262080;
inline constexpr std::uintptr_t kLoopGuardRva = 0x240239;
inline constexpr std::uintptr_t kPressedRva = 0x22DFB0;
inline constexpr std::uintptr_t kHeldRva = 0x22DF50;
inline constexpr std::uintptr_t kReleasedRva = 0x22DD30;
inline constexpr std::uintptr_t kDirectionRva = 0x22E480;
inline constexpr std::uintptr_t kHeldAgeRva = 0x22DAA0;

inline constexpr std::array<std::uint8_t, 16> kStageBeginPrefix{
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x67, 0xAA,
    0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
};
inline constexpr std::array<std::uint8_t, 16> kStageEndPrefix{
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x98, 0x00, 0x00,
    0x00, 0x89, 0x4D, 0x8C, 0xE8, 0x8F, 0xF1, 0xD9,
};
inline constexpr std::array<std::uint8_t, 6> kLoopGuardPrefix{
    0x0F, 0x8E, 0x91, 0x00, 0x00, 0x00,
};
inline constexpr std::array<std::uint8_t, 16> kPressedPrefix{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x89, 0x4D,
    0xD8, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xD8,
};
inline constexpr std::array<std::uint8_t, 16> kHeldPrefix{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D,
    0xF4, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xF4,
};
inline constexpr std::array<std::uint8_t, 16> kReleasedPrefix{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x89, 0x4D,
    0xD8, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xD8,
};
inline constexpr std::array<std::uint8_t, 16> kDirectionPrefix{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D,
    0xF8, 0x8B, 0x45, 0x0C, 0xD9, 0xEE, 0xD9, 0x18,
};
inline constexpr std::array<std::uint8_t, 16> kHeldAgePrefix{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x89, 0x4D,
    0xF8, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00,
};

inline constexpr std::uintptr_t kLoopTailRva = 0x2402D0;
inline constexpr std::uintptr_t kRecognitionRva = 0x1D68E0;
inline constexpr std::uintptr_t kScoreRva = 0x1CF930;
inline constexpr std::uintptr_t kGetInputManagerRva = 0x001040;
inline constexpr std::uintptr_t kGetGlobalRva = 0x0011D0;
inline constexpr std::uintptr_t kGetConfigRva = 0x0011E0;
inline constexpr std::uintptr_t kGetSoundManagerRva = 0x210400;
inline constexpr std::uintptr_t kGetGroupCursorRva = 0x2122B0;

inline constexpr std::ptrdiff_t kTuneStackOffset = -0x32C;
inline constexpr std::size_t kTuneJudgementStatesOffset = 0x254;
inline constexpr std::size_t kTuneScoreStatesOffset = 0x26C;
inline constexpr std::size_t kPointerCollectionBeginOffset = 0x0C;
inline constexpr std::size_t kPointerCollectionEndOffset = 0x10;
inline constexpr std::size_t kGlobalPlayerIndexOffset = 0xCB4;
inline constexpr std::size_t kInputManagerBoosterOffset = 4;
inline constexpr std::size_t kGameTimeOffsetOffset = 0x2C;
inline constexpr std::size_t kHoldSafeFrameOffset = 0x64;
inline constexpr std::size_t kSlideHoldSafeFrameOffset = 0x68;

inline constexpr std::size_t kScoreMissOffset = 120;
inline constexpr std::size_t kScoreGoodOffset = 124;
inline constexpr std::size_t kScoreCoolOffset = 128;
inline constexpr std::size_t kScoreGreatOffset = 132;
inline constexpr std::size_t kJudgementArrangePublicationOffset = 0xAA;
inline constexpr std::size_t kJudgementLeftFreeTapPublicationOffset = 0xED;
inline constexpr std::size_t kJudgementRightFreeTapPublicationOffset = 0xEE;
inline constexpr int kGameplaySoundGroup = 2;

using RecognitionFn = void(__thiscall*)(void*, int, int);
using ScoreFn = void(__thiscall*)(void*, int);
using StageBeginFn = std::uint8_t(__thiscall*)(void*);
using StageEndFn = int(__thiscall*)(void*);
using PressedFn = std::uint8_t(__thiscall*)(void*, int, int);
using HeldFn = std::uint8_t(__thiscall*)(void*, int, int);
using ReleasedFn = std::uint8_t(__thiscall*)(void*, int, int);
using DirectionFn = int(__thiscall*)(void*, int, float*, float*, int);
using HeldAgeFn = int(__thiscall*)(void*, unsigned int);

using AccessorFn = void*(__cdecl*)();
using GetGroupCursorFn = int(__thiscall*)(void*, int);

} // namespace gc::absolute_judgement::native_abi
