"""Read-only cataloging for Groove Coaster gameplay CTune effect assets."""

from __future__ import annotations

import argparse
import hashlib
import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CORE_CONTAINER_NAMES = ("efcdata.dat", "effect.dat", "uvdata.dat")
IMAGE_NAME_PATTERN = re.compile(
    r"^img(?P<big>_big)?(?P<slot>[0-9]+)"
    r"(?:_(?P<language>[^.]+))?\.(?P<suffix>bin|png)$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class OffsetContainer:
    declared_size: int
    record_count: int
    offsets: tuple[int, ...]
    records: tuple[bytes, ...]


@dataclass(frozen=True)
class EffectTrack:
    track_type: int
    texture_slot: int | None
    raw: bytes


@dataclass(frozen=True)
class EffectDefinition:
    index: int
    authored_frames: int
    tracks: tuple[EffectTrack, ...]


@dataclass(frozen=True)
class PngInfo:
    width: int
    height: int
    sha256: str


def _bounded_slice(data: bytes, offset: int, size: int, field: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"{field} is outside the available bytes")
    return data[offset : offset + size]


def read_u16_be(data: bytes, offset: int) -> int:
    return int.from_bytes(
        _bounded_slice(data, offset, 2, "BE16 field"),
        "big",
    )


def read_u32_be(data: bytes, offset: int) -> int:
    return int.from_bytes(
        _bounded_slice(data, offset, 4, "BE32 field"),
        "big",
    )


def parse_offset_container(data: bytes) -> OffsetContainer:
    if len(data) < 6:
        raise ValueError("container header is truncated")

    declared_size = read_u32_be(data, 0)
    record_count = read_u16_be(data, 4)
    header_size = 6 + 4 * (record_count + 1)
    if len(data) < header_size:
        raise ValueError("container offset table is truncated")
    if declared_size != len(data):
        raise ValueError(
            f"container declared size {declared_size} does not match "
            f"actual size {len(data)}"
        )

    offsets = tuple(
        read_u32_be(data, 6 + index * 4)
        for index in range(record_count + 1)
    )
    if offsets[0] != header_size:
        raise ValueError(
            f"first record offset {offsets[0]} does not match "
            f"header size {header_size}"
        )
    if offsets[-1] != declared_size:
        raise ValueError(
            f"final offset {offsets[-1]} does not match "
            f"declared size {declared_size}"
        )
    if any(left >= right for left, right in zip(offsets, offsets[1:])):
        raise ValueError("container offsets must be strictly increasing")

    records = tuple(
        data[start:end] for start, end in zip(offsets, offsets[1:])
    )
    return OffsetContainer(
        declared_size=declared_size,
        record_count=record_count,
        offsets=offsets,
        records=records,
    )


def parse_effect_definition(index: int, record: bytes) -> EffectDefinition:
    if len(record) < 3:
        raise ValueError(f"effect definition header {index} is truncated")

    authored_frames = read_u16_be(record, 0)
    track_count = record[2]
    table_end = 3 + 2 * track_count
    if len(record) < table_end:
        raise ValueError(
            f"effect definition {index} track offset table is truncated"
        )
    if track_count == 0:
        return EffectDefinition(index, authored_frames, ())

    offsets = tuple(
        read_u16_be(record, 3 + track_index * 2)
        for track_index in range(track_count)
    )
    if offsets[0] != table_end:
        raise ValueError(
            f"effect definition {index} first track offset "
            f"{offsets[0]} does not match table end {table_end}"
        )
    if any(left >= right for left, right in zip(offsets, offsets[1:])):
        raise ValueError(
            f"effect definition {index} track offsets must be "
            "strictly increasing"
        )
    if offsets[-1] >= len(record):
        raise ValueError(
            f"effect definition {index} final track offset is out of bounds"
        )

    tracks: list[EffectTrack] = []
    ends = offsets[1:] + (len(record),)
    for track_index, (start, end) in enumerate(zip(offsets, ends)):
        if start >= end or end > len(record):
            raise ValueError(
                f"effect definition {index} track {track_index} "
                "range is invalid"
            )
        raw = record[start:end]
        track_type = raw[0]
        texture_slot: int | None = None
        if track_type == 0x02:
            if len(raw) < 3:
                raise ValueError(
                    f"effect definition {index} track {track_index} "
                    "is missing its texture slot"
                )
            texture_slot = read_u16_be(raw, 1)
        tracks.append(
            EffectTrack(
                track_type=track_type,
                texture_slot=texture_slot,
                raw=raw,
            )
        )

    return EffectDefinition(
        index=index,
        authored_frames=authored_frames,
        tracks=tuple(tracks),
    )


def parse_png(payload: bytes) -> PngInfo:
    if not payload.startswith(PNG_SIGNATURE):
        raise ValueError("image payload does not have a PNG signature")
    if len(payload) < 24:
        raise ValueError("PNG IHDR is truncated")

    ihdr_length = read_u32_be(payload, 8)
    if payload[12:16] != b"IHDR" or ihdr_length != 13:
        raise ValueError("PNG does not begin with a valid IHDR chunk")
    ihdr_end = 16 + ihdr_length + 4
    if len(payload) < ihdr_end:
        raise ValueError("PNG IHDR is truncated")

    width = read_u32_be(payload, 16)
    height = read_u32_be(payload, 20)
    if width == 0 or height == 0:
        raise ValueError("PNG dimensions must be positive")
    return PngInfo(
        width=width,
        height=height,
        sha256=hashlib.sha256(payload).hexdigest(),
    )


def _image_sort_key(path: Path) -> tuple[int, int, str, int, str]:
    match = IMAGE_NAME_PATTERN.fullmatch(path.name)
    if match is None:
        raise ValueError(f"unsupported CTune image name: {path.name}")
    return (
        int(match.group("slot")),
        1 if match.group("big") else 0,
        (match.group("language") or "").casefold(),
        0 if match.group("suffix").casefold() == "bin" else 1,
        path.name.casefold(),
    )


def _container_record(path: Path, container: OffsetContainer) -> dict[str, Any]:
    payload = path.read_bytes()
    return {
        "name": path.name,
        "size": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "declared_size": container.declared_size,
        "record_count": container.record_count,
        "offsets": list(container.offsets),
    }


def _definition_record(definition: EffectDefinition) -> dict[str, Any]:
    return {
        "index": definition.index,
        "authored_frames": definition.authored_frames,
        "track_count": len(definition.tracks),
        "tracks": [
            {
                "track_type": track.track_type,
                "texture_slot": track.texture_slot,
                "raw_hex": track.raw.hex(),
            }
            for track in definition.tracks
        ],
    }


def _image_record(path: Path) -> dict[str, Any]:
    payload = path.read_bytes()
    info = parse_png(payload)
    match = IMAGE_NAME_PATTERN.fullmatch(path.name)
    if match is None:
        raise ValueError(f"unsupported CTune image name: {path.name}")
    return {
        "name": path.name,
        "size": len(payload),
        "sha256": info.sha256,
        "width": info.width,
        "height": info.height,
        "slot": int(match.group("slot")),
        "big": match.group("big") is not None,
        "language": match.group("language"),
        "suffix": match.group("suffix").casefold(),
    }


def catalog_effect_directory(root: Path) -> dict[str, object]:
    resolved_root = root.resolve()
    if not resolved_root.is_dir():
        raise ValueError(f"effect root is not a directory: {resolved_root}")

    containers: list[dict[str, Any]] = []
    parsed_containers: dict[str, OffsetContainer] = {}
    for name in CORE_CONTAINER_NAMES:
        path = resolved_root / name
        if not path.is_file():
            raise ValueError(f"required CTune container is missing: {name}")
        payload = path.read_bytes()
        container = parse_offset_container(payload)
        parsed_containers[name] = container
        containers.append(_container_record(path, container))

    definitions = tuple(
        parse_effect_definition(index, record)
        for index, record in enumerate(
            parsed_containers["efcdata.dat"].records
        )
    )
    definition_records = [
        _definition_record(definition) for definition in definitions
    ]

    image_paths = sorted(
        (
            path
            for path in resolved_root.iterdir()
            if path.is_file() and IMAGE_NAME_PATTERN.fullmatch(path.name)
        ),
        key=_image_sort_key,
    )
    if not image_paths:
        raise ValueError("effect directory contains no CTune images")
    images = [_image_record(path) for path in image_paths]

    referenced_slots = sorted(
        {
            track.texture_slot
            for definition in definitions
            for track in definition.tracks
            if track.texture_slot not in (None, 0xFFFF)
        }
    )
    base_variants = {
        (image["slot"], image["big"])
        for image in images
        if image["suffix"] == "bin" and image["language"] is None
    }
    for slot in referenced_slots:
        if (slot, False) not in base_variants or (slot, True) not in base_variants:
            raise ValueError(
                f"texture slot {slot} does not have both "
                f"img{slot}.bin and img_big{slot}.bin"
            )

    return {
        "containers": containers,
        "definitions": definition_records,
        "images": images,
        "referenced_texture_slots": referenced_slots,
    }


def _format_slots(tracks: Sequence[Mapping[str, object]]) -> str:
    slots = sorted(
        {
            int(slot)
            for track in tracks
            if (slot := track["texture_slot"]) not in (None, 0xFFFF)
        }
    )
    return ", ".join(str(slot) for slot in slots) if slots else "-"


def render_catalog_markdown(
    catalog: Mapping[str, object],
    root: Path,
) -> str:
    containers = list(catalog["containers"])
    definitions = list(catalog["definitions"])
    images = list(catalog["images"])
    referenced_slots = list(catalog["referenced_texture_slots"])

    lines = [
        "# CTune Effect Asset Catalog",
        "",
        "This file is generated by `tools/analysis/ctune_effect_catalog.py`.",
        "The source directory is read-only evidence and is not part of GCLoader.",
        "",
        f"Source: `{root.resolve()}`",
        "",
        "## Containers",
        "",
        "| File | Bytes | SHA-256 | Records | Declared bytes |",
        "|---|---:|---|---:|---:|",
    ]
    for item in containers:
        lines.append(
            f"| `{item['name']}` | {item['size']} | "
            f"`{item['sha256']}` | {item['record_count']} | "
            f"{item['declared_size']} |"
        )

    lines.extend(
        [
            "",
            "## Effect definitions",
            "",
            "| Index | Authored frames | Tracks | Texture slots |",
            "|---:|---:|---:|---|",
        ]
    )
    for definition in definitions:
        lines.append(
            f"| {definition['index']} | "
            f"{definition['authored_frames']} | "
            f"{definition['track_count']} | "
            f"{_format_slots(definition['tracks'])} |"
        )

    lines.extend(
        [
            "",
            "## Images",
            "",
            "| File | Bytes | Dimensions | SHA-256 |",
            "|---|---:|---:|---|",
        ]
    )
    for image in images:
        lines.append(
            f"| `{image['name']}` | {image['size']} | "
            f"{image['width']}x{image['height']} | "
            f"`{image['sha256']}` |"
        )

    binary_images = [image for image in images if image["suffix"] == "bin"]
    tutorial = {
        definition["index"]: definition
        for definition in definitions
        if 61 <= definition["index"] <= 69
    }
    lines.extend(
        [
            "",
            "## Coverage canaries",
            "",
            f"- Referenced texture slots: "
            f"{', '.join(str(slot) for slot in referenced_slots)}.",
            f"- `.bin` image payloads with valid PNG headers: "
            f"{len(binary_images)}.",
        ]
    )
    if len(tutorial) == 9:
        durations = ", ".join(
            str(tutorial[index]["authored_frames"])
            for index in range(61, 70)
        )
        tutorial_slots = sorted(
            {
                int(slot)
                for definition in tutorial.values()
                for track in definition["tracks"]
                if (slot := track["texture_slot"]) not in (None, 0xFFFF)
            }
        )
        lines.extend(
            [
                "- Tutorial definitions: 61 through 69.",
                f"- Tutorial authored durations: {durations}.",
                "- Tutorial type-`0x02` texture slots: "
                + ", ".join(str(slot) for slot in tutorial_slots)
                + ".",
            ]
        )

    return "\n".join(lines) + "\n"


def write_catalog_markdown(root: Path, output: Path) -> None:
    resolved_root = root.resolve()
    resolved_output = output.resolve()
    try:
        resolved_output.relative_to(resolved_root)
    except ValueError:
        pass
    else:
        raise ValueError("catalog output cannot be inside the input root")

    markdown = render_catalog_markdown(
        catalog_effect_directory(resolved_root),
        resolved_root,
    )
    resolved_output.parent.mkdir(parents=True, exist_ok=True)
    resolved_output.write_text(markdown, encoding="utf-8", newline="\n")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Catalog Groove Coaster CTune gameplay effect assets."
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args(argv)
    write_catalog_markdown(arguments.root, arguments.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
