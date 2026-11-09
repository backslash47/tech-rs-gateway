#!/usr/bin/env python3
"""Apply the pinned clang-format style and separate control-flow blocks."""

import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GENERATED = {
    "build_info.h",
    "dynamic_menu_assets.h",
    "dynamic_menu_assets.cpp",
    "label_dictionary_data.h",
    "label_dictionary_data.cpp",
}


def separate_blocks(source):
    """Apply project blank-line rules to clang-formatted source text.

    Args:
        source: Formatted C++ text; generated assets are excluded by the caller.
    Returns:
        Text with separated control-flow blocks and a final newline. This is a
        formatting heuristic, not a C++ parser or semantic transformation.
    """
    lines = source.splitlines()
    result = []

    for line in lines:
        if result and line.strip():
            previous = result[-1]
            starts_control = re.match(r"\s*(if|for|while|switch|try)\b", line)
            follows_block = previous.strip() == "}" and (
                len(previous) - len(previous.lstrip()) == len(line) - len(line.lstrip())
            )
            continues_block = line.lstrip().startswith(("else", "catch", "}", ")"))

            if (starts_control and previous.rstrip().endswith((";", "}"))) or (
                follows_block and not continues_block
            ):
                result.append("")

        result.append(line)

    return "\n".join(result) + "\n"


def main():
    """Format handwritten C++ files or report differences with --check.

    Check mode leaves files unchanged and exits nonzero on differences.
    Subprocess and filesystem failures propagate to the CLI.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    changed = []
    paths = list((ROOT / "components/tech_rs_gateway").glob("*.h"))
    paths += list((ROOT / "components/tech_rs_gateway").glob("*.cpp"))
    paths += list((ROOT / "tests").glob("*.cpp"))
    paths += list((ROOT / "tests").rglob("*.h"))

    for path in paths:
        if path.name in GENERATED:
            continue

        formatted = subprocess.check_output(["clang-format", str(path)], text=True)
        formatted = separate_blocks(formatted)

        if formatted != path.read_text():
            changed.append(str(path.relative_to(ROOT)))

            if not args.check:
                path.write_text(formatted)

    if args.check and changed:
        raise SystemExit("Formatting required: " + ", ".join(changed))


if __name__ == "__main__":
    main()
