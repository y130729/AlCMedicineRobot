"""Encoding and repository hygiene checks.

The firmware mixes GBK and UTF-8 on purpose. A silent re-encode is a real
defect (it already happened once in this repository), so every source file is
decoded explicitly and rejected if it contains U+FFFD.
"""

from __future__ import annotations

import unittest

from common import FW, QT, REPO, read_text

GBK_FILES = [
    FW / "USER/car_app.c",
    FW / "RTOS/rtos_app.c",
    FW / "HARDWARE/ATK-ESP8266/common.c",
    FW / "SYSTEM/sys/sys.c",
]

UTF8_FILES = [
    FW / "USER/main.c",
    FW / "USER/car_app.h",
    FW / "USER/wifi_cmd.c",
    FW / "HARDWARE/ADC/adc.c",
    FW / "HARDWARE/ADC/adc.h",
    FW / "HARDWARE/TIMER/timer.c",
    FW / "HARDWARE/TIMER/timer.h",
    FW / "HARDWARE/USART3/usart3.c",
    FW / "HARDWARE/USART3/usart3.h",
    FW / "HARDWARE/WIFI_TRANSFER/wifi_transfer.c",
    FW / "HARDWARE/WIFI_TRANSFER/wifi_transfer.h",
    FW / "HARDWARE/ATK-ESP8266/wifista.c",
    FW / "HARDWARE/ATK-ESP8266/wifista.h",
    FW / "README.md",
    QT / "mainwindow.cpp",
    QT / "mainwindow.h",
]


class EncodingTests(unittest.TestCase):
    def test_known_gbk_files_decode(self):
        for path in GBK_FILES:
            with self.subTest(path=path.name):
                text = read_text(path, "gbk")
                self.assertNotIn("\ufffd", text, f"{path} contains U+FFFD")

    def test_known_utf8_files_decode(self):
        for path in UTF8_FILES:
            with self.subTest(path=path.name):
                text = read_text(path, "utf-8-sig")
                self.assertNotIn("\ufffd", text, f"{path} contains U+FFFD")

    def test_every_firmware_source_decodes(self):
        files = sorted(FW.rglob("*.c")) + sorted(FW.rglob("*.h"))
        self.assertGreater(len(files), 40)
        for path in files:
            raw = path.read_bytes()
            decoded = None
            for encoding in ("utf-8-sig", "gbk"):
                try:
                    decoded = raw.decode(encoding)
                    break
                except UnicodeDecodeError:
                    continue
            with self.subTest(path=path.relative_to(FW)):
                self.assertIsNotNone(decoded, "cannot decode source file")
                self.assertNotIn("\ufffd", decoded or "")

    def test_readme_has_utf8_bom(self):
        raw = (FW / "README.md").read_bytes()
        self.assertTrue(raw.startswith(b"\xef\xbb\xbf"))

    def test_no_merge_conflict_markers(self):
        roots = [FW, QT, REPO / "tests"]
        suffixes = {".c", ".h", ".cpp", ".hpp", ".py", ".md", ".yml", ".yaml", ".pro"}
        for root in roots:
            for path in root.rglob("*"):
                if path.is_file() and path.suffix.lower() in suffixes:
                    text = path.read_bytes().decode("utf-8-sig", errors="replace")
                    with self.subTest(path=path.relative_to(REPO)):
                        self.assertNotIn("<" * 7, text)
                        self.assertNotIn(">" * 7, text)


if __name__ == "__main__":
    unittest.main()
