"""Source-level invariant tests for the firmware.

These tests do not build or run the firmware. They read the real sources and
assert that safety-critical properties which are easy to regress (ADC mapping,
non-blocking control loop, watchdog semantics, DMA RX, protocol constants and
Qt/firmware wire-format agreement) still hold.
"""

from __future__ import annotations

import re
import unittest

from common import FW, QT, c_function, define_int, read_gbk, read_utf8, strip_c_comments


def code_only(text: str) -> str:
    return strip_c_comments(text)


class AdcAndTimingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.adc = read_utf8(FW / "HARDWARE/ADC/adc.c")
        cls.timer = read_utf8(FW / "HARDWARE/TIMER/timer.c")
        cls.car = read_gbk(FW / "USER/car_app.c")
        cls.readme = read_utf8(FW / "README.md")

    def test_adc_scan_order_matches_physical_mapping(self):
        channels = re.findall(
            r"ADC_RegularChannelConfig\(ADC3,\s*ADC_Channel_(\d+),\s*(\d+)",
            code_only(self.adc),
        )
        self.assertEqual(channels, [("5", "1"), ("4", "2"), ("15", "3")])
        for token in ("PF7(ADC3_CH5)", "PF6(ADC3_CH4)", "PF5(ADC3_CH15)"):
            self.assertIn(token, self.readme)

    def test_adc_is_timer_triggered_and_dma_circular(self):
        adc = code_only(self.adc)
        self.assertIn("ADC_ExternalTrigConv_T2_TRGO", adc)
        self.assertIn("TIM_SelectOutputTrigger(TIM2, TIM_TRGOSource_Update)", adc)
        self.assertIn("DMA_Mode_Circular", adc)
        self.assertIn("DMA_IT_HT | DMA_IT_TC", adc)
        self.assertIn("Adc_ReadLine", adc)

    def test_control_loop_has_no_blocking_delays(self):
        blocking = (
            "ADC_ReadLineSensors",
            "CAR_StepSteer",
            "CAR_StepSearch",
            "CAR_StepRange",
            "CAR_StepAdc",
            "CAR_StepCamera",
            "CAR_StepTask",
            "CAR_StepMotion",
        )
        for name in blocking:
            body = code_only(c_function(self.car, name))
            with self.subTest(function=name):
                self.assertNotIn("delay_ms(", body)
                self.assertNotIn("delay_us(", body)
                self.assertNotIn("MOTOR_LineTurn", body)
        self.assertNotIn("Get_Adc_Average", self.car)

    def test_control_period_and_station_trigger_are_consistent(self):
        period = define_int(self.car, "CAR_CONTROL_PERIOD_MS")
        samples = define_int(self.car, "TASK_TRIGGER_SAMPLES")
        self.assertEqual(period, 5)
        self.assertGreaterEqual(samples * period, 15)
        self.assertLessEqual(samples * period, 25)

        rtos_loop = code_only(c_function(self.car, "vTaskCarControl"))
        self.assertIn("vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(CAR_CONTROL_PERIOD_MS))", rtos_loop)
        for call in (
            "WDG_TaskTick(WDG_TASK_CONTROL)",
            "WDG_CheckAndFeed()",
            "USART3_TextPump()",
            "CAR_StepRange(&ctx)",
            "CAR_StepAdc(&ctx)",
            "CAR_StepTask(&ctx)",
            "CAR_StepMotion(&ctx)",
        ):
            self.assertIn(call, rtos_loop)

    def test_ultrasonic_capture_is_non_blocking(self):
        timer = code_only(self.timer)
        self.assertIn("EXTI_Trigger_Rising_Falling", timer)
        self.assertIn("HC_SR04_ECHO_TIMEOUT_US", timer)
        self.assertIn("DWT_Micros()", timer)
        poll = code_only(c_function(self.timer, "HC_SR04_Poll"))
        self.assertNotIn("delay_ms(", poll)
        start = code_only(c_function(self.timer, "HC_SR04_Start"))
        self.assertNotIn("while", start)


class WatchdogAndBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sys = read_gbk(FW / "SYSTEM/sys/sys.c")
        cls.main = read_utf8(FW / "USER/main.c")
        cls.wifista = read_utf8(FW / "HARDWARE/ATK-ESP8266/wifista.c")
        cls.common = read_gbk(FW / "HARDWARE/ATK-ESP8266/common.c")

    def test_watchdog_uses_independent_time_base(self):
        sys_code = code_only(self.sys)
        self.assertIn("xTaskGetTickCount()", sys_code)
        self.assertIn("DWT->CYCCNT", sys_code)
        self.assertIn("wdg_progress", sys_code)
        self.assertIn("wdg_seen", sys_code)

    def test_at_wait_does_not_unconditionally_feed(self):
        progress = code_only(c_function(self.sys, "WDG_TaskProgress"))
        self.assertEqual(progress.count("WDG_Feed()"), 2)
        bare = progress.index("#if !RTOS_ENABLE")
        alt = progress.index("#else")
        guard = progress.index("xTaskGetSchedulerState()")
        feed_rtos = progress.index("WDG_Feed()", alt)
        self.assertLess(bare, alt)
        self.assertLess(alt, guard)
        self.assertLess(guard, feed_rtos)
        self.assertIn("taskSCHEDULER_RUNNING", progress[guard:feed_rtos + 40])

    def test_watchdog_requires_progress_from_every_task(self):
        check = code_only(c_function(self.sys, "WDG_CheckAndFeed"))
        self.assertIn("wdg_progress[i] == wdg_seen[i]", check)
        self.assertIn("WDG_TASK_ALIVE_MS", check)
        self.assertIn("WDG_Feed()", check)
        self.assertIn("for (i = 0; i < WDG_TASK_COUNT; i++)", check)

    def test_boot_order_starts_watchdog_before_network(self):
        code = code_only(self.main)
        self.assertLess(code.index("DWT_Init()"), code.index("WDG_Init()"))
        self.assertLess(code.index("WDG_Init()"), code.index("atk_8266_wifista_config()"))
        self.assertIn("BOOT_TIMEOUT", self.main)
        self.assertIn("WIFI_LinkStartRecovery()", self.main)

    def test_wifi_boot_is_bounded(self):
        timeout = define_int(self.wifista, "WIFI_BOOT_TIMEOUT_MS")
        self.assertGreater(timeout, 0)
        self.assertLessEqual(timeout, 60_000)
        body = code_only(c_function(self.wifista, "atk_8266_wifista_config"))
        self.assertIn("while (step < 8)", body)
        self.assertIn("WIFI_BOOT_TIMEOUT_MS", body)
        self.assertIn("BOOT_TIMEOUT,step=", self.wifista)
        self.assertNotIn("while (1)", body)
        self.assertNotIn("while(1)", body)

    def test_at_waits_only_report_progress(self):
        code = code_only(self.common)
        self.assertIn("WDG_TaskProgress(WDG_TASK_WIFI)", code)
        self.assertNotIn("WDG_Feed()", code)


class SerialAndVideoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.usart = read_utf8(FW / "HARDWARE/USART3/usart3.c")
        cls.usart_h = read_utf8(FW / "HARDWARE/USART3/usart3.h")
        cls.transfer = read_utf8(FW / "HARDWARE/WIFI_TRANSFER/wifi_transfer.c")
        cls.qt = read_utf8(QT / "mainwindow.cpp")

    def test_usart3_rx_uses_circular_dma_and_idle_irq(self):
        code = code_only(self.usart)
        self.assertIn("DMA1_Stream1", code)
        self.assertIn("DMA_Mode_Circular", code)
        self.assertIn("USART_ITConfig(USART3, USART_IT_IDLE, ENABLE)", code)
        self.assertIn("DMA_IT_HT | DMA_IT_TC", code)
        self.assertIn("USART3_RxReset", code)
        self.assertNotIn("USART_IT_RXNE", code)
        self.assertGreaterEqual(define_int(self.usart_h, "USART3_RX_DMA_SIZE"), 512)

    def test_text_tx_is_bounded_and_never_uses_vsprintf(self):
        code = code_only(self.usart)
        fmt = define_int(self.usart, "USART3_FMT_MAX")
        tx_max = define_int(self.usart_h, "USART3_MAX_SEND_LEN")
        self.assertLessEqual(fmt, tx_max)
        body = code_only(c_function(self.usart, "u3_printf"))
        self.assertIn("vsnprintf(tmp, sizeof(tmp), fmt, ap)", body)
        self.assertNotIn("vsprintf(", body)
        drain = code_only(c_function(self.usart, "USART3_TextDrain"))
        self.assertIn("timeout_ms", drain)

    def test_video_chunk_layout(self):
        code = code_only(self.transfer)
        self.assertEqual(define_int(self.transfer, "VIDEO_CHUNK_SIZE"), 256)
        self.assertEqual(define_int(self.transfer, "VIDEO_PKT_HEADER"), 10)
        self.assertGreaterEqual(define_int(self.transfer, "VIDEO_MIN_BAUD"), 460_800)
        for pattern in (
            r"video_pkt\[0\]\s*=\s*0xAA",
            r"video_pkt\[1\]\s*=\s*0x55",
            r"video_pkt\[2\]\s*=\s*0xA5",
            r"video_pkt\[3\]\s*=\s*0x5A",
            r"video_pkt\[4\]\s*=",
            r"video_pkt\[6\]\s*=",
            r"video_pkt\[8\]\s*=",
        ):
            self.assertRegex(code, pattern)
        body = code_only(c_function(self.transfer, "jpeg_process_and_send"))
        self.assertIn("USART3_TextPending()", body)
        self.assertLess(body.index("USART3_TextPending()"), body.index("USART3_DMA_TryStart"))
        self.assertIn("USART3_DMA_TryStart(video_pkt", body)

    def test_qt_chunk_parser_matches_firmware_header(self):
        qt = self.qt
        self.assertIn('QByteArray::fromHex("AA55A55A")', qt)
        self.assertRegex(qt, r"m_recvBuffer\.size\(\)\s*<\s*10")
        self.assertRegex(qt, r"\.at\(4\)")
        self.assertRegex(qt, r"\.at\(6\)")
        self.assertRegex(qt, r"\.at\(8\)")
        self.assertIn("mid(10, chunkLen)", qt)
        self.assertIn(".at(10 + chunkLen)", qt)
        self.assertIn("crc16_ccitt((const uint8_t*)payload.constData()", qt)
        self.assertIn("m_jpegAssembly", qt)
        self.assertIn("m_jpegExpected", qt)


class ProtocolContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.car = read_gbk(FW / "USER/car_app.c")
        cls.wifi_cmd = read_utf8(FW / "USER/wifi_cmd.c")
        cls.qt = read_utf8(QT / "mainwindow.cpp")
        cls.readme = read_utf8(FW / "README.md")

    def test_ack_format_and_reason_codes(self):
        self.assertIn("ACK,%s,%d,%s,seq=%u", self.car)
        self.assertIn("DUP,seq=%u", self.car)
        for reason in ("OK", "BUSY", "STATE", "BADREQ", "UNKNOWN"):
            self.assertIn('"' + reason + '"', self.car)
        self.assertIn("TASK_IDEM_TTL_MS", self.car)

    def test_idempotency_window_covers_host_retries(self):
        ttl = define_int(self.car, "TASK_IDEM_TTL_MS")
        retry_ms = 500
        retries = 3
        self.assertGreater(ttl, retry_ms * (retries + 1))
        self.assertIn("m_ackTimer->start(500)", self.qt)
        self.assertGreaterEqual(self.qt.count("sendCarCommand(m_pendingCmd)"), 2)

    def test_qt_sends_seq_and_reads_reason(self):
        self.assertRegex(self.qt, r"m_pendingCmd\s*=\s*cmd\s*\+\s*QString\(\",seq=%1\"\)")
        self.assertIn("sendCarCommand(m_pendingCmd)", self.qt)
        self.assertRegex(self.qt, r"handleAck\(p\.at\(1\),\s*p\.at\(2\)\s*==\s*\"1\",")
        self.assertIn("p.at(3)", self.qt)

    def test_every_qt_command_is_recognised_by_firmware(self):
        qt_commands = set(re.findall(r'QString\("([A-Z_]+),', self.qt))
        self.assertTrue(qt_commands)
        self.assertIn("TASK_START", qt_commands)
        self.assertIn("STOP", qt_commands)
        for command in sorted(qt_commands):
            with self.subTest(command=command):
                self.assertRegex(self.wifi_cmd, r'"' + command + r',"')

    def test_documented_command_set_matches_firmware(self):
        commands = ("TASK_START", "STOP", "TASK_CONFIRM", "TASK_PAUSE", "TASK_RESUME", "TASK_CANCEL")
        for command in commands:
            with self.subTest(command=command):
                self.assertIn(command, self.wifi_cmd)
                self.assertIn(command, self.readme)


if __name__ == "__main__":
    unittest.main()
