# Reloadable RFID Card Number File Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load a configurable 16-digit RFID card number from current-directory `card.txt` for every card transfer while preserving the existing default.

**Architecture:** A small `CardData` unit owns parsing, fallback, and Unicode-safe standard-library file access. The existing JVS device calls it only in the armed-card `0x32` transfer branch; CMake stages the default text file beside `config.toml`.

**Tech Stack:** C++23, `std::filesystem::path`, standard file streams, CMake/Ninja, existing executable-style C++ tests.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`; do not deploy into `H:\gc`.
- `card.txt` trims surrounding ASCII whitespace, then accepts exactly sixteen decimal digits.
- Missing, unreadable, empty, malformed, short, or long input returns the built-in `7020392010281502` payload.
- Re-read the file at each armed JVS `0x32` payload transfer; do not cache it or read it in the key-polling worker.
- Preserve the fixed prefix bytes `04 C2 3D DA 6F 52 80 00`.
- Use `std::filesystem::path` and the standard stream path overload end to end. Do not call `.string()`, `_wfopen`, `CreateFileA`, or another manual path conversion.
- Prove a current directory containing both Chinese and Japanese characters works.
- Do not add a TOML field, ConfigGUI control, watcher, or reusable framework.

---

### Task 1: Load, transfer, and package the card number

**Files:**
- Create: `src/Rfid/CardData.h`
- Create: `src/Rfid/CardData.cpp`
- Create: `card.txt`
- Modify: `src/Rfid/State.h:12-15`
- Modify: `src/Rfid/Jvs/Device.cpp:1-10,405-412`
- Modify: `src/Rfid/CMakeLists.txt:1-9`
- Modify: `tests/Rfid/JvsDeviceTests.cpp:1-14,295-323,466-477`
- Modify: `CMakeLists.txt:9-15`
- Modify: `docs/superpowers/specs/2026-07-29-card-number-file-design.md`

**Interfaces:**
- Consumes: `State::card_scan.IsPresent()` and the existing one-shot JVS `0x32` transfer.
- Produces: `using CardData = std::array<std::uint8_t, 24>`, `inline constexpr CardData kDefaultCardData`, `CardData LoadCardData(const std::filesystem::path&) noexcept`, and `CardData LoadCurrentDirectoryCardData() noexcept`.

- [ ] **Step 1: Write focused failing tests**

In `tests/Rfid/JvsDeviceTests.cpp`, include `Rfid/CardData.h`, `<chrono>`,
`<filesystem>`, `<fstream>`, `<stdexcept>`, `<string>`, and `<system_error>`.
Add these helpers in the anonymous namespace:

```cpp
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
            (L"GCLoader-CardData-" + std::to_wstring(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class ScopedCurrentDirectory {
public:
    explicit ScopedCurrentDirectory(const std::filesystem::path& path)
        : original_{std::filesystem::current_path()}
    {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(original_, error);
    }

private:
    std::filesystem::path original_;
};

void WriteText(
    const std::filesystem::path& path,
    std::string_view text)
{
    std::ofstream output{
        path, std::ios::binary | std::ios::trunc};
    if (!output) {
        throw std::runtime_error{"could not create card test file"};
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error{"could not write card test file"};
    }
}

gc::rfid::CardData ExpectedCardData(std::string_view number)
{
    auto result = gc::rfid::kDefaultCardData;
    std::ranges::transform(
        number, result.begin() + 8,
        [](char value) { return static_cast<std::uint8_t>(value); });
    return result;
}
```

Add `test_card_data_file_loading()` and call it from `main()`. Cover these
exact cases:

```cpp
int test_card_data_file_loading()
{
    using namespace gc::rfid;

    int failures = 0;
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    const auto missing = root / L"missing.txt";
    failures += expect(
        LoadCardData(missing) == kDefaultCardData,
        "missing file uses default");

    const auto card_file = root / L"card.txt";
    WriteText(card_file, " \t1234567890123456\r\n");
    failures += expect(
        LoadCardData(card_file) ==
            ExpectedCardData("1234567890123456"),
        "surrounding ASCII whitespace");

    for (const std::string_view invalid : {
             "", "123456789012345", "12345678901234567",
             "123456789012345X"}) {
        WriteText(card_file, invalid);
        failures += expect(
            LoadCardData(card_file) == kDefaultCardData,
            "invalid card number uses default");
    }

    const auto unicode_directory =
        root / L"\u6d4b\u8bd5-\u30ab\u30fc\u30c9";
    std::filesystem::create_directories(unicode_directory);
    {
        ScopedCurrentDirectory current_directory{unicode_directory};

        WriteText(L"card.txt", "1111222233334444\n");
        failures += expect(
            LoadCurrentDirectoryCardData() ==
                ExpectedCardData("1111222233334444"),
            "Unicode current directory first load");

        WriteText(L"card.txt", "9999888877776666\n");
        failures += expect(
            LoadCurrentDirectoryCardData() ==
                ExpectedCardData("9999888877776666"),
            "Unicode current directory reload");
    }

    return failures;
}
```

Extend `test_card_output_and_overflow()` with a separate temporary Unicode
current directory. Write the first number before constructing the device:

```cpp
TemporaryDirectory temporary;
const auto unicode_directory =
    temporary.path() / L"\u6d4b\u8bd5-\u30ab\u30fc\u30c9";
std::filesystem::create_directories(unicode_directory);
ScopedCurrentDirectory current_directory{unicode_directory};
WriteText(L"card.txt", "1234567890123456\n");

State state;
state.assigned_address = Address{0x01};
Device device{state};

state.card_scan.Arm();
std::vector<std::uint8_t> expected_card{0x01, 0x01};
const auto first_card = ExpectedCardData("1234567890123456");
expected_card.insert(
    expected_card.end(), first_card.begin(), first_card.end());
expected_card.push_back(0x01);
```

After asserting the first payload and one-shot consumption, overwrite the
file and arm a second scan:

```cpp
WriteText(L"card.txt", "6543210987654321\n");
state.card_scan.Arm();
std::vector<std::uint8_t> expected_second{0x01, 0x01};
const auto second_card = ExpectedCardData("6543210987654321");
expected_second.insert(
    expected_second.end(), second_card.begin(), second_card.end());
expected_second.push_back(0x01);
failures += expect_acknowledgement(
    device.HandlePacket(packet(Address{0x01}, {0x32, 0x01, 0x00})),
    expected_second,
    "card payload reload");
failures += expect(
    !state.card_scan.IsPresent(),
    "reloaded card payload consumes card");
```

Keep the existing absent-card zero payload, truncated request, overflow, and
one-shot consumption assertions. Size the zero payload with
`kDefaultCardData.size()`.

- [ ] **Step 2: Run the tests and verify RED**

Run:

```powershell
cmake --build --preset msvc32-release --target JvsDeviceTests
```

Expected: build failure because `Rfid/CardData.h`, `LoadCardData`,
`LoadCurrentDirectoryCardData`, and `kDefaultCardData` do not exist yet. Fix
only test syntax if the failure is unrelated to those missing production
symbols, then rerun until it fails for the intended reason.

- [ ] **Step 3: Add the minimal card-data loader**

Create `src/Rfid/CardData.h` with this public contract:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace gc::rfid {

using CardData = std::array<std::uint8_t, 24>;

inline constexpr CardData kDefaultCardData{
    0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00,
    '7', '0', '2', '0', '3', '9', '2', '0',
    '1', '0', '2', '8', '1', '5', '0', '2'};

[[nodiscard]] CardData LoadCardData(
    const std::filesystem::path& path) noexcept;
[[nodiscard]] CardData LoadCurrentDirectoryCardData() noexcept;

} // namespace gc::rfid
```

Create `src/Rfid/CardData.cpp` with this implementation:

```cpp
#include "Rfid/CardData.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace gc::rfid {
namespace {

constexpr bool IsAsciiWhitespace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f' || value == '\v';
}

std::string_view TrimAsciiWhitespace(std::string_view value) noexcept
{
    while (!value.empty() && IsAsciiWhitespace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && IsAsciiWhitespace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

CardData AssembleCardData(std::string_view number) noexcept
{
    auto result = kDefaultCardData;
    std::ranges::transform(
        number, result.begin() + 8,
        [](char value) { return static_cast<std::uint8_t>(value); });
    return result;
}

} // namespace

CardData LoadCardData(const std::filesystem::path& path) noexcept
{
    try {
        std::ifstream input{path, std::ios::binary};
        if (!input) {
            return kDefaultCardData;
        }

        const std::string contents{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        if (input.bad()) {
            return kDefaultCardData;
        }

        const auto number = TrimAsciiWhitespace(contents);
        if (number.size() != 16 ||
            !std::ranges::all_of(number, [](char value) {
                return value >= '0' && value <= '9';
            })) {
            return kDefaultCardData;
        }
        return AssembleCardData(number);
    } catch (...) {
        return kDefaultCardData;
    }
}

CardData LoadCurrentDirectoryCardData() noexcept
{
    try {
        return LoadCardData(std::filesystem::path{L"card.txt"});
    } catch (...) {
        return kDefaultCardData;
    }
}

} // namespace gc::rfid
```

Add `CardData.cpp` to `gc_rfid_core` in `src/Rfid/CMakeLists.txt`. Remove the
old `kCardData` constant from `State.h`. The implementation must retain the
path object and pass it directly to `std::ifstream`; do not narrow it.

- [ ] **Step 4: Connect the loader at the one-shot JVS transfer**

Include `Rfid/CardData.h` in `src/Rfid/Jvs/Device.cpp`. Replace only the
card-present payload selection:

```cpp
const bool card_present = state_.card_scan.IsPresent();
if (card_present) {
    const auto card_data = LoadCurrentDirectoryCardData();
    if (!AppendOrOverflow(writer, card_data)) {
        return DeviceResponse{acknowledgement};
    }
} else {
    const auto response_size =
        static_cast<std::size_t>(byte_count) *
        kDefaultCardData.size();
    for (std::size_t i = 0; i < response_size; ++i) {
        if (!AppendOrOverflow(writer, 0x00)) {
            return DeviceResponse{acknowledgement};
        }
    }
}
```

Do not change when the card is armed, reported present, consumed, or reset.

- [ ] **Step 5: Add and stage the default file**

Create repository-root `card.txt` with exactly:

```text
7020392010281502
```

Add a second `configure_file(... COPYONLY)` block in top-level
`CMakeLists.txt`:

```cmake
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/card.txt"
        "${GC_DIST_DIR}/card.txt"
        COPYONLY
)
```

- [ ] **Step 6: Build and run focused verification**

Run:

```powershell
cmake --build --preset msvc32-release --target JvsDeviceTests RfidRuntimeTests JvsCodecTests ComPortStateTests iDmacDrv32
ctest --preset msvc32-release -R "^(JvsDeviceTests|RfidRuntimeTests|JvsCodecTests|ComPortStateTests)$"
```

Expected: all targets build and all four RFID tests pass with zero failures.

Verify packaging and exact default content:

```powershell
$sourceCard = Get-Content -Raw '.\card.txt'
$distCard = Get-Content -Raw '.\build-msvc32-release\dist\card.txt'
if ($sourceCard -ne $distCard -or $sourceCard.Trim() -ne '7020392010281502') {
    throw 'dist/card.txt does not match the repository default'
}
git diff --check
```

Expected: no exception and `git diff --check` exits 0.

- [ ] **Step 7: Commit the implementation**

```powershell
git add -- card.txt CMakeLists.txt `
  src/Rfid/CardData.h src/Rfid/CardData.cpp src/Rfid/State.h `
  src/Rfid/Jvs/Device.cpp src/Rfid/CMakeLists.txt `
  tests/Rfid/JvsDeviceTests.cpp `
  docs/superpowers/specs/2026-07-29-card-number-file-design.md
git commit -m "feat: load RFID card number from file"
```
