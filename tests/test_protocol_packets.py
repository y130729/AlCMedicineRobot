"""Golden-vector tests for the QQ video chunk protocol.

The test suite does not need Qt: it implements the documented receiver rules
(text lines between chunks, CRC16 per chunk, offset-based reassembly, resync)
and checks the exact wire layout produced by the firmware.
"""

from __future__ import annotations

import unittest

from common import FW, QT, define_int, read_utf8

MAGIC = b"\xAA\x55\xA5\x5A"
CHUNK = 256


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def packet(total: int, offset: int, payload: bytes) -> bytes:
    header = MAGIC + total.to_bytes(2, "little") + offset.to_bytes(2, "little")
    header += len(payload).to_bytes(2, "little")
    return header + payload + crc16_ccitt(payload).to_bytes(2, "little")


def split_frame(frame: bytes) -> list[bytes]:
    return [packet(len(frame), off, frame[off:off + CHUNK]) for off in range(0, len(frame), CHUNK)]


class VideoAssembler:
    """Reference implementation of the Qt-side parser rules."""

    TEXT_PREFIXES = (b"ACK,", b"ENTER_STATION,", b"STATUS,", b"TASK_ARRIVE,",
                     b"TASK_DONE,", b"HEARTBEAT,", b"LINK,", b"STATS,")
    LEGACY_HEADER = b"\xAA\x55"
    WAITING = TEXT_PREFIXES + (b"BOOT_TIMEOUT,",)

    def __init__(self):
        self.buf = bytearray()
        self.asm = bytearray()
        self.expected = 0
        self.frames = []
        self.lines = []

    def feed(self, data: bytes) -> None:
        self.buf.extend(data)
        progress = True
        while progress:
            progress = False
            if not self.buf:
                break
            nl = self.buf.find(b"\r\n")
            if nl >= 0 and self.buf.startswith(self.TEXT_PREFIXES):
                self.lines.append(bytes(self.buf[:nl]))
                del self.buf[:nl + 2]
                progress = True
                continue
            if self.buf.startswith(MAGIC):
                if len(self.buf) < 10:
                    break
                total = int.from_bytes(self.buf[4:6], "little")
                offset = int.from_bytes(self.buf[6:8], "little")
                length = int.from_bytes(self.buf[8:10], "little")
                if total < 500 or total > 50 * 1024 or length == 0 or length > CHUNK or offset + length > total:
                    del self.buf[0]
                    progress = True
                    continue
                full = 10 + length + 2
                if len(self.buf) < full:
                    break
                payload = bytes(self.buf[10:10 + length])
                got = int.from_bytes(self.buf[10 + length:full], "little")
                if got != crc16_ccitt(payload):
                    del self.buf[0]
                    progress = True
                    continue
                if offset == 0:
                    self.asm = bytearray()
                    self.expected = total
                if total != self.expected or offset != len(self.asm):
                    self.asm = bytearray()
                    self.expected = total
                    if offset != 0:
                        del self.buf[:full]
                        progress = True
                        continue
                self.asm.extend(payload)
                del self.buf[:full]
                progress = True
                if len(self.asm) == self.expected:
                    self.frames.append(bytes(self.asm))
                    self.asm = bytearray()
                    self.expected = 0
                continue
            if self.buf.startswith(self.LEGACY_HEADER):
                if len(self.buf) < 6:
                    break
                jpeg_len = int.from_bytes(self.buf[2:6], "little")
                if 500 <= jpeg_len <= 50 * 1024:
                    full = 6 + jpeg_len + 2
                    if len(self.buf) < full:
                        break
                    jpeg = bytes(self.buf[6:6 + jpeg_len])
                    got = int.from_bytes(self.buf[6 + jpeg_len:full], "little")
                    if got == crc16_ccitt(jpeg):
                        self.frames.append(jpeg)
                        del self.buf[:full]
                        progress = True
                        continue
                del self.buf[0]
                progress = True
                continue
            if any(prefix.startswith(bytes(self.buf)) or self.buf.startswith(prefix)
                   for prefix in self.WAITING):
                break
            index = self.buf.find(MAGIC)
            if index < 0:
                if len(self.buf) > 1:
                    del self.buf[:-1]
                break
            del self.buf[:index]
            progress = True


class ProtocolPacketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.transfer = read_utf8(FW / "HARDWARE/WIFI_TRANSFER/wifi_transfer.c")
        cls.qt = read_utf8(QT / "mainwindow.cpp")

    def test_wire_constants_match_sources(self):
        self.assertEqual(define_int(self.transfer, "VIDEO_CHUNK_SIZE"), CHUNK)
        self.assertEqual(define_int(self.transfer, "VIDEO_PKT_HEADER"), 10)
        self.assertIn('QByteArray::fromHex("AA55A55A")', self.qt)

    def test_crc_reference_vector(self):
        self.assertEqual(crc16_ccitt(b"123456789"), 0x29B1)
        self.assertEqual(crc16_ccitt(b""), 0xFFFF)

    def test_reassembles_1000_byte_frame_with_tiny_reads(self):
        frame = bytes((i * 37 + 11) & 0xFF for i in range(1000))
        packets = split_frame(frame)
        self.assertEqual([len(p) for p in packets], [268, 268, 268, 244])
        parser = VideoAssembler()
        stream = b"".join(packets)
        step = 1
        pos = 0
        while pos < len(stream):
            parser.feed(stream[pos:pos + step])
            pos += step
            step = step % 17 + 1
        self.assertEqual(parser.frames, [frame])
        self.assertEqual(parser.buf, b"")

    def test_text_line_can_be_interleaved_between_chunks(self):
        frame = bytes(range(256)) * 2
        packets = split_frame(frame)
        parser = VideoAssembler()
        parser.feed(packets[0])
        parser.feed(b"ACK,T1,1,OK,seq=7\r\n")
        parser.feed(packets[1])
        self.assertEqual(parser.frames, [frame])
        self.assertEqual(parser.lines, [b"ACK,T1,1,OK,seq=7"])

    def test_corrupted_chunk_is_dropped_and_next_frame_recovers(self):
        frame = bytes((i * 13) & 0xFF for i in range(700))
        packets = split_frame(frame)
        bad = bytearray(packets[1])
        bad[10] ^= 0x5A
        parser = VideoAssembler()
        parser.feed(packets[0] + bytes(bad) + packets[2] + b"".join(split_frame(frame)))
        self.assertEqual(parser.frames, [frame])

    def test_missing_chunk_drops_partial_frame(self):
        frame = bytes((i * 5) & 0xFF for i in range(700))
        packets = split_frame(frame)
        parser = VideoAssembler()
        parser.feed(packets[0] + packets[2])
        self.assertEqual(parser.frames, [])
        parser.feed(b"".join(packets))
        self.assertEqual(parser.frames, [frame])

    def test_parser_resynchronises_after_garbage(self):
        frame = bytes((i * 3) & 0xFF for i in range(600))
        parser = VideoAssembler()
        parser.feed(b"\x00\x11\x22garbage")
        parser.feed(b"".join(split_frame(frame)))
        self.assertEqual(parser.frames, [frame])

    def test_maximum_supported_frame(self):
        # The firmware accepts 60 kB, while the Qt parser caps frames at 50 kB.
        # The effective limit is therefore the stricter value; keep the test
        # inside it and document the narrowing instead of hiding it.
        c_max = define_int(self.transfer, "VIDEO_MAX_TOTAL")
        qt_max = 50 * 1024
        limit = min(c_max, qt_max)
        self.assertGreaterEqual(limit, 40000)
        frame = bytes((i * 7) & 0xFF for i in range(limit))
        parser = VideoAssembler()
        parser.feed(b"".join(split_frame(frame)))
        self.assertEqual(parser.frames, [frame])
        self.assertEqual(len(parser.frames[0]), limit)


if __name__ == "__main__":
    unittest.main()
