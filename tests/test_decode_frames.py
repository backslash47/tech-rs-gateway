# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("decoder", ROOT / "tools/decode_frames.py")
decoder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(decoder)


class FrameTests(unittest.TestCase):
    def test_truncated_route_recovery(self):
        payload = bytes.fromhex("02 26 ff f4 04 00 00 06 17 88 00 02 03 02 00 00")
        packet = payload + bytes.fromhex("02 18") + decoder.crc16(payload).to_bytes(2, "big")
        frame = decoder.decode(packet)
        self.assertTrue(frame["recovery"])
        self.assertEqual(frame["fields"], [{"tag": "1788", "length": 2, "value": "03 02"}])

    def test_observed_write_exchange(self):
        lines = (ROOT / "tests/fixtures/write-transport.hex").read_text().splitlines()
        packets = [bytes.fromhex(line) for line in lines if line and not line.startswith("#")]
        self.assertEqual(len(packets), 5)

        for packet in packets:
            self.assertTrue(decoder.decode(packet)["crc_valid"])

        frame = decoder.decode(packets[2])
        message = next(field for field in frame["fields"] if field["tag"] == "0400")
        self.assertEqual(bytes.fromhex(message["value"]), b"\x03\x01<X|0.0.1|8307,34>")

    def test_vectors(self):
        self.assertEqual(decoder.crc16(b"123456789"), 0x6F91)
        for line in (ROOT / "tests/fixtures/internet-envelope.hex").read_text().splitlines():
            if line.strip() and not line.startswith("#"):
                packet = bytes.fromhex(line)
                self.assertTrue(decoder.decode(packet)["crc_valid"])
                for end in range(len(packet)):
                    with self.assertRaises(ValueError):
                        decoder.decode(packet[:end])

    def test_corruption(self):
        packet = bytearray.fromhex("02 26 ff f4 17 88 00 00 02 18 5d 9f")
        packet[4] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            decoder.decode(packet)

    def test_cli_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.hex"
            path.write_text("# synthetic invalid fixture\nnot hex\n")
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/decode_frames.py"), str(path)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn('"error"', result.stdout)


if __name__ == "__main__":
    unittest.main()
