#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <string_view>

namespace gc::rfid::card_reader {

inline constexpr wchar_t kPipeName[] =
    LR"(\\.\pipe\GCLoader.CardReader)";
inline constexpr std::size_t kRequestByteCount = 16;
inline constexpr std::string_view kAcceptedResponse{"OK"};
inline constexpr std::string_view kInvalidResponse{"INVALID"};

} // namespace gc::rfid::card_reader
