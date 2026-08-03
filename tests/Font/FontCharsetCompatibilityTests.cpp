#include "Font/FontCharsetCompatibility.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

struct CaptureState {
    int calls{};
    bool received_null{};
    const LOGFONTW* received_pointer{};
    LOGFONTW received{};
};

CaptureState* g_capture{};

struct FontResourceCapture {
    int original_calls{};
    LPCSTR original_file{};
    DWORD original_flags{};
    PVOID original_reserved{};
    int observer_calls{};
    LPCSTR observed_file{};
    DWORD observed_flags{};
    PVOID observed_reserved{};
    int observed_result{};
    DWORD observed_last_error{};
};

FontResourceCapture* g_font_resource_capture{};

HFONT ExpectedHandle() noexcept {
    return reinterpret_cast<HFONT>(std::uintptr_t{0x1234});
}

HFONT WINAPI CaptureCreateFontIndirectW(
    const LOGFONTW* requested) noexcept {
    ++g_capture->calls;
    g_capture->received_null = requested == nullptr;
    g_capture->received_pointer = requested;
    if (requested != nullptr) {
        g_capture->received = *requested;
    }
    return ExpectedHandle();
}

int WINAPI CaptureAddFontResourceExA(
    LPCSTR file,
    DWORD flags,
    PVOID reserved) noexcept {
    ++g_font_resource_capture->original_calls;
    g_font_resource_capture->original_file = file;
    g_font_resource_capture->original_flags = flags;
    g_font_resource_capture->original_reserved = reserved;
    SetLastError(ERROR_ACCESS_DENIED);
    return 3;
}

void CaptureFontResourceObservation(
    LPCSTR file,
    DWORD flags,
    PVOID reserved,
    int result,
    DWORD last_error) noexcept {
    ++g_font_resource_capture->observer_calls;
    g_font_resource_capture->observed_file = file;
    g_font_resource_capture->observed_flags = flags;
    g_font_resource_capture->observed_reserved = reserved;
    g_font_resource_capture->observed_result = result;
    g_font_resource_capture->observed_last_error = last_error;
    SetLastError(ERROR_INVALID_DATA);
}

LOGFONTW CanaryLogFont(BYTE charset) {
    LOGFONTW value{};
    value.lfHeight = -42;
    value.lfWidth = 17;
    value.lfEscapement = 123;
    value.lfOrientation = 321;
    value.lfWeight = FW_BOLD;
    value.lfItalic = TRUE;
    value.lfUnderline = TRUE;
    value.lfStrikeOut = TRUE;
    value.lfCharSet = charset;
    value.lfOutPrecision = OUT_TT_PRECIS;
    value.lfClipPrecision = CLIP_LH_ANGLES;
    value.lfQuality = CLEARTYPE_NATURAL_QUALITY;
    value.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    constexpr wchar_t face[] = L"InfinityFont_midiam_dot";
    std::copy(std::begin(face), std::end(face), value.lfFaceName);
    return value;
}

int TestFontRequestPassThrough(BYTE charset, const char* name) {
    auto requested = CanaryLogFont(charset);
    const auto original_request = requested;

    CaptureState capture{};
    g_capture = &capture;
    const auto result =
        gc::font::detail::InvokeCreateFontIndirectWDetour(
            &requested,
            CaptureCreateFontIndirectW);

    int failures = 0;
    failures += Expect(result == ExpectedHandle(), name);
    failures += Expect(capture.calls == 1, "original called once");
    failures += Expect(!capture.received_null, "non-null request forwarded");
    failures += Expect(
        capture.received_pointer == &requested,
        "original request pointer forwarded");
    failures += Expect(
        std::memcmp(
            &capture.received,
            &original_request,
            sizeof(original_request)) == 0,
        "all LOGFONTW fields forwarded unchanged");
    failures += Expect(
        std::memcmp(
            &requested,
            &original_request,
            sizeof(original_request)) == 0,
        "caller LOGFONTW remains unchanged");
    return failures;
}

int TestInfinityFontDiagnosticClassification() {
    auto infinity = CanaryLogFont(DEFAULT_CHARSET);
    auto other = infinity;
    constexpr wchar_t other_face[] = L"MS PGothic";
    std::fill(std::begin(other.lfFaceName), std::end(other.lfFaceName), L'\0');
    std::copy(
        std::begin(other_face),
        std::end(other_face),
        other.lfFaceName);

    int failures = 0;
    failures += Expect(
        gc::font::detail::IsInfinityFontFace(&infinity),
        "InfinityFont face is selected for diagnostics");
    failures += Expect(
        !gc::font::detail::IsInfinityFontFace(&other),
        "unrelated face is not selected for diagnostics");
    failures += Expect(
        !gc::font::detail::IsInfinityFontFace(nullptr),
        "null font request is not selected for diagnostics");
    return failures;
}

int TestFontResourceObservationPreservesWin32Contract() {
    constexpr char file[] = "data/font/InfinityFont_midiam_dot.ttf";
    constexpr DWORD flags = 0x30;
    const auto reserved = reinterpret_cast<PVOID>(std::uintptr_t{0x5678});

    FontResourceCapture capture{};
    g_font_resource_capture = &capture;
    SetLastError(ERROR_SUCCESS);
    const auto result =
        gc::font::detail::InvokeAddFontResourceExADetour(
            file,
            flags,
            reserved,
            CaptureAddFontResourceExA,
            CaptureFontResourceObservation);
    const auto returned_last_error = GetLastError();

    int failures = 0;
    failures += Expect(result == 3, "font resource result preserved");
    failures += Expect(
        capture.original_calls == 1,
        "font resource original called once");
    failures += Expect(
        capture.original_file == file &&
            capture.original_flags == flags &&
            capture.original_reserved == reserved,
        "font resource arguments forwarded unchanged");
    failures += Expect(
        capture.observer_calls == 1,
        "font resource observer called once");
    failures += Expect(
        capture.observed_file == file &&
            capture.observed_flags == flags &&
            capture.observed_reserved == reserved &&
            capture.observed_result == 3 &&
            capture.observed_last_error == ERROR_ACCESS_DENIED,
        "font resource outcome observed exactly");
    failures += Expect(
        returned_last_error == ERROR_ACCESS_DENIED,
        "font resource last error preserved across diagnostics");
    return failures;
}

int TestNtGdiEntryByteFormatting() {
    const std::array<std::byte, 16> bytes{
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
        std::byte{0x05},
        std::byte{0x06},
        std::byte{0x07},
        std::byte{0x08},
        std::byte{0x09},
        std::byte{0x0A},
        std::byte{0x0B},
        std::byte{0x0C},
        std::byte{0x0D},
        std::byte{0x0E},
        std::byte{0x0F},
    };
    constexpr char expected[] =
        "00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f";

    const auto formatted =
        gc::font::detail::FormatNtGdiEntryBytes(bytes);
    return Expect(
        std::strcmp(formatted.data(), expected) == 0,
        "NtGdi entry bytes use stable lowercase hex formatting");
}

} // namespace

int main() {
    int failures = 0;
    failures += TestInfinityFontDiagnosticClassification();
    failures += TestFontResourceObservationPreservesWin32Contract();
    failures += TestNtGdiEntryByteFormatting();
    failures += TestFontRequestPassThrough(
        ANSI_CHARSET,
        "ANSI charset is forwarded unchanged");
    failures += TestFontRequestPassThrough(
        DEFAULT_CHARSET,
        "default charset is forwarded unchanged");
    failures += TestFontRequestPassThrough(
        GB2312_CHARSET,
        "explicit charset is forwarded unchanged");

    CaptureState null_capture{};
    g_capture = &null_capture;
    const auto null_result =
        gc::font::detail::InvokeCreateFontIndirectWDetour(
            nullptr,
            CaptureCreateFontIndirectW);
    failures += Expect(
        null_result == ExpectedHandle(),
        "null request preserves original result");
    failures += Expect(
        null_capture.calls == 1 && null_capture.received_null,
        "null request is forwarded unchanged");

    return failures == 0 ? 0 : 1;
}
