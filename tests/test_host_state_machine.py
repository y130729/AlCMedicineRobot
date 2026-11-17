"""Build and run the host-side state-machine test harness.

The harness compiles the real USER/car_app.c and USER/wifi_cmd.c against small
hardware stubs, then drives TASK_START/STOP/CONFIRM/PAUSE/RESUME/CANCEL through
the same wifi_dispatch_commands() entry point used by the firmware.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from common import FW, REPO
from toolchain import Builder, find_toolchain, run_exe

INCLUDE_DIRS = [
    REPO / "tests/host/stubs",
    FW / "USER",
    FW / "RTOS",
    FW / "SYSTEM/sys",
    FW / "HARDWARE/ADC",
    FW / "HARDWARE/CAMERA",
    FW / "HARDWARE/DCMI",
    FW / "HARDWARE/LED",
    FW / "HARDWARE/EXTI",
    FW / "HARDWARE/TIMER",
    FW / "HARDWARE/MOTOR",
    FW / "HARDWARE/UI",
    FW / "HARDWARE/USART3",
    FW / "HARDWARE/WIFI_TRANSFER",
    FW / "HARDWARE/ATK-ESP8266",
]


class HostStateMachineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tc = find_toolchain()

    def test_state_machine_scenarios(self):
        if self.tc is None:
            self.skipTest("no host C compiler found")
        with tempfile.TemporaryDirectory(prefix="aic-state-") as tmp:
            work = Path(tmp)
            builder = Builder(self.tc, work)
            test_obj = builder.compile(
                REPO / "tests/host/test_car_state.c",
                work / "test_car_state.obj",
                includes=INCLUDE_DIRS,
                charset=".936",
            )
            wifi_obj = builder.compile(
                FW / "USER/wifi_cmd.c",
                work / "wifi_cmd.obj",
                includes=INCLUDE_DIRS,
                charset=".65001",
            )
            stub_obj = builder.compile(
                REPO / "tests/host/stubs.c",
                work / "stubs.obj",
                includes=INCLUDE_DIRS,
                charset=".65001",
            )
            exe = builder.link([test_obj, wifi_obj, stub_obj], work / "test_car_state.exe")
            output = run_exe(exe).stdout

        self.assertIn("HOST_TESTS_OK", output, output)
        self.assertNotIn("FAIL ", output, output)


if __name__ == "__main__":
    unittest.main()
