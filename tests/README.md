# 无硬件测试套件

本目录提供一套**不依赖开发板、仿真器或电机/摄像头**的自动化测试，用于在
PC 和 CI 上验证最容易回归的固件契约、通信协议与任务状态机。

测试遵循两个原则：

1. **不修改被测源码**：固件与 Qt 源码保持原样，测试只读取、编译或包含它们。
2. **只桩硬件边界**：电机、ADC、串口、UI 等硬件调用由 `tests/host/stubs.c`
   替换；状态机、协议解析、CRC、分片等业务代码全部使用真实实现。

## 测试分层

| 文件 | 类型 | 验证内容 |
| --- | --- | --- |
| `test_encoding.py` | 静态检查 | GBK/UTF-8 混合编码可解码、README BOM、无替换字符、无冲突标记 |
| `test_firmware_invariants.py` | 源码契约 | ADC 通道映射、非阻塞控制环、5 ms 周期、进站 4 次采样、看门狗独立时基、USART3 DMA 接收、视频分片、协议命令集 |
| `test_protocol_packets.py` | 黄金向量 | QQ 视频分片头、CRC16-CCITT、offset 重组、文本行穿插、坏片/缺片恢复、最大帧 |
| `test_crc_source.py` | 真实源码编译 | 从 `wifi_transfer.c` 提取 `crc16_ccitt()`，在主机编译并与 Python 参考实现逐字节比对 |
| `test_host_state_machine.py` | 主机状态机 | 用桩编译真实 `car_app.c` + `wifi_cmd.c`，驱动 `TASK_START/STOP/CONFIRM/PAUSE/RESUME/CANCEL`、幂等重发、进站、兜底、丢线超时、心跳 |

当前共 35 个用例；主机状态机 harness 内含 105 条断言。

## 运行

仓库根目录执行：

```bash
python -m unittest discover -s tests -v
```

只跑某一层：

```bash
python -m unittest discover -s tests -p "test_protocol_packets.py" -v
python -m unittest discover -s tests -p "test_firmware_invariants.py" -v
```

### 依赖

- Python 3.9+，仅使用标准库（不需要 pytest）。
- 主机 C 编译器（可选）：
  - Linux/macOS：`cc` 或 `gcc`
  - Windows：Visual Studio Build Tools（`cl`）或已安装的 MSVC
- 没有编译器时，`test_crc_source.py` 与 `test_host_state_machine.py` 会自动
  `skip`，其余静态测试仍可运行。

Windows 下若 `cl` 不在 PATH，`tests/toolchain.py` 会通过 `vswhere` 自动定位
Visual Studio 并调用 `vcvars64.bat`。

## CI

`.github/workflows/ci.yml` 在每次 push / pull request 时运行：

- `ubuntu-latest` + GCC
- `windows-latest` + MSVC

CI 只编译主机测试，不构建 Keil 工程；固件的目标板编译仍需在本地用
`D:\MDK5\UV4\UV4.exe` 完成。

## 设计说明

### 主机状态机如何复用真实源码

`tests/host/test_car_state.c` 在包含 `car_app.c` 前临时定义 `static` 为空，
从而直接调用文件内的 `CAR_StepTask()`、`CAR_OnTaskCommand()` 等函数，并检查
真实全局状态 `g_task`。源文件本身没有被改写。

硬件边界由桩提供，例如：

- `MOTOR_*`：记录调用次数与速度，供断言检查
- `EXTIX_GetMillis()`：由测试控制虚拟时钟，避免 `sleep`
- `u3_printf()`：把串口输出收集成文本行，直接断言 `ACK` / `STATUS` /
  `TASK_ARRIVE` 顺序
- `UI_*`、`CAMERA_*`、`WDG_*`、`USART3_*`：返回可控的固定状态

### 为什么用源码契约测试

嵌入式项目很难在 CI 中跑目标板。ADC 扫描顺序、看门狗时基、DMA 配置、
非阻塞控制环这类属性一旦被重构破坏，往往要到台架上才暴露。用正则和 C
函数提取做“源码契约”检查，成本极低，却能在提交阶段拦住大部分误改。

## 局限

- **不是 HIL**：不验证真实电机、PWM 死区、ADC 噪声、超声回波、WiFi 丢包、
  摄像头曝光等电气与射频特性。
- **不验证实时性**：状态机使用虚拟时钟，不能替代 DWT/GPIO 翻转测量的
  p50/p99 抖动数据。
- **源码断言对重构敏感**：函数改名或宏改名后需要同步更新测试；这是有意的
  提醒，不是测试框架缺陷。
- **桩可能与硬件行为漂移**：`stubs.c` 只模拟测试所需的接口语义，新增硬件
  交互时需要补桩。

真实硬件验证仍建议保留最小台架清单：三路循迹阈值、进站 4 次采样、STOP
兜底、断网重连、视频分片在 921600 波特率下的完整性。

## 新增用例

- 纯协议/算法：优先加到 `test_protocol_packets.py` 或新建 Python 用例。
- 真实 C 函数：优先用 `common.c_function()` 提取后在主机编译，避免复制实现。
- 状态机分支：在 `tests/host/test_car_state.c` 中通过 `dispatch()` 与
  `step()` 驱动，新增 `CHECK` / `CHECK_HAS` 断言。
- 新增硬件接口：在 `tests/host/stubs.c` 补桩，并在 `host_shim.h` 声明。
