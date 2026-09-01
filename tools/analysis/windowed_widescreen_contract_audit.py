from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
from typing import Any


IDA_CLI_SOURCE = Path(r"H:\IDACLI\src")
TARGET_DATABASE = Path(r"H:\gc\game471.exe.i64")
PREFERRED_IMAGE_BASE = 0x00400000

try:
    from ida_cli.agent_bridge import AgentSession
except ModuleNotFoundError:
    sys.path.insert(0, str(IDA_CLI_SOURCE))
    from ida_cli.agent_bridge import AgentSession


BYTE_CONTRACTS = (
    ("config_apply", "inline", 0x0023C360, "55 8B EC 83 EC 14 E8 E5 F8 FF FF"),
    ("window_device_create", "inline", 0x0005B8A0, "83 EC 64 53 55 56 57 6A 30 33 ED 8D"),
    ("frame_begin", "inline", 0x0005AC70, "51 53 56 8D 44 24 08 57 50 8B F1 E8"),
    ("frame_end", "inline", 0x0005ACE0, "8B 41 08 8B 08 8B 91 A8 00 00 00 50"),
    ("task_dispatch", "inline", 0x0005C1B0, "8B 09 8B 01 8B 50 10 FF E2 CC CC CC"),
    ("screen_width_int", "inline", 0x00052F20, "A1 E8 6F 78 00 C3"),
    ("screen_height_int", "inline", 0x00052F30, "A1 EC 6F 78 00 C3"),
    ("screen_width_float", "inline", 0x00052F40, "D9 05 F0 6F 78 00 C3"),
    ("screen_height_float", "inline", 0x00052F50, "D9 05 F4 6F 78 00 C3"),
    ("target_width_int", "inline", 0x00052FA0, "A1 F8 6F 78 00 C3"),
    ("target_height_int", "inline", 0x00052FB0, "A1 FC 6F 78 00 C3"),
    ("target_width_float", "inline", 0x00052FC0, "D9 05 00 70 78 00 C3"),
    ("target_height_float", "inline", 0x00052FD0, "D9 05 04 70 78 00 C3"),
    (
        "logical_resolution_set",
        "inline",
        0x00053660,
        "6A FF 68 EB DA 66 00 64 A1 00 00 00 00 50",
    ),
    (
        "logical_target_width_set",
        "inline",
        0x00052F60,
        "DB 44 24 04 8B 44 24 04 A3 F8 6F 78 00",
    ),
    (
        "logical_target_height_set",
        "inline",
        0x00052F80,
        "DB 44 24 04 8B 44 24 04 A3 FC 6F 78 00",
    ),
    ("viewport_reset", "inline", 0x00053140, "8B 4C 24 04 33 C0 83 EC 20 3B C8 0F"),
    ("mouse_debug_poll", "inline", 0x000B06B0, "55 8B EC 83 EC 08 89 4D F8 8B 45 F8"),
    ("reset_pre", "mid", 0x0005B28B, "83 BE 94 00 00 00 00"),
    ("reset_post", "mid", 0x0005B474, "83 C4 04 B8 01 00 00 00"),
    ("gameplay_stage_background", "mid", 0x00262FA0, "E8 4B 1A FE FF 8B 4D C4"),
    ("gameplay_track", "mid", 0x00262FA8, "E8 D3 56 FE FF 8B 4D C4"),
    ("gameplay_effects", "mid", 0x00263041, "E8 FA 5C FE FF E8 D5 00 DF FF"),
    (
        "gameplay_hud_projection",
        "mid",
        0x0023FDBA,
        "E8 B1 F3 F9 FF 8B B5 24 FF FF FF 81 C6 D0 00 00",
    ),
    ("clip_default", "read_only", 0x002441C6, "C6 45 DF 00"),
    ("clip_gate", "mid", 0x002441CA, "8B 95 80 FE FF FF 8B 82 4C 02 00 00 0F B6 88 5C 01 00 00"),
    ("clip_continuation", "read_only", 0x0024422F, "8B 4D D8 E8 C9 18 DC FF 0F B6"),
    ("batch_flush", "read_only", 0x001C9B10, "55 8B EC 83 EC 08 C7 45 FC 00 00 00"),
    ("clip_owner", "read_only", 0x00244000, "55 8B EC 81 EC A0 01 00 00 56 57 89"),
    ("live_frustum_helper", "read_only", 0x00243BE0, "55 8B EC 81 EC C0 00 00 00 89 8D 58"),
)

POINTER_CONTRACTS = (
    ("config_width_setter", 0x002AE62C + 0x18, 0x00059CC0),
    ("config_height_setter", 0x002AE62C + 0x1C, 0x00059CE0),
    ("config_resize_setter", 0x002AE62C + 0x28, 0x00059D20),
    ("config_minmax_setter", 0x002AE62C + 0x2C, 0x00059D40),
    ("config_mode_setter", 0x002AE62C + 0x30, 0x00059D70),
    ("common_2d_render", 0x002F9AFC + 0x10, 0x001F5670),
    ("common_3d_render", 0x002FB218 + 0x10, 0x001784B0),
)

CALLING_CONVENTIONS = (
    ("config_apply", 0x0023C360, "int __cdecl(int main_config_ptr)"),
    ("window_device_create", 0x0005B8A0, "int __thiscall(renderer)"),
    ("frame_begin", 0x0005AC70, "int __thiscall(renderer)"),
    ("frame_end", 0x0005ACE0, "int __thiscall(renderer)"),
    ("task_dispatch", 0x0005C1B0, "int __thiscall(DWORD* task_node)"),
    ("logical_resolution_set", 0x00053660, "int __cdecl(int width, int height)"),
    ("logical_target_width_set", 0x00052F60, "int __cdecl(int width)"),
    ("logical_target_height_set", 0x00052F80, "int __cdecl(int height)"),
    ("viewport_reset", 0x00053140, "int __cdecl(int* viewport)"),
    ("mouse_debug_poll", 0x000B06B0, "POINT* __thiscall(owner, DWORD* output)"),
)

OBJECT_CONTRACTS = {
    "renderer_device_offset": 0x08,
    "renderer_window_offset": 0x8C,
    "renderer_style_offset": 0x98,
    "batch_queue_pointer_rva": 0x003F24FC,
    "batch_queue_count": 4,
    "batch_queue_stride": 24,
    "batch_pending_count_offset": 24,
    "mouse_x_word": 0,
    "mouse_y_word": 1,
    "mouse_valid_word": 6,
}

SOURCE_ROOT = Path(__file__).resolve().parents[2] / "src" / "Patches"
WINDOWED_WIDESCREEN_SOURCE = SOURCE_ROOT / "WindowedWidescreen"
RENDERER_LOSS_HOOK_RANGE = (0x000E5578, 0x000E7A84)
EXPECTED_IDMAC_EXPORTS = (
    "iDmacDrvOpen",
    "iDmacDrvClose",
    "iDmacDrvProgramDownload",
    "iDmacDrvDmaRead",
    "iDmacDrvDmaWrite",
    "iDmacDrvRegisterRead",
    "iDmacDrvRegisterWrite",
    "iDmacDrvRegisterBufferRead",
    "iDmacDrvRegisterBufferWrite",
    "iDmacDrvMemoryRead",
    "iDmacDrvMemoryWrite",
    "iDmacDrvMemoryBufferRead",
    "iDmacDrvMemoryBufferWrite",
    "iDmacDrvMemoryReadExt",
    "iDmacDrvMemoryWriteExt",
)


def _remote_audit_code() -> str:
    return f"""
import struct

byte_contracts = {BYTE_CONTRACTS!r}
pointer_contracts = {POINTER_CONTRACTS!r}
calling_conventions = {CALLING_CONVENTIONS!r}
preferred_base = {PREFERRED_IMAGE_BASE}
image_base = int(idaapi.get_imagebase())

byte_rows = []
all_match = image_base == preferred_base
for name, kind, rva, expected_hex in byte_contracts:
    expected = bytes.fromhex(expected_hex)
    ea = image_base + rva
    actual = ida_bytes.get_bytes(ea, len(expected))
    actual_hex = None if actual is None else actual.hex(" ").upper()
    matched = actual == expected
    all_match = all_match and matched
    function = ida_funcs.get_func(ea)
    byte_rows.append({{
        "name": name,
        "kind": kind,
        "rva": rva,
        "ea": ea,
        "expected": expected_hex,
        "actual": actual_hex,
        "matched": matched,
        "function_start": None if function is None else int(function.start_ea),
        "function_name": None if function is None else idc.get_func_name(function.start_ea),
    }})

pointer_rows = []
for name, pointer_rva, target_rva in pointer_contracts:
    ea = image_base + pointer_rva
    raw = ida_bytes.get_bytes(ea, 4)
    actual = None if raw is None else struct.unpack("<I", raw)[0]
    expected = image_base + target_rva
    matched = actual == expected
    all_match = all_match and matched
    pointer_rows.append({{
        "name": name,
        "pointer_rva": pointer_rva,
        "ea": ea,
        "target_rva": target_rva,
        "expected": expected,
        "actual": actual,
        "matched": matched,
        "target_name": None if actual is None else idc.get_func_name(actual),
        "target_type": None if actual is None else idc.get_type(actual),
    }})

calling_rows = []
for name, rva, contract in calling_conventions:
    ea = image_base + rva
    calling_rows.append({{
        "name": name,
        "rva": rva,
        "ea": ea,
        "contract": contract,
        "ida_name": idc.get_func_name(ea),
        "ida_type": idc.get_type(ea),
    }})

__result__ = {{
    "database_path": str(__database_path__),
    "image_base": image_base,
    "preferred_image_base": preferred_base,
    "byte_contracts": byte_rows,
    "pointer_contracts": pointer_rows,
    "calling_conventions": calling_rows,
    "all_match": all_match,
}}
"""


def _pe_rva_to_offset(
    data: bytes,
    sections_offset: int,
    section_count: int,
    rva: int,
) -> int:
    for index in range(section_count):
        section = sections_offset + index * 40
        if section + 40 > len(data):
            break
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, section + 8
        )
        extent = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + extent:
            offset = raw_offset + rva - virtual_address
            if offset >= len(data):
                break
            return offset
    raise ValueError(f"PE RVA 0x{rva:X} is outside all file sections")


def _read_pe_c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if offset < 0 or end < 0:
        raise ValueError("unterminated PE export name")
    return data[offset:end].decode("ascii")


def _read_pe_exports(
    data: bytes,
    optional_offset: int,
    sections_offset: int,
    section_count: int,
) -> list[str]:
    optional_magic = struct.unpack_from("<H", data, optional_offset)[0]
    if optional_magic != 0x010B:
        raise ValueError(f"expected PE32 optional header, got 0x{optional_magic:04X}")
    export_rva, export_size = struct.unpack_from("<II", data, optional_offset + 96)
    if export_rva == 0 or export_size == 0:
        return []
    export_offset = _pe_rva_to_offset(
        data, sections_offset, section_count, export_rva
    )
    if export_offset + 40 > len(data):
        raise ValueError("truncated PE export directory")
    number_of_names = struct.unpack_from("<I", data, export_offset + 24)[0]
    names_rva = struct.unpack_from("<I", data, export_offset + 32)[0]
    names_offset = _pe_rva_to_offset(
        data, sections_offset, section_count, names_rva
    )
    exports: list[str] = []
    for index in range(number_of_names):
        name_rva = struct.unpack_from("<I", data, names_offset + index * 4)[0]
        name_offset = _pe_rva_to_offset(
            data, sections_offset, section_count, name_rva
        )
        exports.append(_read_pe_c_string(data, name_offset))
    return exports


def _inspect_dll(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"not a PE image: {path}")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 6 > len(data) or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"invalid PE signature: {path}")
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    optional_offset = pe_offset + 24
    sections_offset = optional_offset + optional_size
    exports = _read_pe_exports(
        data,
        optional_offset,
        sections_offset,
        section_count,
    )
    required_strings = (
        b"enable_windowed_widescreen_stage",
        b"widescreen_window_width",
        b"widescreen_window_height",
    )
    forbidden_configuration_strings = (
        b"widescreen_stage_clip_policy",
    )
    return {
        "path": str(path.resolve()),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "machine": machine,
        "machine_hex": f"0x{machine:04X}",
        "is_x86": machine == 0x014C,
        "configuration_strings": {
            value.decode("ascii"): value in data for value in required_strings
        },
        "forbidden_configuration_strings": {
            value.decode("ascii"): value in data
            for value in forbidden_configuration_strings
        },
        "exports": exports,
        "expected_exports": list(EXPECTED_IDMAC_EXPORTS),
        "exports_match": exports == sorted(EXPECTED_IDMAC_EXPORTS),
    }


def _audit_source_overlap() -> dict[str, Any]:
    hook_rvas = {
        rva: name
        for name, kind, rva, _ in BYTE_CONTRACTS
        if kind in {"inline", "mid"}
    }
    hook_values = {
        value: (name, rva, representation)
        for rva, name in hook_rvas.items()
        for value, representation in (
            (rva, "rva"),
            (PREFERRED_IMAGE_BASE + rva, "preferred_va"),
        )
    }
    collisions: list[dict[str, Any]] = []
    seen: set[tuple[Path, int, str]] = set()
    for source in SOURCE_ROOT.rglob("*"):
        if source.suffix.lower() not in {".h", ".cpp"}:
            continue
        if WINDOWED_WIDESCREEN_SOURCE in source.parents:
            continue
        source_text = source.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(
            r"\b0[xX]([0-9A-Fa-f]+)(?:[uUlL]*)\b", source_text
        ):
            matched = hook_values.get(int(match.group(1), 16))
            if matched is None:
                continue
            name, rva, representation = matched
            identity = (source, rva, representation)
            if identity in seen:
                continue
            seen.add(identity)
            collisions.append(
                {
                    "site": name,
                    "rva": rva,
                    "representation": representation,
                    "source": str(source.relative_to(SOURCE_ROOT.parents[1])),
                }
            )

    renderer_range_collisions = [
        {"site": name, "rva": rva}
        for rva, name in hook_rvas.items()
        if RENDERER_LOSS_HOOK_RANGE[0] <= rva <= RENDERER_LOSS_HOOK_RANGE[1]
    ]
    return {
        "selected_hook_count": len(hook_rvas),
        "source_collisions": collisions,
        "renderer_loss_range": list(RENDERER_LOSS_HOOK_RANGE),
        "renderer_range_collisions": renderer_range_collisions,
        "success": not collisions and not renderer_range_collisions,
    }


def _same_windows_path(left: str, right: Path) -> bool:
    return os.path.normcase(os.path.normpath(left)) == os.path.normcase(
        os.path.normpath(str(right))
    )


def run_audit(dll_path: Path | None) -> dict[str, Any]:
    with AgentSession.connect(
        TARGET_DATABASE,
        request_timeout_s=180,
    ) as ida:
        backend = ida.probe_backend(require_ida=True)
        native = ida.result(
            _remote_audit_code(),
            request_id="windowed-widescreen.contract-audit",
            timeout_s=180,
        )

    database_exact = _same_windows_path(native["database_path"], TARGET_DATABASE)
    success = (
        backend.get("ida_available") is True
        and database_exact
        and native["image_base"] == PREFERRED_IMAGE_BASE
        and native["all_match"] is True
    )
    result: dict[str, Any] = {
        "backend": backend,
        "database_exact": database_exact,
        "native": native,
        "object_contracts": OBJECT_CONTRACTS,
        "source_overlap": _audit_source_overlap(),
    }
    success = success and result["source_overlap"]["success"]
    if dll_path is not None:
        dll = _inspect_dll(dll_path)
        result["dll"] = dll
        success = (
            success
            and dll["is_x86"]
            and all(dll["configuration_strings"].values())
            and not any(dll["forbidden_configuration_strings"].values())
            and dll["exports_match"]
        )
    result["success"] = success
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read-only audit of the frozen windowed-widescreen ABI"
    )
    parser.add_argument(
        "--artifact",
        "--dll",
        dest="dll",
        type=Path,
        help="optional built iDmacDrv32.dll to inspect",
    )
    arguments = parser.parse_args()
    result = run_audit(arguments.dll)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
