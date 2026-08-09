// SPDX-License-Identifier: CC0-1.0

#include <Windows.h>

#include <array>
#include <cstddef>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 ||
        (std::wstring_view{argv[1]} != L"--asio-probe" &&
         std::wstring_view{argv[1]} != L"--asio-control-panel")) {
        return 2;
    }

    std::array<std::byte, 256> input{};
    for (;;) {
        DWORD read{};
        if (!ReadFile(
                GetStdHandle(STD_INPUT_HANDLE),
                input.data(),
                static_cast<DWORD>(input.size()),
                &read,
                nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                break;
            }
            return 3;
        }
        if (read == 0) {
            break;
        }
    }

    const DWORD pid = GetCurrentProcessId();
    DWORD written{};
    if (!WriteFile(
            GetStdHandle(STD_OUTPUT_HANDLE),
            &pid,
            sizeof(pid),
            &written,
            nullptr) ||
        written != sizeof(pid) ||
        !FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE))) {
        return 4;
    }

    Sleep(INFINITE);
    return 0;
}
