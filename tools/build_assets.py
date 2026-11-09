#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate shared gzip dictionaries and the standalone web page."""

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "components/tech_rs_gateway"
LABEL_BLOCK_SIZE = 16384
LANGUAGES = ("cs", "pl", "en")


def build_identity(language="en", root=ROOT):
    """Identify the application version and reproducible source/asset build.

    Args:
        language: Selected MQTT dictionary language.
        root: Source root; local secrets and generated outputs are excluded.
    Returns:
        Version, source fingerprint and MQTT language for firmware and web metadata.
    Raises:
        ValueError: The version or language is invalid.
    """
    version = (root / "VERSION").read_text().strip()
    if language not in LANGUAGES or not re.fullmatch(
        r"\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?", version
    ):
        raise ValueError("Invalid application version or MQTT language")
    generated = {
        "dynamic_menu_assets.h",
        "dynamic_menu_assets.cpp",
        "label_dictionary_data.h",
        "label_dictionary_data.cpp",
        "build_info.h",
    }
    files = [root / "VERSION"]
    for folder, suffixes in (
        ("components/tech_rs_gateway", {".h", ".cpp"}),
        ("web", {".js", ".html", ".css", ".json"}),
        ("vendor/emodul", {".json"}),
    ):
        files.extend(
            p
            for p in (root / folder).rglob("*")
            if p.is_file() and p.suffix in suffixes and p.name not in generated
        )
    for name in ("tools/build_assets.py", "requirements-dev.txt", "config/esp32-c3-lpkit.yaml"):
        if (root / name).is_file():
            files.append(root / name)
    digest = hashlib.sha256(language.encode())
    for path in sorted(files):
        digest.update(path.relative_to(root).as_posix().encode() + b"\0")
        digest.update(path.read_bytes() + b"\0")
    return {"version": version, "build": digest.hexdigest()[:16], "mqtt_language": language}


def render_html(identity=None):
    """Inline web sources and interface locales into a standalone HTML page.

    Args:
        identity: Build identity to embed; defaults to the current English MQTT build.
    Returns:
        HTML text; no files are written by this function.
    Raises:
        OSError: A required source file cannot be read.
        ValueError: A locale JSON file is malformed.
    """
    web = ROOT / "web"
    html = (web / "index.html").read_text()
    locales = {
        language: json.loads((web / "locales" / f"{language}.json").read_text())
        for language in LANGUAGES
    }
    scripts = {
        "build-info.js": "const BUILD_INFO = " + json.dumps(identity or build_identity()) + ";",
        "locales.js": "const UI_LOCALES = " + json.dumps(locales, ensure_ascii=False) + ";",
        "menu-complex.js": (web / "menu-complex.js").read_text(),
        "dictionary.js": (web / "dictionary.js").read_text(),
        "app.js": (web / "app.js").read_text(),
        "state.js": (web / "state.js").read_text(),
        "ui.js": (web / "ui.js").read_text(),
        "menu-api.js": (web / "menu-api.js").read_text(),
        "menu-editors.js": (web / "menu-editors.js").read_text(),
        "menu-renderer.js": (web / "menu-renderer.js").read_text(),
        "menu-dialogs.js": (web / "menu-dialogs.js").read_text(),
        "mqtt-settings.js": (web / "mqtt-settings.js").read_text(),
    }

    for name, source in scripts.items():
        html = html.replace(f'<script src="{name}"></script>', "<script>" + source + "</script>")

    return html.replace(
        '<link rel="stylesheet" href="style.css" />',
        "<style>" + (web / "style.css").read_text() + "</style>",
    )


def encode_dictionary(labels, block_size=LABEL_BLOCK_SIZE):
    """Independent gzip members whose concatenated output is one JSON object.

    Each member begins at a key boundary. The block index stores only the first
    numeric key and byte extents, not another copy of any label or per-key index.

    Args:
        labels: Mapping of canonical uint32 decimal keys to label strings.
        block_size: Maximum decoded fragment size in bytes.
    Returns:
        Compressed bytes and index tuples of first code, byte offset, compressed
        byte length and decoded byte length.
    Raises:
        ValueError: The mapping is empty/invalid or an entry exceeds the limit.
    """
    if not labels:
        raise ValueError("Dictionary is empty")

    entries = sorted(labels.items(), key=lambda item: int(item[0]))
    blocks = []
    current = b"{"
    first_code = int(entries[0][0])

    for index, (key, value) in enumerate(entries):
        code = int(key)

        if str(code) != key or not 0 <= code <= 0xFFFFFFFF or not isinstance(value, str):
            raise ValueError("Dictionary requires canonical uint32 keys and string values")

        prefix = "" if index == 0 else ","
        entry = (prefix + json.dumps(key) + ":" + json.dumps(value, ensure_ascii=False)).encode()

        if len(entry) + 2 > block_size:
            raise ValueError(f"Label {key} exceeds the block memory limit")

        if len(current) + len(entry) + 1 > block_size:
            blocks.append((first_code, current))
            current = b""
            first_code = code

        current += entry

    blocks.append((first_code, current + b"}"))
    compressed = bytearray()
    index = []

    for first_code, raw in blocks:
        member = gzip.compress(raw, mtime=0)
        index.append((first_code, len(compressed), len(member), len(raw)))
        compressed.extend(member)

    return bytes(compressed), index


def write_array(output, name, data):
    """Write a C++ byte-array definition to an existing text stream.

    Args:
        output: Writable text stream; ownership stays with the caller.
        name: Valid C++ identifier chosen by the generator.
        data: Bytes to embed without modification.
    """
    output.write(f"const uint8_t {name}[] = {{\n")

    for start in range(0, len(data), 24):
        output.write("    " + ", ".join(str(value) for value in data[start : start + 24]) + ",\n")

    output.write("};\n\n")


def main():
    """Parse the MQTT language option and write generated header/source assets.

    Reads local web/vendor files only. Invalid inputs or filesystem errors
    propagate to the CLI; generated outputs overwrite previous build artifacts.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mqtt-language", choices=LANGUAGES, default="en")
    args = parser.parse_args()
    identity = build_identity(args.mqtt_language)
    (OUT / "build_info.h").write_text(
        "// Generated application identity.\n#pragma once\n"
        "namespace tech_rs_gateway::build {\n"
        f"inline constexpr char VERSION[] = {json.dumps(identity['version'])};\n"
        f"inline constexpr char ID[] = {json.dumps(identity['build'])};\n"
        f"inline constexpr char SOFTWARE_VERSION[] = {json.dumps(identity['version'] + ' (' + identity['build'] + ')')};\n"
        "}\n"
    )
    notice = "// Generated from eModul dictionaries; see vendor/emodul/README.md.\n"
    indexes = {}
    assets = {"menu_html_gz": gzip.compress(render_html(identity).encode(), mtime=0)}

    for language in LANGUAGES:
        labels = json.loads((ROOT / f"vendor/emodul/{language}.json").read_text())["data"]
        data, index = encode_dictionary(labels)
        assets[f"menu_labels_{language}_gz"] = data
        indexes[language] = index
        print(language, len(labels), "labels;", len(data), "gzip bytes;", len(index), "blocks")

    with (OUT / "dynamic_menu_assets.h").open("w") as output:
        output.write(notice + "#pragma once\n#include <cstdint>\n\n")

        for name, data in assets.items():
            output.write(f"extern const uint8_t {name}[{len(data)}];\n")

    with (OUT / "dynamic_menu_assets.cpp").open("w") as output:
        output.write(notice + '#include "dynamic_menu_assets.h"\n\n')

        for name, data in assets.items():
            write_array(output, name, data)

    with (OUT / "label_dictionary_data.h").open("w") as output:
        output.write(notice + '#pragma once\n#include "compressed_labels.h"\n')

        for language, index in indexes.items():
            output.write(
                f"extern const tech_rs_gateway::LabelBlock label_blocks_{language}[{len(index)}];\n"
            )

        output.write("extern const tech_rs_gateway::LabelDictionary mqtt_dictionary;\n")

    with (OUT / "label_dictionary_data.cpp").open("w") as output:
        output.write(notice + '#include "label_dictionary_data.h"\n')
        output.write('#include "dynamic_menu_assets.h"\n\n')

        for language, index in indexes.items():
            output.write(f"const tech_rs_gateway::LabelBlock label_blocks_{language}[] = {{\n")

            for first, offset, compressed, unpacked in index:
                output.write(f"    {{{first}, {offset}, {compressed}, {unpacked}}},\n")

            output.write("};\n\n")

        language = args.mqtt_language
        output.write("const tech_rs_gateway::LabelDictionary mqtt_dictionary = {\n")
        output.write(f"    menu_labels_{language}_gz, sizeof(menu_labels_{language}_gz),\n")
        output.write(f"    label_blocks_{language}, {len(indexes[language])}\n}};\n")


if __name__ == "__main__":
    main()
