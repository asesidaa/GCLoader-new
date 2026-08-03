#include "Font/FontCharsetCompatibility.h"

#include <algorithm>
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
    LOGFONTW received{};
};

CaptureState* g_capture{};

HFONT ExpectedHandle() noexcept {
    return reinterpret_cast<HFONT>(std::uintptr_t{0x1234});
}

HFONT WINAPI CaptureCreateFontIndirectW(
    const LOGFONTW* requested) noexcept {
    ++g_capture->calls;
    g_capture->received_null = requested == nullptr;
    if (requested != nullptr) {
        g_capture->received = *requested;
    }
    return ExpectedHandle();
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

int TestCharsetConversion(
    BYTE requested_charset,
    BYTE expected_charset,
    const char* name) {
    auto requested = CanaryLogFont(requested_charset);
    const auto original_request = requested;
    auto expected = requested;
    expected.lfCharSet = expected_charset;

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
        std::memcmp(&capture.received, &expected, sizeof(expected)) == 0,
        "only expected charset is changed");
    failures += Expect(
        std::memcmp(
            &requested,
            &original_request,
            sizeof(original_request)) == 0,
        "caller LOGFONTW remains unchanged");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestCharsetConversion(
        ANSI_CHARSET,
        SHIFTJIS_CHARSET,
        "ANSI charset converts to Shift-JIS");
    failures += TestCharsetConversion(
        DEFAULT_CHARSET,
        SHIFTJIS_CHARSET,
        "default charset converts to Shift-JIS");
    failures += TestCharsetConversion(
        GB2312_CHARSET,
        GB2312_CHARSET,
        "explicit charset is preserved");

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
