#include "NesysServiceProcess.h"

#include <Windows.h>
#include <iostream>
#include <string>

namespace {

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_false(bool actual, const char* name) {
    if (!actual) {
        return 0;
    }
    std::cerr << "Expected false for " << name << "\n";
    return 1;
}

int expect_dword(DWORD actual, DWORD expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << "\n";
    return 1;
}

LPSTR mutable_command_line(std::string& value) {
    return value.empty() ? nullptr : value.data();
}

} // namespace

int main() {
    int failures = 0;

    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("NesysService.exe"),
        "bare service image");
    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("C:\\Games\\GC\\NesysService.exe"),
        "absolute service image");
    failures += expect_true(
        gc::nesys_service::IsNesysServiceImagePathA("\"C:\\Games\\GC\\NesysService.exe\""),
        "quoted service image");
    failures += expect_false(
        gc::nesys_service::IsNesysServiceImagePathA("game.exe"),
        "game image is not service image");

    failures += expect_true(
        gc::nesys_service::CommandLineContainsAppArgumentA("\"NesysService.exe\" -app"),
        "quoted command line has -app");
    failures += expect_true(
        gc::nesys_service::CommandLineContainsAppArgumentA("NesysService.exe -APP"),
        "command line has uppercase -APP");
    failures += expect_false(
        gc::nesys_service::CommandLineContainsAppArgumentA("NesysService.exe -application"),
        "command line rejects -application");

    std::string service_cmd = "\"C:\\Games\\GC\\NesysService.exe\" -app";
    failures += expect_true(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(service_cmd)),
        "null application with service command");

    std::string args_only = "-app";
    failures += expect_true(
        gc::nesys_service::IsNesysServiceLaunchA("C:\\Games\\GC\\NesysService.exe", mutable_command_line(args_only)),
        "application service path with args-only command line");

    std::string wrong_image = "Other.exe -app";
    failures += expect_false(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(wrong_image)),
        "wrong image with app argument");

    std::string missing_app = "NesysService.exe";
    failures += expect_false(
        gc::nesys_service::IsNesysServiceLaunchA(nullptr, mutable_command_line(missing_app)),
        "service image without app argument");

    failures += expect_true(
        gc::nesys_service::DetectProcessRoleFromImagePathA("C:\\Games\\GC\\NesysService.exe")
            == gc::nesys_service::ProcessRole::Service,
        "service role from image path");
    failures += expect_true(
        gc::nesys_service::DetectProcessRoleFromImagePathA("C:\\Games\\GC\\game.exe")
            == gc::nesys_service::ProcessRole::Game,
        "game role from image path");
    failures += expect_true(
        gc::nesys_service::ShouldRunGameOnlyInitialization(gc::nesys_service::ProcessRole::Game),
        "game role runs game initialization");
    failures += expect_false(
        gc::nesys_service::ShouldRunGameOnlyInitialization(gc::nesys_service::ProcessRole::Service),
        "service role skips game initialization");

    failures += expect_dword(
        gc::nesys_service::AddCreateSuspendedFlag(0),
        CREATE_SUSPENDED,
        "empty flags become suspended");
    failures += expect_dword(
        gc::nesys_service::AddCreateSuspendedFlag(CREATE_NO_WINDOW),
        CREATE_NO_WINDOW | CREATE_SUSPENDED,
        "existing flags preserve create no window");
    failures += expect_true(
        gc::nesys_service::WasCreateSuspendedRequested(CREATE_SUSPENDED),
        "detect caller requested suspended");
    failures += expect_false(
        gc::nesys_service::WasCreateSuspendedRequested(CREATE_NO_WINDOW),
        "detect caller did not request suspended");

    return failures == 0 ? 0 : 1;
}
