# SPDX-License-Identifier: MIT
"""UI tests use only synthetic controller data and intercepted HTTP requests."""

import json
import importlib.util
import os
from pathlib import Path
import unittest
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parents[1]
ASSET_SPEC = importlib.util.spec_from_file_location("assets", ROOT / "tools/build_assets.py")
ASSETS = importlib.util.module_from_spec(ASSET_SPEC)
ASSET_SPEC.loader.exec_module(ASSETS)


@unittest.skipUnless(
    os.environ.get("RUN_WEB_TESTS") == "1", "Set RUN_WEB_TESTS=1 and install Playwright Chromium"
)
class WebTests(unittest.TestCase):
    def setUp(self):
        from playwright.sync_api import sync_playwright

        self.p = sync_playwright().start()
        options = {"headless": True}
        if os.environ.get("CHROMIUM_EXECUTABLE"):
            options["executable_path"] = os.environ["CHROMIUM_EXECUTABLE"]
        self.browser = self.p.chromium.launch(**options)
        self.page = self.browser.new_page(viewport={"width": 390, "height": 844})
        self.value = "30"
        self.online = True
        self.actions = False
        self.writes = []
        self.result = {"code": "idle", "item": "", "value": None}
        self.write_error = None
        self.confirm_writes = True
        self.page.route("**/*", self.serve)
        self.page.goto("http://gateway.test/")
        self.page.locator('input[data-key="MU:101"]').wait_for()

    def tearDown(self):
        self.browser.close()
        self.p.stop()

    def serve(self, route):
        url = urlparse(route.request.url)
        query = parse_qs(url.query)
        if url.hostname != "gateway.test":
            raise AssertionError("Unexpected external access: " + url.hostname)
        if url.path == "/":
            html = ASSETS.render_html()
            route.fulfill(content_type="text/html", body=html)
            return
        if url.path == "/labels.gz":
            lang = query.get("lang", ["en"])[0]
            data = {
                "759": {"en": "Water target", "cs": "Cíl vody", "pl": "Cel wody"}[lang],
                "388": "Menu",
                "312": "Action",
                "319": "Run action?",
                "1035": "°C",
            }
        elif url.path == "/api/menu":
            data = {
                "online": self.online,
                "busy": False,
                "overflow": False,
                "token": "synthetic-test-token",
                "result": self.result,
                "total": 3,
                "next": None,
                "items": [
                    {
                        "key": "MU:0",
                        "raw": "0,0,0,1,388,0,0",
                        "visible": True,
                        "writable": False,
                        "action": False,
                        "age": 0,
                    },
                    {
                        "key": "MU:101",
                        "raw": f"101,0,1,1,759,0,0,{self.value},10,50,30,1035",
                        "visible": True,
                        "writable": True,
                        "action": False,
                        "age": 0,
                    },
                    {
                        "key": "MU:102",
                        "raw": "102,0,20,1,312,0,0,319,1,-1,0",
                        "visible": True,
                        "writable": False,
                        "action": self.actions,
                        "age": 0,
                    },
                ],
            }
        elif url.path == "/api/write":
            assert route.request.method == "POST"
            assert route.request.headers["x-tech-token"] == "synthetic-test-token"
            self.writes.append(query)
            if self.write_error:
                route.fulfill(
                    status=409,
                    content_type="application/json",
                    body=json.dumps({"code": self.write_error, "item": "MU:101", "value": None}),
                )
                return
            if self.confirm_writes:
                self.value = query["value"][0]
            self.result = {
                "code": "write_confirmed" if self.confirm_writes else "write_queued",
                "item": "MU:101",
                "value": int(query["value"][0]),
            }
            data = {"code": "write_queued", "item": "MU:101", "value": int(query["value"][0])}
        else:
            route.fulfill(status=404, body="Not a test endpoint")
            return
        route.fulfill(content_type="application/json", body=json.dumps(data))

    def test_edit_language_and_write(self):
        field = self.page.locator('input[data-key="MU:101"]')
        field.fill("31")
        self.page.evaluate('async()=>{await load();await setLanguage("pl")}')
        self.assertEqual(field.input_value(), "31")
        self.page.evaluate('async()=>await setLanguage("en")')
        self.page.locator("#menu form .save").click()
        self.page.wait_for_function(
            'document.querySelector("input[data-key]").value==="31" && !appState.submitting'
        )
        self.assertEqual(self.writes, [{"key": ["MU:101"], "value": ["31"], "expected": ["30"]}])
        self.assertFalse(self.page.evaluate("document.documentElement.scrollWidth>innerWidth"))

    def test_application_identity_in_about_dialog(self):
        self.page.locator("#about").click()
        identity = ASSETS.build_identity()
        content = self.page.locator("#detail-meta").inner_text()
        self.assertIn(identity["version"], content)
        self.assertIn(identity["build"], content)
        self.assertFalse(self.page.evaluate("document.documentElement.scrollWidth>innerWidth"))

    def test_offline_and_actions(self):
        self.assertEqual(self.page.locator(".action-button").count(), 0)
        self.page.locator('input[data-key="MU:101"]').fill("31")
        self.online = False
        self.page.evaluate("async()=>{await load();render()}")
        self.assertTrue(self.page.locator("#menu form .save").is_disabled())
        self.online = True
        self.actions = True
        self.page.evaluate("async()=>{await load();render()}")
        self.page.locator(".action-button").click()
        self.assertTrue(self.page.locator("#complex").evaluate("(d)=>d.open"))
        self.assertIn("Run action?", self.page.locator("#complex").inner_text())
        self.page.locator("#complex-close").click()
        self.assertEqual(self.writes, [])  # Opening/canceling the dialog never sends.

    def test_structured_conflict_preserves_draft(self):
        self.write_error = "value_conflict"
        field = self.page.locator('input[data-key="MU:101"]')
        field.fill("31")
        self.page.locator("#menu form .save").click()
        self.page.wait_for_function(
            "!appState.submitting && document.getElementById('message').textContent.includes('Value changed')"
        )
        self.assertEqual(field.input_value(), "31")

    def test_queued_is_not_confirmed(self):
        self.confirm_writes = False
        self.page.locator('input[data-key="MU:101"]').fill("31")
        self.page.locator("#menu form .save").click()
        self.page.wait_for_function(
            "!appState.submitting && appState.meta.result.code === 'write_queued'"
        )
        self.assertNotIn("Confirmed", self.page.locator("#status").inner_text())
        self.value = "31"
        self.result = {"code": "write_confirmed", "item": "MU:101", "value": 31}
        self.page.evaluate("async()=>await load()")
        self.assertIn("Confirmed", self.page.locator("#status").inner_text())


if __name__ == "__main__":
    unittest.main()
