# 主机端自动化测试

本目录包含在主机上运行的自动化测试，用于验证固件源码约束、通信协议和任务状态机。
测试通过主机 C 编译器和硬件桩执行，被测源码保持原样。

正式测试计划、用例编号、覆盖矩阵与执行记录见
[`../TESTING.md`](../TESTING.md)。

## 测试分层

| 文件 | 类型 | 验证内容 |
| --- | --- | --- |
| `test_encoding.py` | 静态检查 | GBK/UTF-8 混合编码、README BOM、替换字符、冲突标记 |
| `test_firmware_invariants.py` | 源码契约 | ADC 映射、非阻塞控制环、5 ms 周期、进站采样、看门狗时基、USART3 DMA、视频分片、协议命令集 |
| `test_protocol_packets.py` | 黄金向量 | 视频分片头、CRC16-CCITT、offset 重组、文本穿插、坏片/缺片恢复、最大帧 |
| `test_crc_source.py` | 真实源码编译 | 从 `wifi_transfer.c` 提取 `crc16_ccitt()`，与 Python 参考实现比对 |
| `test_host_state_machine.py` | 主机状态机 | 编译真实 `car_app.c` + `wifi_cmd.c`，驱动任务命令、幂等、进站、兜底、丢线、暂停/恢复/取消和心跳 |

当前共 35 个用例；主机状态机 harness 含 105 条断言。

## 运行

仓库根目录执行：

```bash
python -m unittest discover -s tests -v
```

只运行某一层：

```bash
python -m unittest discover -s tests -p "test_protocol_packets.py" -v
python -m unittest discover -s tests -p "test_firmware_invariants.py" -v
```

## 依赖

- Python 3.9+，仅使用标准库。
- 主机 C 编译器：
  - Linux/macOS：`cc` 或 `gcc`
  - Windows：Visual Studio Build Tools（`cl`）
- 没有 C 编译器时，`test_crc_source.py` 与 `test_host_state_machine.py` 自动跳过，
  其余静态测试仍可运行。

Windows 下若 `cl` 不在 PATH，`tests/toolchain.py` 通过 `vswhere` 定位 Visual Studio
并调用 `vcvars64.bat`。

## CI

`.github/workflows/ci.yml` 在两个环境运行同一套用例：

- `ubuntu-latest` + GCC
- `windows-latest` + MSVC

CI 不构建 Keil 工程；目标板编译在本地 Keil MDK 中完成。

## 设计说明

### 主机状态机

`tests/host/test_car_state.c` 在包含 `car_app.c` 前临时将 `static` 定义为空，
从而直接调用 `CAR_StepTask()`、`CAR_OnTaskCommand()` 等函数并检查真实全局状态。
`car_app.c` 为 GBK 编码，测试在临时目录将其转码为 UTF-8 后编译，避免混合编码
翻译单元；仓库源文件本身不改写。

硬件边界由桩提供：

- `MOTOR_*`：记录调用次数与速度，供断言检查。
- `EXTIX_GetMillis()`：由测试推进虚拟时钟，覆盖超时分支。
- `u3_printf()`：收集串口输出为文本行，用于断言 `ACK`、`STATUS`、`TASK_ARRIVE`。
- `UI_*`、`CAMERA_*`、`WDG_*`、`USART3_*`：返回可控状态。

### 源码契约

ADC 扫描顺序、看门狗时基、DMA 配置、非阻塞控制环等属性无法仅靠主机单元测试观察。
测试读取真实源码，对关键配置和调用顺序做断言，以在提交阶段发现误改。

## 局限

- 不验证电机、PWM 死区、ADC 噪声、超声回波、WiFi 丢包、摄像头曝光等硬件特性。
- 状态机使用虚拟时钟，不能替代 DWT/GPIO 翻转测得的调度抖动。
- 源码断言对函数名和宏名敏感，重构后需同步更新。
- 硬件桩只模拟测试所需的接口语义，新增外设交互时需补充桩。

台架仍需覆盖：三路循迹阈值、进站采样、STOP 兜底、断网重连、921600 下的视频完整性。

## 扩展测试

- 纯协议或算法：加入 `test_protocol_packets.py` 或新建 Python 用例。
- 真实 C 函数：优先用 `common.c_function()` 提取后在主机编译，避免复制实现。
- 状态机分支：在 `tests/host/test_car_state.c` 中通过 `dispatch()` 与 `step()` 驱动。
- 新增硬件接口：在 `tests/host/stubs.c` 补桩，并在 `host_shim.h` 声明。
