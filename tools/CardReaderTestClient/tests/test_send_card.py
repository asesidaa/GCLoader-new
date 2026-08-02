from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "send_card.py"


def load_send_card():
    spec = importlib.util.spec_from_file_location("send_card", SCRIPT_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {SCRIPT_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


send_card = load_send_card() if SCRIPT_PATH.is_file() else None


class CardReaderPythonClientTests(unittest.TestCase):
    def test_example_script_exists(self):
        self.assertTrue(SCRIPT_PATH.is_file())

    @unittest.skipIf(send_card is None, "send_card.py does not exist yet")
    def test_encode_card_number_accepts_exact_ascii_digits(self):
        self.assertEqual(
            send_card.encode_card_number("1234567890123456"),
            b"1234567890123456",
        )

    @unittest.skipIf(send_card is None, "send_card.py does not exist yet")
    def test_encode_card_number_rejects_malformed_values(self):
        for value in (
            "123456789012345",
            "12345678901234567",
            "123456789012345X",
            "１２３４５６７８９０１２３４５６",
        ):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    send_card.encode_card_number(value)

    @unittest.skipIf(send_card is None, "send_card.py does not exist yet")
    def test_classify_response_requires_an_exact_protocol_response(self):
        self.assertEqual(send_card.classify_response(b"OK"), "OK")
        self.assertEqual(
            send_card.classify_response(b"INVALID"),
            "INVALID",
        )
        for response in (b"", b"OK\0", b"UNKNOWN"):
            with self.subTest(response=response):
                with self.assertRaises(RuntimeError):
                    send_card.classify_response(response)


if __name__ == "__main__":
    unittest.main()
