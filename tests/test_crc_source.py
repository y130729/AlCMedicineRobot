"""Run the firmware CRC16 implementation on the host.

The function text is extracted from wifi_transfer.c at test time, so this is
not a re-implementation: a change to the firmware polynomial or initial value
is executed and compared against an independent Python reference.
"""

from __future__ import annotations

import random
import tempfile
import unittest
from pathlib import Path

from common import FW, c_function, read_utf8
from toolchain import Builder, find_toolchain, run_exe


def crc16_reference(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def c_array(name: str, data: bytes) -> str:
    if not data:
        return f"static const uint8_t {name}[1] = {{0}};\n"
    body = ",".join(str(b) for b in data)
    return f"static const uint8_t {name}[{len(data)}] = {{{body}}};\n"


class CrcSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tc = find_toolchain()

    def test_extracted_crc_matches_reference(self):
        if self.tc is None:
            self.skipTest("no host C compiler found")
        source = read_utf8(FW / "HARDWARE/WIFI_TRANSFER/wifi_transfer.c")
        function = c_function(source, "crc16_ccitt")

        rng = random.Random(20261117)
        vectors = {
            "v0": b"",
            "v1": b"123456789",
            "v2": bytes(range(256)),
            "v3": bytes(rng.randrange(256) for _ in range(1024)),
        }
        expected = [crc16_reference(data) for data in vectors.values()]

        declarations = "\n".join(c_array(name, data) for name, data in vectors.items())
        prints = "\n".join(
            f'    printf("%04X\\n", crc16_ccitt({name}, {len(data)}));'
            for name, data in vectors.items()
        )
        program = (
            "#include <stdint.h>\n#include <stdio.h>\n\n"
            + function
            + "\n\n"
            + declarations
            + "\nint main(void)\n{\n"
            + prints
            + "\n    return 0;\n}\n"
        )

        with tempfile.TemporaryDirectory(prefix="aic-crc-") as tmp:
            work = Path(tmp)
            src = work / "crc_test.c"
            src.write_text(program, encoding="utf-8")
            builder = Builder(self.tc, work)
            obj = builder.compile(src, work / "crc_test.obj", charset=".65001")
            exe = builder.link([obj], work / "crc_test.exe")
            output = run_exe(exe).stdout.split()
        self.assertEqual(output, [f"{value:04X}" for value in expected])


if __name__ == "__main__":
    unittest.main()
