"""Actual local broker with the production C++ projection/router and a synthetic TECH peer.

This checks topic/Discovery/readback contracts, not the ESP-IDF network driver.
No device or production broker is contacted.
"""

import asyncio
import importlib.util
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(
    os.environ.get("RUN_MQTT_TESTS") == "1", "Set RUN_MQTT_TESTS=1 for local broker tests"
)
class BrokerTests(unittest.TestCase):
    def test_broker_discovery_and_commands(self):
        from amqtt.broker import Broker
        import paho.mqtt.client as mqtt

        spec = importlib.util.spec_from_file_location("host", ROOT / "tools/test_host.py")
        host = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(host)
        binary = ROOT / "build/mqtt_bridge_harness"
        subprocess.run([sys.executable, str(ROOT / "tools/build_assets.py")], check=True)
        binary.parent.mkdir(exist_ok=True)
        subprocess.run(
            [
                os.environ.get("CXX", "c++"),
                "-std=c++17",
                "-pthread",
                str(ROOT / "tests/mqtt_bridge_harness.cpp"),
                *[str(host.SOURCE / source) for source in host.CORE],
                str(host.SOURCE / "mqtt_projection.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )

        with socket.socket() as temporary:
            temporary.bind(("127.0.0.1", 0))
            port = temporary.getsockname()[1]
        loop = asyncio.new_event_loop()
        thread = threading.Thread(target=loop.run_forever, daemon=True)
        thread.start()

        async def start_broker():
            broker = Broker(
                {
                    "listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{port}"}},
                    "plugins": {
                        "amqtt.plugins.authentication.AnonymousAuthPlugin": {
                            "allow_anonymous": True
                        }
                    },
                }
            )
            await broker.start()
            return broker

        broker = asyncio.run_coroutine_threadsafe(start_broker(), loop).result(15)
        process = None
        clients = []
        try:
            commands = queue.Queue()
            observed = queue.Queue()
            reader_queue = queue.Queue()
            delivered = []

            def client(identifier, destination):
                connected = threading.Event()
                subscribed = threading.Event()
                c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=identifier)
                c.on_connect = lambda *args: connected.set()
                c.on_subscribe = lambda *args: subscribed.set()
                c.on_message = lambda _c, _u, message: destination.put(message)
                c.connect("127.0.0.1", port)
                c.loop_start()
                clients.append(c)
                self.assertTrue(connected.wait(10))
                return c, subscribed

            observer, subscribed = client("observer", observed)
            observer.subscribe("#", qos=1)
            self.assertTrue(subscribed.wait(10))
            command_topic = "test_gateway/menu/MU/101/set"
            observer.publish(command_topic, "49", qos=1, retain=True).wait_for_publish(10)
            gateway, subscribed = client("gateway", commands)
            gateway.subscribe("test_gateway/menu/+/+/set", qos=1)
            self.assertTrue(subscribed.wait(10))

            process = subprocess.Popen(
                [str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1
            )

            def read_output():
                for line in process.stdout:
                    reader_queue.put(json.loads(line))
                reader_queue.put({"eof": True})

            reader = threading.Thread(target=read_output, daemon=True)
            reader.start()

            def collect():
                publications = []
                while True:
                    value = reader_queue.get(timeout=15)
                    self.assertNotIn("eof", value)
                    if value.get("done"):
                        return publications
                    publications.append(value)
                    gateway.publish(
                        value["topic"], value["payload"], qos=1, retain=value["retain"]
                    ).wait_for_publish(10)

            def deliver(expected=None):
                # QoS1 may deliver retained startup messages more than once.
                for _ in range(32):
                    message = commands.get(timeout=10)
                    payload = message.payload.decode()
                    delivered.append((message.topic, payload, message.retain))
                    process.stdin.write(f"{message.topic}\t{payload}\t{int(message.retain)}\n")
                    process.stdin.flush()
                    publications = collect()
                    if expected is None or payload == expected:
                        return publications
                    self.assertTrue(message.retain, delivered)
                    self.assertEqual(publications, [])
                self.fail(f"Too many retained replays: {delivered}")

            startup = collect()
            self.assertEqual(deliver(), [])  # Retained startup replay is ignored.
            discovery = json.loads(
                next(p["payload"] for p in startup if p["topic"].startswith("homeassistant/"))
            )
            self.assertEqual((discovery["min"], discovery["max"]), (10, 50))
            self.assertFalse(discovery["retain"])

            observer.publish(command_topic, "99", qos=1).wait_for_publish(10)
            invalid = deliver("99")
            self.assertTrue(invalid, delivered)
            self.assertEqual(json.loads(invalid[0]["payload"])["code"], "out_of_range")
            observer.publish("test_gateway/menu/MU/102/set", "1", qos=1).wait_for_publish(10)
            self.assertEqual(json.loads(deliver("1")[0]["payload"])["code"], "not_writable")
            observer.publish(command_topic, "31", qos=1).wait_for_publish(10)
            written = deliver("31")
            codes = [
                json.loads(p["payload"])["code"] for p in written if p["topic"].endswith("/result")
            ]
            self.assertEqual(codes, ["write_queued", "write_sent", "write_confirmed"])

            # A newly connected consumer must receive the retained final state.
            retained = queue.Queue()
            late, subscribed = client("late-observer", retained)
            late.subscribe("#", qos=1)
            self.assertTrue(subscribed.wait(10))
            for _ in range(32):
                final = retained.get(timeout=10)
                if final.topic == "test_gateway/menu/MU/101/state":
                    break
            self.assertEqual(final.topic, "test_gateway/menu/MU/101/state")
            self.assertEqual(final.payload, b"31")
            self.assertTrue(final.retain)
            process.stdin.close()
            self.assertEqual(process.wait(timeout=10), 0)
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            if process is not None:
                process.stdin.close()
                process.stdout.close()
            for c in clients:
                c.disconnect()
                c.loop_stop()
            asyncio.run_coroutine_threadsafe(broker.shutdown(), loop).result(10)
            loop.call_soon_threadsafe(loop.stop)
            thread.join(timeout=10)


if __name__ == "__main__":
    unittest.main()
