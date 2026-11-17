# 测试文档

| 项目 | 内容 |
| --- | --- |
| 文档版本 | 1.0 |
| 更新日期 | 2026-11-17 |
| 代码基线 | `439107e`（测试套件与固件代码） |
| 被测对象 | STM32F407 车端固件、Qt 上位机、TCP 通信协议 |
| 测试类型 | 主机自动化、源码静态检查、协议黄金向量 |
| 当前结果 | 35/35 用例通过；状态机 harness 105 条断言 |

## 1. 目的与范围

本文档定义项目的测试目标、策略、用例清单、覆盖矩阵和执行记录，用于验证：

- 固件源码的 GBK/UTF-8 混合编码一致性；
- ADC 通道映射、非阻塞控制环、5 ms 控制周期和进站采样；
- 看门狗时基、任务心跳、AT 等待和启动超时约束；
- USART3 接收 DMA、文本发送边界和视频分片格式；
- CRC16-CCITT、offset 重组、坏片/缺片/垃圾数据恢复；
- 任务状态机命令、幂等重发、进站确认、兜底停车和丢线超时；
- Qt 上位机与固件之间的命令集、ACK 格式和分片解析一致性。

### 1.1 不纳入范围

- 电机、驱动器、PWM 死区、电源与电池特性；
- ADC 噪声、循迹传感器光学特性、超声回波与安装误差；
- WiFi 射频性能、真实丢包率、断网重连时延；
- 摄像头曝光、JPEG 画质、二维码识别率；
- FreeRTOS 实际调度抖动、中断延迟和栈溢出；
- Keil 目标板编译、烧录、长时间老化和 HIL 联调。

上述项目需在台架和实车上验证，不能由主机测试替代。

## 2. 参考文档

- `README.md`：系统架构、构建、使用说明与测试入口；
- `AIC_MedRobot_stm32f407/README.md`：固件引脚、协议和 FreeRTOS 设计；
- `tests/README.md`：测试实现、硬件桩和扩展方法；
- `.github/workflows/ci.yml`：持续集成配置。

## 3. 测试策略

1. **真实实现优先**：CRC 从 `wifi_transfer.c` 提取；状态机直接编译真实
   `car_app.c` 与 `wifi_cmd.c`，不复制业务逻辑。
2. **硬件边界桩接**：电机、ADC、串口、UI、看门狗等由 `tests/host/stubs.c`
   提供可控实现，业务代码保持原样。
3. **虚拟时钟**：状态机测试通过 `EXTIX_GetMillis()` 桩推进时间，覆盖超时分支，
   不依赖实际延时。
4. **源码契约检查**：对 ADC 扫描顺序、DMA 配置、看门狗时基等难以在主机运行
   的属性做源码断言，防止重构误改。
5. **自动化执行**：测试仅依赖 Python 标准库和主机 C 编译器，可在本地及
   GitHub Actions 运行。

## 4. 测试环境

| 项目 | 要求 |
| --- | --- |
| Python | 3.9 及以上，仅标准库 |
| 主机编译器 | Linux/macOS：`cc` 或 `gcc`；Windows：MSVC `cl` |
| 操作系统 | Windows 10/11、Ubuntu 22.04+ |
| 目标板 | 测试执行不需要；目标板验证单独进行 |

### 4.1 执行命令

```bash
python -m unittest discover -s tests -v
```

单独执行某一层：

```bash
python -m unittest discover -s tests -p "test_encoding.py" -v
python -m unittest discover -s tests -p "test_firmware_invariants.py" -v
python -m unittest discover -s tests -p "test_protocol_packets.py" -v
python -m unittest discover -s tests -p "test_host_state_machine.py" -v
```

没有 C 编译器时，CRC 与状态机用例自动跳过，静态测试仍会执行。

## 5. 测试用例

### 5.1 编码与仓库卫生（TC-ENC）

| 用例编号 | 自动化用例 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-ENC-001 | `test_known_gbk_files_decode` | 已知 GBK 文件可解码 | 无解码异常、无 U+FFFD |
| TC-ENC-002 | `test_known_utf8_files_decode` | 已知 UTF-8 文件可解码 | 无解码异常、无 U+FFFD |
| TC-ENC-003 | `test_every_firmware_source_decodes` | 全部固件 `.c/.h` 可解码 | 每个文件至少匹配一种编码 |
| TC-ENC-004 | `test_readme_has_utf8_bom` | 固件 README 保持 UTF-8 BOM | 文件以 `EF BB BF` 开头 |
| TC-ENC-005 | `test_no_merge_conflict_markers` | 无遗留合并冲突标记 | 不出现 7 个连续 `<` 或 `>` |

### 5.2 ADC、控制环与时序（TC-ADC）

| 用例编号 | 自动化用例 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-ADC-001 | `test_adc_scan_order_matches_physical_mapping` | ADC3 扫描顺序与接线一致 | 依次为 PF7/CH5、PF6/CH4、PF5/CH15 |
| TC-ADC-002 | `test_adc_is_timer_triggered_and_dma_circular` | ADC 由 TIM2 触发、DMA 循环 | 存在 TRGO、Circular、HT/TC 配置 |
| TC-ADC-003 | `test_control_loop_has_no_blocking_delays` | 控制环无阻塞延时 | 关键 `CAR_Step*` 不含 `delay_ms/us` |
| TC-ADC-004 | `test_control_period_and_station_trigger_are_consistent` | 5 ms 周期与进站采样一致 | 周期 5 ms，4 次采样落在 15~25 ms |
| TC-ADC-005 | `test_ultrasonic_capture_is_non_blocking` | 超声测距非阻塞 | 使用双边沿捕获与 DWT 超时 |

### 5.3 看门狗与启动（TC-WDG）

| 用例编号 | 自动化用例 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-WDG-001 | `test_watchdog_uses_independent_time_base` | 看门狗使用独立时基 | 使用 `xTaskGetTickCount()` / DWT，不依赖 TIM6 |
| TC-WDG-002 | `test_at_wait_does_not_unconditionally_feed` | AT 等待不绕过健康检查 | 仅调度器运行且进度有效时喂狗 |
| TC-WDG-003 | `test_at_waits_only_report_progress` | AT 等待只更新自身进度 | 不直接无条件 `WDG_Feed()` |
| TC-WDG-004 | `test_boot_order_starts_watchdog_before_network` | 看门狗先于联网启动 | `WDG_Init()` 位于 WiFi 初始化之前 |
| TC-WDG-005 | `test_watchdog_requires_progress_from_every_task` | 所有任务均需心跳 | 缺少任一任务进度则判定异常 |
| TC-WDG-006 | `test_wifi_boot_is_bounded` | 联网启动有总超时 | 各等待循环有次数/时间上限 |

### 5.4 串口与视频链路（TC-UART）

| 用例编号 | 自动化用例 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-UART-001 | `test_usart3_rx_uses_circular_dma_and_idle_irq` | USART3 接收架构 | DMA1 Stream1 循环 + IDLE 中断 |
| TC-UART-002 | `test_text_tx_is_bounded_and_never_uses_vsprintf` | 文本发送边界 | 使用 `vsnprintf`，不出现 `vsprintf` |
| TC-UART-003 | `test_video_chunk_layout` | 视频分片头与发送顺序 | 256 B 分片、10 B 头、文本队列优先 |
| TC-UART-004 | `test_qt_chunk_parser_matches_firmware_header` | Qt 解析与固件头一致 | 魔数、偏移、长度、CRC 字段一致 |

### 5.5 协议契约（TC-PROTO）

| 用例编号 | 自动化用例 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-PROTO-001 | `test_ack_format_and_reason_codes` | ACK 格式与原因码 | 包含 OK/BUSY/STATE/BADREQ/UNKNOWN 与 DUP |
| TC-PROTO-002 | `test_idempotency_window_covers_host_retries` | 幂等窗口覆盖重发 | TTL 大于 3 次 500 ms 重试 |
| TC-PROTO-003 | `test_qt_sends_seq_and_reads_reason` | 上位机携带并解析 seq | 命令带 `seq`，ACK 读取原因字段 |
| TC-PROTO-004 | `test_every_qt_command_is_recognised_by_firmware` | Qt 命令固件均识别 | 每个 `TASK_*`/`STOP` 都有解析分支 |
| TC-PROTO-005 | `test_documented_command_set_matches_firmware` | 文档与实现一致 | 命令同时出现在固件与 README |

### 5.6 CRC 真实源码（TC-CRC）

| 用例编号 | 自动化用例 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-CRC-001 | `test_extracted_crc_matches_reference` | 固件 CRC 与参考实现一致 | 空串、`123456789`、0~255、1024 B 随机数据全部一致 |

### 5.7 视频分片黄金向量（TC-VID）

| 用例编号 | 自动化用例 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-VID-001 | `test_wire_constants_match_sources` | 分片常量与源码一致 | 256 B 分片、10 B 头、魔数 `AA55A55A` |
| TC-VID-002 | `test_crc_reference_vector` | CRC 标准向量 | `123456789` → `0x29B1`，空串 → `0xFFFF` |
| TC-VID-003 | `test_reassembles_1000_byte_frame_with_tiny_reads` | 小片读取重组 | 逐字节喂入仍能重组 1000 B |
| TC-VID-004 | `test_text_line_can_be_interleaved_between_chunks` | 文本与视频混流 | 文本行正确提取，视频帧不损坏 |
| TC-VID-005 | `test_corrupted_chunk_is_dropped_and_next_frame_recovers` | 坏 CRC 分片 | 丢弃当前帧，下一帧恢复 |
| TC-VID-006 | `test_missing_chunk_drops_partial_frame` | 缺片 | 丢弃不完整帧，不输出坏图 |
| TC-VID-007 | `test_parser_resynchronises_after_garbage` | 垃圾数据重同步 | 找到下一帧魔数并恢复 |
| TC-VID-008 | `test_maximum_supported_frame` | 最大帧边界 | 50 KB 上限内可正确重组 |

### 5.8 任务状态机（TC-SM）

主机 harness 编译真实 `car_app.c` 与 `wifi_cmd.c`，共 105 条断言：

| 用例编号 | 子场景 | 验证内容 | 预期结果 |
| --- | --- | --- | --- |
| TC-SM-001 | `test_remote_dispatch` | 遥控命令精确匹配 | `forWardX` 不误触发；多行命令按序执行 |
| TC-SM-002 | `test_task_start_and_idempotency` | 任务启动与幂等 | 重复 seq 返回 DUP；繁忙返回 BUSY；坏请求返回 BADREQ |
| TC-SM-003 | `test_station_flow_and_confirm` | 进站与装卸确认 | ENTER_STATION、STOP、TASK_ARRIVE、NAV、TASK_DONE 顺序正确 |
| TC-SM-004 | `test_stop_fallback` | STOP 兜底 | 2 s 未收到 STOP 时上报 FALLBACK 与 TIMEOUT |
| TC-SM-005 | `test_line_lost_timeout` | 丢线超时 | 8 s 全白后停车、上报 LINE_LOST，可取消 |
| TC-SM-006 | `test_pause_resume_cancel` | 暂停/恢复/取消 | 状态、电机停止与循迹使能符合预期 |
| TC-SM-007 | `test_heartbeat` | 周期心跳 | 输出 `HEARTBEAT,<run>,NAV` |
| TC-SM-008 | `test_pure_helpers` | 纯函数边界 | 百分位、字段解析、seq 解析、状态文本正确 |

## 6. 覆盖矩阵

| 质量属性 | 对应用例 |
| --- | --- |
| 源码编码一致 | TC-ENC-001 ~ 005 |
| 传感器与控制环实时性 | TC-ADC-001 ~ 005 |
| 系统自恢复 | TC-WDG-001 ~ 006 |
| 串口收发边界 | TC-UART-001 ~ 004 |
| 协议重试与诊断 | TC-PROTO-001 ~ 005、TC-SM-002 |
| 视频帧完整性 | TC-VID-001 ~ 008、TC-UART-003/004 |
| CRC 与上位机一致 | TC-CRC-001、TC-VID-002 |
| 配送任务闭环 | TC-SM-003 ~ 007 |
| Qt 与固件契约一致 | TC-PROTO-003 ~ 005、TC-UART-004 |

## 7. 执行记录

| 项目 | 结果 |
| --- | --- |
| 执行日期 | 2026-11-17 |
| 代码基线 | `439107e` |
| 执行环境 | Windows、Python 3.13、MSVC 2022 |
| 执行命令 | `python -m unittest discover -s tests -v` |
| 用例总数 | 35 |
| 通过 | 35 |
| 失败 | 0 |
| 跳过 | 0 |
| 耗时 | 约 11 s |
| 状态机断言数 | 105 |

分层结果：

| 测试套件 | 用例数 | 结果 |
| --- | ---: | --- |
| `test_encoding.py` | 5 | 全部通过 |
| `test_firmware_invariants.py` | 20 | 全部通过 |
| `test_crc_source.py` | 1 | 通过 |
| `test_protocol_packets.py` | 8 | 全部通过 |
| `test_host_state_machine.py` | 1（8 个子场景） | 通过 |

CI 作业在 Ubuntu + GCC、Windows + MSVC 运行同一套命令，结果以 GitHub Actions
执行页面为准。

## 8. 通过准则

- 静态与协议用例全部通过；
- 主机状态机 harness 输出 `HOST_TESTS_OK`，且无 `FAIL` 行；
- 允许的跳过仅限主机没有 C 编译器导致的 TC-CRC-001 与 TC-SM 系列；
- 测试预期应与协议和设计规格一致，不以修改断言替代缺陷修复。

## 9. 风险与局限

| 风险 | 影响 | 缓解措施 |
| --- | --- | --- |
| 非 HIL | 电气、机械、光学问题无法发现 | 保留台架与实车验证 |
| 虚拟时钟 | 无法覆盖真实调度抖动与中断延迟 | 用 DWT/GPIO 翻转单独测量 p50/p99/max |
| 源码契约对重构敏感 | 函数/宏改名会导致用例失败 | 重构时同步更新测试 |
| 硬件桩与真实行为漂移 | 状态机通过但硬件异常 | 新增外设接口时同步补桩与台架用例 |
| 未覆盖 WiFi/摄像头 | 丢包、曝光、二维码识别率未知 | 通过上位机联调和现场测试补充 |

## 10. 维护约定

- 修改通信协议时，同步更新 TC-PROTO、TC-VID 与 `tests/host/test_car_state.c`；
- 修改状态机分支时，同步补充子场景与断言；
- 新增硬件接口时，在 `tests/host/stubs.c` 补桩并在 `host_shim.h` 声明；
- 每次发布前更新本文档的代码基线和执行记录；
- 测试实现细节见 `tests/README.md`。

## 11. 测试文件索引

| 路径 | 作用 |
| --- | --- |
| `tests/common.py` | 编码读取、去注释、C 函数提取、宏解析 |
| `tests/toolchain.py` | MSVC/GCC 探测与主机编译链接 |
| `tests/test_encoding.py` | 编码与仓库卫生 |
| `tests/test_firmware_invariants.py` | 固件源码契约 |
| `tests/test_crc_source.py` | 真实 CRC 源码主机测试 |
| `tests/test_protocol_packets.py` | 视频分片黄金向量与参考接收器 |
| `tests/test_host_state_machine.py` | 状态机 harness 构建与执行 |
| `tests/host/test_car_state.c` | 状态机断言与子场景 |
| `tests/host/stubs.c` | 硬件边界桩与串口输出捕获 |
| `.github/workflows/ci.yml` | Ubuntu/Windows CI |
