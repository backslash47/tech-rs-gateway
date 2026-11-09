"""Exercise real browser decoding of concatenated gzip members over HTTP."""

import importlib.util
import json
import os
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("assets", ROOT / "tools/build_assets.py")
assets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assets)


@unittest.skipUnless(os.environ.get("RUN_WEB_TESTS") == "1", "Requires Playwright Chromium")
class DictionaryHttpTests(unittest.TestCase):
    def test_browser_reads_every_label(self):
        from playwright.sync_api import sync_playwright

        dictionaries = {}
        compressed = {}
        block_lengths = {}

        for language in assets.LANGUAGES:
            dictionaries[language] = json.loads(
                (ROOT / f"vendor/emodul/{language}.json").read_text()
            )["data"]
            compressed[language], index = assets.encode_dictionary(dictionaries[language])
            block_lengths[language] = ",".join(str(block[2]) for block in index)

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                language = self.path.removeprefix("/").removesuffix(".json")
                data = compressed.get(language)
                self.send_response(200)
                self.send_header("Content-Type", "application/json" if data else "text/html")

                if data:
                    self.send_header("X-Tech-Dictionary-Blocks", block_lengths[language])

                body = data if data else b"<!doctype html><title>Dictionary test</title>"
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        try:
            with sync_playwright() as playwright:
                options = {"headless": True}

                if os.environ.get("CHROMIUM_EXECUTABLE"):
                    options["executable_path"] = os.environ["CHROMIUM_EXECUTABLE"]

                browser = playwright.chromium.launch(**options)
                page = browser.new_page()
                page.goto(f"http://127.0.0.1:{server.server_port}/")
                page.add_script_tag(path=str(ROOT / "web/dictionary.js"))

                for language in assets.LANGUAGES:
                    received = page.evaluate(
                        "async language => decodeDictionary(await fetch('/' + language + '.json'))",
                        language,
                    )
                    self.assertEqual(received, dictionaries[language])

                browser.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
