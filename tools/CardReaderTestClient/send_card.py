from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import sys


PIPE_NAME = r"\\.\pipe\GCLoader.CardReader"

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
PIPE_READMODE_MESSAGE = 0x00000002
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


def encode_card_number(card_number: str) -> bytes:
    if len(card_number) != 16 or any(
        digit < "0" or digit > "9" for digit in card_number
    ):
        raise ValueError("card number must contain exactly 16 ASCII digits")
    return card_number.encode("ascii")


def classify_response(response: bytes) -> str:
    if response == b"OK":
        return "OK"
    if response == b"INVALID":
        return "INVALID"
    raise RuntimeError(f"unexpected response: {response!r}")


def _load_kernel32():
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    kernel32.CreateFileW.argtypes = (
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    )
    kernel32.CreateFileW.restype = wintypes.HANDLE

    kernel32.SetNamedPipeHandleState.argtypes = (
        wintypes.HANDLE,
        ctypes.POINTER(wintypes.DWORD),
        ctypes.POINTER(wintypes.DWORD),
        ctypes.POINTER(wintypes.DWORD),
    )
    kernel32.SetNamedPipeHandleState.restype = wintypes.BOOL

    kernel32.WriteFile.argtypes = (
        wintypes.HANDLE,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        ctypes.c_void_p,
    )
    kernel32.WriteFile.restype = wintypes.BOOL

    kernel32.ReadFile.argtypes = (
        wintypes.HANDLE,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        ctypes.c_void_p,
    )
    kernel32.ReadFile.restype = wintypes.BOOL

    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    return kernel32


def submit_card(card_number: str) -> str:
    request = encode_card_number(card_number)
    kernel32 = _load_kernel32()
    pipe = kernel32.CreateFileW(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        None,
        OPEN_EXISTING,
        0,
        None,
    )
    if pipe == INVALID_HANDLE_VALUE:
        raise ctypes.WinError(ctypes.get_last_error())

    try:
        mode = wintypes.DWORD(PIPE_READMODE_MESSAGE)
        if not kernel32.SetNamedPipeHandleState(
            pipe,
            ctypes.byref(mode),
            None,
            None,
        ):
            raise ctypes.WinError(ctypes.get_last_error())

        request_buffer = (ctypes.c_char * len(request)).from_buffer_copy(
            request
        )
        bytes_written = wintypes.DWORD()
        if not kernel32.WriteFile(
            pipe,
            request_buffer,
            len(request),
            ctypes.byref(bytes_written),
            None,
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        if bytes_written.value != len(request):
            raise RuntimeError(
                f"short write: {bytes_written.value} of {len(request)} bytes"
            )

        response_buffer = (ctypes.c_char * 8)()
        bytes_read = wintypes.DWORD()
        if not kernel32.ReadFile(
            pipe,
            response_buffer,
            len(response_buffer),
            ctypes.byref(bytes_read),
            None,
        ):
            raise ctypes.WinError(ctypes.get_last_error())

        response = bytes(response_buffer[: bytes_read.value])
        return classify_response(response)
    finally:
        kernel32.CloseHandle(pipe)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Submit one card number to GCLoader's local RFID pipe."
    )
    parser.add_argument("card_number", help="exactly 16 ASCII digits")
    arguments = parser.parse_args(argv)

    try:
        response = submit_card(arguments.card_number)
    except (OSError, RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    print(response)
    return 0 if response == "OK" else 2


if __name__ == "__main__":
    raise SystemExit(main())
