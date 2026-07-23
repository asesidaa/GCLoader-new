from __future__ import annotations

import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

from tools.analysis.ctune_effect_catalog import (
    catalog_effect_directory,
    parse_effect_definition,
    parse_offset_container,
    parse_png,
    read_u16_be,
    read_u32_be,
    render_catalog_markdown,
    write_catalog_markdown,
)


def make_container(records: list[bytes]) -> bytes:
    header_size = 6 + 4 * (len(records) + 1)
    offsets = [header_size]
    for record in records:
        offsets.append(offsets[-1] + len(record))
    body = b"".join(records)
    declared_size = header_size + len(body)
    return (
        declared_size.to_bytes(4, "big")
        + len(records).to_bytes(2, "big")
        + b"".join(offset.to_bytes(4, "big") for offset in offsets)
        + body
    )


def make_effect_definition(
    authored_frames: int,
    tracks: list[bytes],
) -> bytes:
    table_size = 3 + 2 * len(tracks)
    offsets: list[int] = []
    cursor = table_size
    for track in tracks:
        offsets.append(cursor)
        cursor += len(track)
    return (
        authored_frames.to_bytes(2, "big")
        + bytes([len(tracks)])
        + b"".join(offset.to_bytes(2, "big") for offset in offsets)
        + b"".join(tracks)
    )


def make_png(width: int, height: int) -> bytes:
    payload = struct.pack(">II", width, height) + b"\x08\x06\x00\x00\x00"
    return (
        b"\x89PNG\r\n\x1a\n"
        + struct.pack(">I", len(payload))
        + b"IHDR"
        + payload
        + b"\x00\x00\x00\x00"
    )


class IntegerReaderTests(unittest.TestCase):
    def test_big_endian_integer_readers(self) -> None:
        data = bytes.fromhex("12 34 56 78 9A BC")
        self.assertEqual(read_u16_be(data, 0), 0x1234)
        self.assertEqual(read_u32_be(data, 1), 0x3456789A)

    def test_integer_readers_reject_out_of_bounds_offsets(self) -> None:
        with self.assertRaises(ValueError):
            read_u16_be(b"\x00", 0)
        with self.assertRaises(ValueError):
            read_u32_be(b"\x00\x00\x00\x00", 1)
        with self.assertRaises(ValueError):
            read_u16_be(b"\x00\x00", -1)


class OffsetContainerTests(unittest.TestCase):
    def test_parses_declared_size_offsets_and_records(self) -> None:
        payload = make_container([b"abc", b"de"])
        container = parse_offset_container(payload)

        self.assertEqual(container.declared_size, len(payload))
        self.assertEqual(container.record_count, 2)
        self.assertEqual(container.offsets, (18, 21, 23))
        self.assertEqual(container.records, (b"abc", b"de"))

    def test_rejects_declared_size_mismatch(self) -> None:
        payload = bytearray(make_container([b"abc"]))
        payload[0:4] = (len(payload) + 1).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "declared size"):
            parse_offset_container(bytes(payload))

    def test_rejects_truncated_offset_table(self) -> None:
        payload = b"\x00\x00\x00\x0e\x00\x01" + b"\x00\x00\x00\x0e"
        with self.assertRaisesRegex(ValueError, "offset table"):
            parse_offset_container(payload)

    def test_rejects_wrong_first_record_offset(self) -> None:
        payload = bytearray(make_container([b"abc"]))
        payload[6:10] = (15).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "first record"):
            parse_offset_container(bytes(payload))

    def test_rejects_non_increasing_offsets(self) -> None:
        payload = bytearray(make_container([b"abc", b"de"]))
        payload[10:14] = (18).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            parse_offset_container(bytes(payload))

    def test_rejects_out_of_bounds_or_wrong_final_offset(self) -> None:
        payload = bytearray(make_container([b"abc"]))
        payload[10:14] = (len(payload) + 1).to_bytes(4, "big")
        with self.assertRaisesRegex(ValueError, "final offset"):
            parse_offset_container(bytes(payload))


class EffectDefinitionTests(unittest.TestCase):
    def test_parses_authored_length_track_types_and_texture_slot(self) -> None:
        record = make_effect_definition(
            38,
            [b"\x02\x00\x0d\xaa", b"\x00\xff\xff"],
        )
        definition = parse_effect_definition(61, record)

        self.assertEqual(definition.index, 61)
        self.assertEqual(definition.authored_frames, 38)
        self.assertEqual(len(definition.tracks), 2)
        self.assertEqual(definition.tracks[0].track_type, 2)
        self.assertEqual(definition.tracks[0].texture_slot, 13)
        self.assertEqual(definition.tracks[0].raw, b"\x02\x00\x0d\xaa")
        self.assertEqual(definition.tracks[1].track_type, 0)
        self.assertIsNone(definition.tracks[1].texture_slot)

    def test_rejects_truncated_header_or_track_table(self) -> None:
        with self.assertRaisesRegex(ValueError, "definition header"):
            parse_effect_definition(0, b"\x00\x01")
        with self.assertRaisesRegex(ValueError, "track offset table"):
            parse_effect_definition(0, b"\x00\x01\x02\x00")

    def test_rejects_track_offset_inside_table(self) -> None:
        record = bytearray(make_effect_definition(10, [b"\x00\xff\xff"]))
        record[3:5] = (4).to_bytes(2, "big")
        with self.assertRaisesRegex(ValueError, "first track"):
            parse_effect_definition(0, bytes(record))

    def test_rejects_non_increasing_track_offsets(self) -> None:
        record = bytearray(
            make_effect_definition(10, [b"\x00\xff\xff", b"\x00\xff\xff"])
        )
        record[5:7] = record[3:5]
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            parse_effect_definition(0, bytes(record))

    def test_rejects_truncated_type_two_track(self) -> None:
        record = make_effect_definition(10, [b"\x02\x00"])
        with self.assertRaisesRegex(ValueError, "texture slot"):
            parse_effect_definition(0, record)


class PngTests(unittest.TestCase):
    def test_parses_signature_ihdr_dimensions_and_hash(self) -> None:
        payload = make_png(512, 256)
        info = parse_png(payload)

        self.assertEqual(info.width, 512)
        self.assertEqual(info.height, 256)
        self.assertEqual(info.sha256, hashlib.sha256(payload).hexdigest())

    def test_rejects_bad_signature_missing_ihdr_and_zero_dimensions(self) -> None:
        with self.assertRaisesRegex(ValueError, "PNG signature"):
            parse_png(b"not a png")

        missing_ihdr = bytearray(make_png(1, 1))
        missing_ihdr[12:16] = b"IDAT"
        with self.assertRaisesRegex(ValueError, "IHDR"):
            parse_png(bytes(missing_ihdr))

        with self.assertRaisesRegex(ValueError, "dimensions"):
            parse_png(make_png(0, 1))

    def test_rejects_truncated_ihdr(self) -> None:
        with self.assertRaisesRegex(ValueError, "IHDR"):
            parse_png(make_png(1, 1)[:20])


class CatalogTests(unittest.TestCase):
    def populate_root(self, root: Path) -> None:
        definition = make_effect_definition(38, [b"\x02\x00\x02"])
        (root / "efcdata.dat").write_bytes(make_container([definition]))
        (root / "effect.dat").write_bytes(make_container([b"effect"]))
        (root / "uvdata.dat").write_bytes(make_container([b"uv"]))
        for name, size in (
            ("img2.bin", (64, 32)),
            ("img_big2.bin", (128, 64)),
            ("img10.bin", (32, 16)),
            ("img_big10.bin", (64, 32)),
            ("img_big2_eng.bin", (128, 64)),
        ):
            (root / name).write_bytes(make_png(*size))

    def test_catalog_is_read_only_and_naturally_sorted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.populate_root(root)
            before = {
                path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in root.iterdir()
            }

            catalog = catalog_effect_directory(root)

            after = {
                path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in root.iterdir()
            }
            self.assertEqual(after, before)
            self.assertEqual(
                [item["name"] for item in catalog["containers"]],
                ["efcdata.dat", "effect.dat", "uvdata.dat"],
            )
            self.assertEqual(
                [item["name"] for item in catalog["images"]],
                [
                    "img2.bin",
                    "img_big2.bin",
                    "img_big2_eng.bin",
                    "img10.bin",
                    "img_big10.bin",
                ],
            )
            self.assertEqual(catalog["referenced_texture_slots"], [2])
            self.assertEqual(catalog["definitions"][0]["authored_frames"], 38)

    def test_catalog_rejects_missing_base_texture_variant(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.populate_root(root)
            (root / "img_big2.bin").unlink()

            with self.assertRaisesRegex(ValueError, "texture slot 2"):
                catalog_effect_directory(root)

    def test_catalog_excludes_ffff_texture_sentinel_from_cross_reference(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            definition = make_effect_definition(10, [b"\x02\xff\xff"])
            (root / "efcdata.dat").write_bytes(make_container([definition]))
            (root / "effect.dat").write_bytes(make_container([b"effect"]))
            (root / "uvdata.dat").write_bytes(make_container([b"uv"]))
            (root / "img0.bin").write_bytes(make_png(32, 32))
            (root / "img_big0.bin").write_bytes(make_png(64, 64))

            catalog = catalog_effect_directory(root)

            self.assertEqual(catalog["referenced_texture_slots"], [])
            self.assertEqual(
                catalog["definitions"][0]["tracks"][0]["texture_slot"],
                0xFFFF,
            )

    def test_markdown_render_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.populate_root(root)
            catalog = catalog_effect_directory(root)

            first = render_catalog_markdown(catalog, root)
            second = render_catalog_markdown(catalog, root)

            self.assertEqual(first, second)
            self.assertIn("# CTune Effect Asset Catalog", first)
            self.assertIn("| `efcdata.dat` |", first)
            self.assertLess(first.index("`img2.bin`"), first.index("`img10.bin`"))

    def test_writer_rejects_output_inside_input_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.populate_root(root)
            output = root / "catalog.md"

            with self.assertRaisesRegex(ValueError, "inside the input root"):
                write_catalog_markdown(root, output)
            self.assertFalse(output.exists())

    def test_writer_creates_only_external_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root = base / "input"
            root.mkdir()
            self.populate_root(root)
            output = base / "evidence" / "catalog.md"

            write_catalog_markdown(root, output)

            self.assertTrue(output.is_file())
            self.assertIn("# CTune Effect Asset Catalog", output.read_text("utf-8"))


if __name__ == "__main__":
    unittest.main()
