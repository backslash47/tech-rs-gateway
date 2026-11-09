#!/usr/bin/env python3
"""Compile host tests against the same separate implementation files as firmware."""

import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/tech_rs_gateway"
TESTS = {
    "wire": ["wire.cpp"],
    "menu": [],
    "session": [],
    "api": ["menu_api.cpp"],
    "mqtt_projection": ["mqtt_projection.cpp"],
    "mqtt_settings": ["mqtt_settings.cpp"],
}
CORE = [
    "controller_service.cpp",
    "dynamic_menu.cpp",
    "menu_text.cpp",
    "operation_result.cpp",
    "internet_session.cpp",
    "wire.cpp",
]


def main():
    """Compile and run host suites against the production implementation files.

    Writes executables under build/ and uses CXX or the default c++ compiler.
    Compilation/test failures propagate as CalledProcessError; no device is used.
    """
    build = ROOT / "build"
    build.mkdir(exist_ok=True)

    for name, sources in TESTS.items():
        executable = build / f"test_{name}"
        command = [
            os.environ.get("CXX", "c++"),
            "-std=c++17",
            "-fno-exceptions",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            str(ROOT / f"tests/test_{name}.cpp"),
        ]
        command.extend(str(SOURCE / source) for source in sources)
        if name in ("menu", "session", "api", "mqtt_projection"):
            command.extend(str(SOURCE / source) for source in CORE)
            command.append("-pthread")
        command.extend(["-o", str(executable)])
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
