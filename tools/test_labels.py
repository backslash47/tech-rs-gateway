#!/usr/bin/env python3
"""Compare every C++ compressed lookup with the original vendor JSON."""

import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Compare every vendor label with the host C++ compressed-dictionary lookup.

    Requires generated assets and host zlib. Writes a test executable under
    build/; compilation, execution and comparison failures terminate the check.
    """
    output = ROOT / "build/test_compressed_labels"
    output.parent.mkdir(exist_ok=True)
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            str(ROOT / "tests/test_compressed_labels.cpp"),
            str(ROOT / "components/tech_rs_gateway/compressed_labels.cpp"),
            str(ROOT / "components/tech_rs_gateway/dynamic_menu_assets.cpp"),
            str(ROOT / "components/tech_rs_gateway/label_dictionary_data.cpp"),
            "-lz",
            "-o",
            str(output),
        ],
        check=True,
    )

    for language in ("cs", "pl", "en"):
        labels = json.loads((ROOT / f"vendor/emodul/{language}.json").read_text())["data"]
        codes = sorted(labels, key=int)
        started = time.perf_counter()
        result = subprocess.run(
            [str(output), language],
            input="\n".join(codes) + "\n",
            capture_output=True,
            text=True,
            check=True,
        )
        lines = result.stdout.rstrip("\n").split("\n")
        assert len(lines) == len(codes), result.stderr

        for code, literal in zip(codes, lines):
            assert json.loads(literal) == labels[code], (language, code)

        print(
            f"PASS: {language}: all {len(codes)} C++ lookups match JSON "
            f"({time.perf_counter() - started:.3f}s on host, including sanitizers)"
        )


if __name__ == "__main__":
    main()
