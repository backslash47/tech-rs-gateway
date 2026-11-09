"""Verify shared dictionary members, indexes and standalone web packaging."""

import gzip
import importlib.util
import json
from pathlib import Path
import unittest
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("assets", ROOT / "tools/build_assets.py")
assets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assets)


class AssetTests(unittest.TestCase):
    def test_build_identity_is_reproducible_and_sensitive_to_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "VERSION").write_text("0.1.0-dev\n")
            component = root / "components/tech_rs_gateway"
            component.mkdir(parents=True)
            source = component / "example.cpp"
            source.write_text("int value = 1;\n")
            first = assets.build_identity("en", root)
            self.assertEqual(first, assets.build_identity("en", root))
            self.assertNotEqual(first["build"], assets.build_identity("cs", root)["build"])
            (component / "build_info.h").write_text("generated")
            self.assertEqual(first, assets.build_identity("en", root))
            source.write_text("int value = 2;\n")
            self.assertNotEqual(first["build"], assets.build_identity("en", root)["build"])
            (root / "VERSION").write_text("invalid")
            with self.assertRaises(ValueError):
                assets.build_identity("en", root)

    def test_standalone_web(self):
        html = assets.render_html()
        self.assertNotIn('src="app.js"', html)
        self.assertNotIn('href="style.css"', html)
        self.assertIn("const UI_LOCALES =", html)

    def test_all_labels_and_members(self):
        for language in assets.LANGUAGES:
            labels = json.loads((ROOT / f"vendor/emodul/{language}.json").read_text())["data"]
            compressed, index = assets.encode_dictionary(labels)
            self.assertEqual(json.loads(gzip.decompress(compressed)), labels)
            found = {}

            for block_number, (first, offset, length, unpacked) in enumerate(index):
                raw = gzip.decompress(compressed[offset : offset + length])
                self.assertEqual(len(raw), unpacked)
                self.assertLessEqual(unpacked, assets.LABEL_BLOCK_SIZE)
                fragment = raw.decode().lstrip("{,").rstrip("}")
                values = json.loads("{" + fragment + "}")
                self.assertEqual(min(map(int, values)), first)
                found.update(values)

                if block_number + 1 < len(index):
                    self.assertLess(max(map(int, values)), index[block_number + 1][0])

            self.assertEqual(found, labels)

    def test_escaping_boundaries_and_reproducibility(self):
        labels = {"1": 'a "quote"', "7": "\\backslash\n", "99": "Žluťoučký", "10000": ""}
        data, index = assets.encode_dictionary(labels, block_size=32)
        self.assertGreater(len(index), 1)
        self.assertEqual(json.loads(gzip.decompress(data)), labels)
        self.assertEqual((data, index), assets.encode_dictionary(labels, block_size=32))

    def test_invalid_data(self):
        for labels in ({}, {"01": "x"}, {"-1": "x"}, {"1": 4}, {"1": "x" * 20000}):
            with self.assertRaises(ValueError):
                assets.encode_dictionary(labels)


if __name__ == "__main__":
    unittest.main()
