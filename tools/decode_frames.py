#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline Internet-envelope decoder. One complete hex frame per non-comment line.

Never connects to a controller or sends data. Does not reassemble text transfers.
Input containing private identities remains private: output is not anonymization.
"""

import argparse
import json
from pathlib import Path


def crc16(data: bytes) -> int:
    """Compute CRC-16/MCRF4XX without mutating input.

    Args:
        data: Complete sequence of octets to checksum, excluding the wire trailer.
    Returns:
        Unsigned 16-bit checksum; wire encoding is big-endian.
    """
    remainder = 0xFFFF
    for byte in data:
        remainder ^= byte
        for _ in range(8):
            remainder = (remainder >> 1) ^ (0x8408 if remainder & 1 else 0)
    return remainder


def decode(packet: bytes) -> dict:
    """Validate and decode one complete Internet-module frame.

    Args:
        packet: Exactly one frame, including header, padding, trailer and CRC.
    Returns:
        JSON-compatible fields or bootstrap metadata; recovery envelopes are marked.
    Raises:
        ValueError: Envelope, checksum or field lengths are invalid.
    """
    if len(packet) < 12 or len(packet) % 4 or packet[:2] != b"\x02\x26":
        raise ValueError("Invalid Internet frame envelope")
    if packet[-4:-2] != b"\x02\x18" or crc16(packet[:-4]) != int.from_bytes(packet[-2:], "big"):
        raise ValueError("Invalid end marker or CRC")
    result = {"address": packet[2:4].hex(), "crc_valid": True}
    if len(packet) == 12 and packet[4:6] == b"\x17\x88" and packet[6:8] in (b"\0\0", b"\1\1"):
        result["bootstrap_code"] = packet[6:8].hex()
        return result
    fields = []
    position, end = 4, len(packet) - 4
    while position < end:
        if end - position < 4:
            raise ValueError("Truncated TLV header")
        length = int.from_bytes(packet[position + 2 : position + 4], "big")
        if (
            packet[position : position + 2] == b"\x04\x00"
            and length in (4, 6)
            and position + 8 <= end
            and packet[position + 4 : position + 6] == b"\x17\x88"
        ):
            result["recovery"] = True
            position += 4
            continue
        padded = (length + 3) & ~3
        if padded > end - position - 4:
            raise ValueError("TLV length exceeds frame")
        fields.append(
            {
                "tag": packet[position : position + 2].hex(),
                "length": length,
                "value": packet[position + 4 : position + 4 + length].hex(" "),
            }
        )
        position += 4 + padded
    result["fields"] = fields
    return result


def main() -> int:
    """Decode a hexadecimal-frame file to JSON lines on stdout.

    Returns:
        Zero if all non-comment lines decode; one if any frame is invalid.
    Raises:
        OSError: The requested input file cannot be read.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    errors = 0
    for number, line in enumerate(args.input.read_text().splitlines(), 1):
        value = line.split("#", 1)[0].strip()
        if not value:
            continue
        try:
            result = decode(bytes.fromhex(value))
        except ValueError as exc:
            errors += 1
            result = {"error": str(exc)}
        print(json.dumps({"line": number, **result}))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
