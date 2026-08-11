# AIC_MedRobot_stm32f407 — STM32F407 药品配送小车固件

## FreeRTOS 支持

本项目已移植 FreeRTOS（内核 V11.1.0），采用条件编译，可在 RTOS 与裸机模式间一键切换：

- 总开关：`RTOS\rtos_conf.h` 中的 `RTOS_ENABLE`（1 = RTOS，0 = 裸机）
- 详细设计（任务划分、延时改造、中断向量、内存预算等）：见下文
  [「FreeRTOS 移植设计」](#freertos-移植设计)

## 项目简介

本项目是基于 **STM32F407** (Cortex-M4) 的嵌入式固件工程，作为 **AIC 药品配送机器人系统** 的下位机端运行。它集成了 **WiFi 遥控、JPEG 实时图传、黑线循迹、超声波避障** 等功能，通过 TCP 协议与 Qt 上位机（`AICMedicineRobotSystem`）通信。

## 开发环境

| 项目         | 内容                          |
| ------------ | ----------------------------- |
| **MCU**      | STM32F407ZGT6 (Cortex-M4)     |
| **IDE**      | Keil MDK v5 (µVision)         |
| **固件库**   | STM32F4xx 标准外设库 (StdPeriph) |
| **调试器**   | J-Link / ST-Link              |
| **RTOS**     | FreeRTOS V11.1.0（可选，条件编译） |

## 目录结构

```
AIC_MedRobot_stm32f407/
├── CORE/              # Cortex-M4 内核文件 & 启动文件
├── FWLIB/             # STM32F4 标准外设库
├── HARDWARE/          # 外设驱动模块（按功能拆分）
│   ├── 24CXX/         # EEPROM (AT24Cxx) 驱动
│   ├── ADC/           # ADC 初始化与采样
│   ├── ATK-ESP8266/   # ESP8266 WiFi 模块 AT 指令驱动
│   ├── BEEP/          # 蜂鸣器驱动
│   ├── CAMERA/        # 摄像头高层任务调度
│   ├── DCMI/          # DCMI 接口 (数字摄像头) 驱动
│   ├── EXTI/          # 外部中断 & 毫秒级时间戳
│   ├── GBK_LibDrive/  # GBK 字库驱动
│   ├── IIC/           # 软件 IIC 驱动
│   ├── KEY/           # 按键驱动
│   ├── LCD/           # TFT-LCD 显示屏驱动
│   ├── LED/           # LED 指示灯驱动
│   ├── MOTOR/         # 直流电机驱动（含循迹/避障运动控制）
│   ├── OV2640/        # OV2640 摄像头传感器配置
│   ├── PWM/           # PWM 输出驱动
│   ├── SPI/           # SPI 总线驱动
│   ├── TIMER/         # 定时器驱动
│   ├── TOUCH/         # 电容触摸屏驱动
│   ├── UI/            # 图形用户界面（3 页面：控制/数据/网络）
│   ├── USART2/        # USART2 (预留，未使用)
│   ├── USART3/        # USART3 (ESP8266 AT + 图传)
│   ├── W25QXX/        # SPI Flash (W25Qxx) 驱动
│   └── WIFI_TRANSFER/ # WiFi 数据透传模块
├── OBJ/               # 编译产物
├── SYSTEM/            # 基础支持模块 (delay/sys/usart)
└── USER/              # 用户代码 & 工程配置
    ├── main.c         # 主程序入口（硬件初始化 + 模式分支）
    ├── car_app.c      # 小车应用层（7 个步骤函数 + 主循环 + RTOS 主控任务）
    ├── wifi_cmd.c     # WiFi 命令解析与执行（startTask/stopTask 等）
    ├── AIC_MedRobot.uvproj      # Keil 工程文件
    └── ...
```

## 硬件接线

| 外设            | 接口      | 说明                         |
| --------------- | --------- | ---------------------------- |
| **OV2640 摄像头** | DCMI + SCCB | 8 位并行数据，JPEG 输出      |
| **ESP8266 WiFi** | USART3    | AT 指令控制 + 透传（配网 115200，图传 921600） |
| **WiFi 图传**    | USART3    | DMA 发送 JPEG（图传临时提速至 921600） |
| **USART2**       | USART2    | 预留（历史遗留，当前未使用） |
| **TFT-LCD**      | FSMC      | 显示与触摸交互               |
| **电机驱动**     | TIM PWM   | 方向 + 速度控制              |
| **HC-SR04 超声波** | GPIO + TIM | 测距避障                    |
| **ADC 循迹**     | ADC3      | 3 路模拟输入 (左=PF7/中=PF6/右=PF5) |

## 软件架构

### 主循环 (`CAR_LineFollow`)

默认采用裸机前后台架构，`main()` 初始化后进入 `CAR_LineFollow()` 大循环；`RTOS_ENABLE=1` 时同一套步骤函数由 FreeRTOS 任务调度（见「FreeRTOS 移植设计」）：

```
┌─────────────────────────────┐
│  WiFi 命令检查 (ESP8266)    │
├─────────────────────────────┤
│  JPEG 帧发送 (USART3 DMA)   │
├─────────────────────────────┤
│  UI 任务 (触摸/显示)        │
├─────────────────────────────┤
│  摄像头遮挡检测              │
├─────────────────────────────┤
│  超声波测距 & 避障          │
├─────────────────────────────┤
│  ADC 循迹传感器采样          │
├─────────────────────────────┤
│  运动控制 (循迹/方向记忆)    │
└─────────────────────────────┘
```

### 控制优先级 (从高到低)

1. **摄像头遮挡** — 遮挡立即停车保护
2. **超声波避障** — 距离 < 25cm 停车，> 28cm 恢复
3. **WiFi 遥控覆盖** — 收到遥控指令后 500ms 内优先响应
4. **黑线循迹** — 默认运动模式

### TCP 通信协议

与上位机（`AICMedicineRobotSystem`）共用端口 **8086**：

**指令格式（上位机 → 小车）**

| 指令         | 说明                 |
| ------------ | -------------------- |
| `forWard`    | 前进                 |
| `backWard`   | 后退                 |
| `lefTurn`    | 左转                 |
| `righTurn`   | 右转                 |
| `letStop`    | 停止                 |
| `speedUp`    | 加速 (+50 PWM)       |
| `speedDown`  | 减速 (-50 PWM)       |
| `startTask`  | 使能循迹（上位机"添加配送任务/呼叫小车/返回充电"时发送） |
| `stopTask`   | 停止循迹并刹停（上位机"暂停/取消/手动控制"时发送） |

> 每条指令以 `\r\n` 结尾；MCU 侧按行拆分执行，快速按键合并帧也不会丢指令。

**视频帧格式（小车 → 上位机）**

```
帧头(0xAA55) + JPEG长度(4B LE) + JPEG数据
```

**任务配送命令（上位机 → 小车，P0 闭环协议）**

| 指令 | 说明 |
| --- | --- |
| `TASK_START,<task_id>,<from>,<to>` | 下发任务，如 `TASK_START,T-2025,S1,S3` |
| `STOP,<task_id>,<station_id>` | 上位机识别到目标站点后要求停车 |
| `TASK_CONFIRM,<task_id>,<load\|unload>,<user>` | 装/卸药人工确认 |
| `TASK_PAUSE,<task_id>` | 暂停任务 |
| `TASK_RESUME,<task_id>` | 恢复任务 |
| `TASK_CANCEL,<task_id>` | 取消任务回 IDLE |

> 所有任务命令均回 `ACK,<task_id>,<0|1>`；上位机 500ms 未收到 ACK 自动重发（最多 3 次）。
> 站点 ID：S1=药房01、S2=病房001、S3=病房002、S4=车库01（与上位机下拉项一一对应）。

**任务状态上报（小车 → 上位机，文本行，`\r\n` 结尾）**

| 报文 | 说明 |
| --- | --- |
| `ACK,<task_id>,<0|1>` | 下行命令确认，1=成功 0=拒绝 |
| `ENTER_STATION,<task_id>,<seq>` | 三路全黑横带触发进站（seq=0 取药点 / 1 病房），请求开启识别窗口 |
| `STATUS,<task_id>,<state>` | state: NAV/APPROACH/STOPPED/WAIT_LOAD/WAIT_UNLOAD/PAUSED/TIMEOUT/CANCELLED |
| `TASK_ARRIVE,<task_id>,<station_id>` | 已停稳，等待人工确认 |
| `TASK_DONE,<task_id>` | 卸药确认后任务完成 |
| `HEARTBEAT,<battery>,<state>` | 每 1s 心跳，上位机据此刷新电量与在线状态 |

> 文本行与视频帧共用一条 TCP 连接：上位机先按 `\r\n` 拆行、命中协议前缀才按文本处理，
> 其余数据走 AA55 帧解析。车端在视频 DMA 发送期间会丢弃文本以避免破坏帧数据，
> 由上位机 ACK 重发机制兜底。

### 功能详解

#### 🚗 黑线循迹
- 3 路 ADC 检测地面黑线，阈值 2000
- 中路黑线直行，左转/右转根据两侧偏差
- 全白时按上次转向方向搜索

#### 📷 JPEG 图传
- OV2640 配置为 JPEG 输出 (QVGA 320×240)
- DCMI + DMA 双缓冲采集，USART3 DMA 发送

#### 📡 WiFi 遥控
- ESP8266 AT 指令接收，匹配预定义指令

#### 🛑 超声波避障
- HC-SR04 周期测距 (50ms)，可 UI 开关

#### 🖥️ 触摸屏 UI
- 3 页面：控制 / 数据 / 网络
- 实时距离、避障状态显示
- 网络页面显示当前网络配置：WiFi 名称、本机 IP、服务器 IP/端口、在线状态
- 数据页（调试）显示三路循迹 ADC 原始值（L/M/R）与运动状态/速度（ST/PWM）
- 控制页装卸确认按钮：原方向键区域改为「确认装药/确认取药」触摸按钮，任务等待装/卸确认时按钮高亮，按下即完成确认（等效 `TASK_CONFIRM`，不依赖上位机）
- 字库（`ui_zh_font.c`）为 16×16 中文点阵，本轮新增 确/认/装/取 四字

#### 📦 任务配送（P0 闭环）
- 状态机：`TASK_IDLE → NAV_TO_LOAD → APPROACH_LOAD → WAIT_LOAD → NAV_TO_WARD → APPROACH_WARD → WAIT_UNLOAD → DONE`
- 进站触发：左中右三路同时 ≥2000 且连续 10 个采样周期（约 20ms）→ 降速至 PWM 200 → 上报 `ENTER_STATION`
- 停车：收到目标站点 `STOP` 且校验通过后停车，上报 `TASK_ARRIVE`；2s 内未收到 `STOP` 强制停车（兜底，补发 `TASK_ARRIVE,<task>,<站>,FALLBACK` 并报 `STATUS,TIMEOUT`，上位机弹窗可人工确认）
- 超时：每段循迹 5 分钟超时中止；装/卸确认 5 分钟超时 LED2 闪烁告警（蜂鸣器 PF8 已被 TIM13 PWM 占用，不可用）
- 车端触摸确认：控制页「确认装药/确认取药」按钮按下等效 `TASK_CONFIRM`（装药→导航病房，取药→`TASK_DONE` 回 IDLE），WiFi 断链也能现场交接
- 心跳：每 1s 上报电量与任务状态（电量暂为固定值 100，预留电池采样扩展）

## 已知问题与注意事项

### 硬件资源占用说明（已核对）

- **PF9（LED1/D1）由 TIM14 右电机 PWM 独占**：`LED_Init()` 已不再配置 PF9，指示灯统一使用 PF10（LED2/D2）；D1 因引脚被 PWM 占用在硬件上不可用，`led.h` 中 `LED1` 宏仅作引脚记录，请勿使用。
- **PF8（蜂鸣器）由 TIM13 左电机 PWM 独占**：`beep.h` 的 `BEEP` 宏在电机启用后不可用（引脚被定时器复用），任务确认超时告警改用 LED2 闪烁。
- **PB8/PB9 为按键 EXTI 输入**：KEY1(PB8)/KEY0(PB9) 下降沿触发（`EXTIX_Init`）；24CXX 软件 IIC（`AT24CXX_Init`）仅在 `TP_Init()` 电阻屏分支初始化。本工程使用电容屏（GT9147/OTT2001A），不初始化 24CXX，无实际冲突；若后续改用电阻屏，需重新评估按键与校准存储的引脚复用。

> 循迹 ADC 通道核对（依据 STM32F407 官方 CubeMX 数据库）：左=PF7(ADC3_CH5)、
> 中=PF6(ADC3_CH4)、右=PF5(ADC3_CH15)，与 `adc.c`/`car_app.c` 代码一致；
> PC5 是 ADC1/ADC2 的 IN15，与 ADC3 无关，不存在 PC5 复用冲突。

### 已修复/优化的软件问题

- **JPEG 图传改为非阻塞 DMA 发送**：原 `USART3_Send_DMA_Start()` 会忙等整帧发送完成（115200 波特率下约 0.5~1s），期间超声波避障/电机控制被冻结。现由 DMA 中断置完成标志，主循环空闲时再重新挂载 DCMI，保障避障实时性。
- **`MOTOR_Stop()` 移除 100ms 阻塞延时**：障碍/遮挡触发停车时立即响应，不再阻塞主循环。
- **USART2 中断优先级越界修正**：`NVIC_PriorityGroup_2` 下抢占优先级范围 0~3，原配置 4/5 被静默截断为最高优先级，已改为 3/2。
- **JPEG 帧缓冲越界防护**：帧头/CRC 写入与 `memmove` 增加边界检查，防止 JPEG 填满缓冲时越界写。
- **JPEG 模式下摄像头遮挡检测**：JPEG 模式时 `camera_frame`（RGB565 预览缓冲）不再更新，原遮挡检测基于陈旧数据。现增加基于 JPEG 帧大小的遮挡启发式检测（阈值 `JPEG_OCCLUSION_SIZE_THRESHOLD`，按实际场景调整），并停止刷新陈旧的预览画面。
- **`LED_Init()` 不再配置 PF9**：PF9 已由 TIM14 右电机 PWM（TIM14_CH1）独占，LED1(D1) 硬件上不可用；现仅初始化 PF10（LED2），消除 LED 初始化对 PWM 引脚的潜在覆写。

### 仍待优化

- **图传帧率仍受限于串口带宽**：已临时提升至 921600，若要更高帧率需进一步压缩 JPEG 尺寸或更换更高带宽链路。
- **USART2 为历史遗留初始化**（蓝牙/旧版 8266 驱动），当前未被使用，可考虑移除。
- **心跳电量暂为固定值 100**：待接入电池电压采样后替换 `TASK_BATTERY_DEFAULT`。
- **站点 ID 暂为编译期常量**：后续可存入 24CXX EEPROM 便于现场配置（注意 PB8/PB9 按键复用约束）。


## FreeRTOS 移植设计

> 本文档原为独立文件 `RTOS_DESIGN_README.md`，已并入本 README。
> 内核版本：**FreeRTOS Kernel V11.1.0**（FreeRTOSv202406.04-LTS，2024-04 发布），
> 编译器：**ARMCC V5.06 update 4**，Keil MDK 工程 `USER\AIC_MedRobot.uvproj`。

### 1. 总开关与条件编译机制

所有 RTOS 相关代码统一由 **`RTOS\rtos_conf.h`** 中的宏控制：

```c
#define RTOS_ENABLE  1   // 1 = 启用 FreeRTOS；0 = 关闭（裸机大循环）
```

- 切换后重新编译即可，**两种模式共用同一份工程、同一份硬件初始化代码**。
- `RTOS_ENABLE` 通过 `sys.h` 引入 `rtos_conf.h` 后对全工程可见；`stm32f4xx_it.c`
  单独 `#include "rtos_conf.h"`。
- 条件编译的分支点：
  - `main()`：`#if RTOS_ENABLE  RTOS_Start();  #else  CAR_LineFollow();`
  - `CAR_LineFollow()`：内部仅裸机模式调用 `CAR_StepJpeg()/CAR_StepUi()`；
    其余步骤函数两种模式共用。
  - `delay.c`：RTOS 模式使用 DWT + `vTaskDelay` 延时（见第 6 节）。
  - `stm32f4xx_it.c`：SVC/PendSV/SysTick 空实现仅在裸机模式编译。
  - `FreeRTOSConfig.h`：栈溢出/内存失败钩子仅在 RTOS 模式启用，保证裸机模式
    链接时内核不会被意外拖入固件。

> 构建验证：`RTOS_ENABLE=1` 与 `RTOS_ENABLE=0` 两种配置均编译通过
> （0 Error / 0 Warning）。

---

### 2. 新增/修改文件清单

| 文件 | 类型 | 说明 |
| --- | --- | --- |
| `RTOS\rtos_conf.h` | 新增 | RTOS 总开关 `RTOS_ENABLE` |
| `RTOS\FreeRTOSConfig.h` | 新增 | FreeRTOS 内核配置（堆、节拍、中断优先级等） |
| `RTOS\rtos_app.c` | 新增 | RTOS 业务层：任务创建、调度入口、钩子、同步对象 |
| `RTOS\rtos_app.h` | 新增 | RTOS 业务层接口（`RTOS_Start` 等） |
| `SYSTEM\delay\delay.c` | 修改 | 增加 FreeRTOS 感知的 `delay_us/delay_ms`（DWT/`vTaskDelay`） |
| `SYSTEM\sys\sys.h` | 修改 | 引入 `rtos_conf.h` |
| `USER\main.c` | 修改 | 主循环拆分为步骤函数 + RTOS 主控任务 + 模式分支 |
| `USER\stm32f4xx_it.c/h` | 修改 | SVC/PendSV/SysTick：裸机空实现 / RTOS 模式 `__asm` B 跳板 |
| `CORE\startup_stm32f40_41xxx.s` | 修改 | 删除 3 个空向量桩，补 `IMPORT` 向量符号 |
| `USER\AIC_MedRobot.uvproj` | 修改 | 新增 FreeRTOS 组（6 个文件）与 3 条包含路径 |

FreeRTOS 内核源码（仅构建所需文件）位于 `RTOS\FreeRTOS-Kernel\`，**未做任何修改**，
仅通过 `RTOS\FreeRTOSConfig.h` 配置；原 `FreeRTOSv202406.04-LTS\` 整包不再参与构建，可整体移除。

---

### 3. 任务划分与优先级

| 任务 | 函数 | 优先级 | 周期 | 栈(字) | 职责 |
| --- | --- | --- | --- | --- | --- |
| 主控任务 | `vTaskCarControl`（main.c） | 4（最高） | 2ms | 512 | 摄像头任务+遮挡检测、超声波测距+避障、ADC 采样、WiFi 命令执行、循迹/运动控制 |
| WiFi 命令任务 | `vTaskWifi`（rtos_app.c） | 3 | 10ms | 256 | 轮询 USART3 接收状态，把原始命令字符串拷贝入队 |
| JPEG 图传任务 | `vTaskJpeg`（rtos_app.c） | 3 | 5ms | 256 | JPEG 帧加头/CRC 并通过 DMA 发送，发送完重启 DCMI 采集 |
| UI 任务 | `vTaskUi`（rtos_app.c） | 1（最低） | 20ms | 384 | 触摸扫描、界面刷新、图像参数调节 |
| 空闲任务 | 系统自带 | 0 | - | 128 | 内核空闲任务 |

设计要点：

- **主控任务保持最高优先级**，保证循迹/避障的实时性；它内部与裸机模式
  完全相同的步骤顺序执行（见第 4 节）。
- **电机只由主控任务驱动**。WiFi 任务只负责“读串口→入队”，不触碰电机，
  避免两个任务并发写 PWM/GPIO 的竞态。
- **JPEG 与 WiFi 同优先级（3）**，靠时间片轮转共享 CPU；JPEG 发送是 DMA
  驱动、5ms 轮询一次，开销很小。
- **UI 任务最低优先级**，可被随时抢占；触摸扫描、LCD 刷新这类“慢”操作
  不再拖累运动控制。

---

### 4. 裸机主循环如何“原样”变成 RTOS 任务

原 `CAR_LineFollow()` 是一个 `while(1)` 大循环。为兼顾“尽量不改动原有函数”
与“多任务并行”，将其循环体按职责拆成 7 个**步骤函数**（均在 `main.c`），
每个步骤内部的逻辑、顺序、局部变量含义与原循环逐行对应：

```
CAR_StepWifi   ->  WiFi 命令检查（裸机直读串口 / RTOS 从队列取命令）
CAR_StepJpeg   ->  JPEG 帧处理与发送（仅裸机；RTOS 由 vTaskJpeg 承担）
CAR_StepUi     ->  UI_Task（仅裸机；RTOS 由 vTaskUi 承担）
CAR_StepCamera ->  CAMERA_Task + 遮挡检测（含 LCD 互斥）
CAR_StepRange  ->  超声波测距 + 避障逻辑（含 LCD 互斥）
CAR_StepAdc    ->  ADC 采样 + LED 心跳 + 数据页刷新（循迹值/状态/速度）
CAR_StepMotion ->  避障/遮挡/遥控暂停 -> 起步加速 -> 循迹逻辑
```

- 裸机模式：`CAR_LineFollow()` 按原顺序调用全部步骤，行为与原来一致。
- RTOS 模式：`vTaskCarControl` 以 2ms 周期调用除 UI/JPEG 外的步骤；
  UI、JPEG、WiFi 由各自任务并行执行。
- 所有原有驱动接口（`MOTOR_*`、`CAMERA_*`、`UI_*`、`HC_SR04_*`、
  `atk_8266_*`、`jpeg_*`、`ADC_*`）均被原样复用，未修改其内部实现。

---

### 5. 任务间通信与同步

### 5.1 WiFi 命令队列（生产者-消费者）

```c
typedef struct { char cmd[64]; } WiFiCmdMsg;   // RTOS/rtos_app.h
```

- `vTaskWifi`：`atk_8266_at_response_rdCmd(1)` 读到命令后拷贝成 64 字节
  消息 `xQueueSend` 入队（队列深 4，满则丢弃——遥控命令不积压，保证最新
  命令尽快生效）。
- `vTaskCarControl`：每周期 `RTOS_WifiCmdReceive()` 非阻塞出队，调用
  `wifi_dispatch_commands()` 拆分后逐条调用原 `handle_wifi_command()` 解析并驱动电机。
- 好处：电机唯一写者；主控任务无需等待串口；命令解析逻辑 100% 复用原函数。

### 5.2 任务控制命令与循迹使能（上位机联动）

- 上位机每条命令以 `\r\n` 结尾；MCU 侧 `wifi_dispatch_commands()` 用 `strtok`
  按行拆分，解决快速按键（按下/松开间隔 < 100ms）时多条命令合并成一帧、
  旧逻辑只执行第一条导致小车不停的问题。
- 新增两条任务命令（裸机/RTOS 共用同一解析入口）：
  - `startTask`：置 `line_enabled = 1`，使能循迹（上位机"添加配送任务 /
    呼叫小车 / 返回充电"时发送）。
  - `stopTask`：置 `line_enabled = 0` 并 `MOTOR_Stop()`（上位机"暂停 / 取消 /
    手动控制"时发送）。
- `CAR_StepMotion()` 增加门控：`line_enabled == 0` 时小车保持静止，
  不进入起步/循迹流程；遥控命令（`forWard` 等）仍可经 `wifi_override`
  临时驱动，500ms 覆盖窗口结束后回到静止或循迹。
- 小车初始 `line_enabled = 0`：上电后不自动循迹，等待上位机任务指令。

### 5.3 图传波特率（临时提速）

- 初始化仍按 `usart3_init(115200)` 标准波特率配网。
- TCP 连接建立后、进入透传前发送 `AT+UART_DEF=921600,8,1,0,0`，
  成功后用 `usart3_set_baudrate(921600)` 重配 USART3（仅改 BRR，不动
  `USART3_RX_STA`/TIM7/DMA），提高 JPEG 图传帧率。
- 若模块固件不支持 `AT+UART_DEF`，保持 115200 继续，功能不受影响。
- 网络参数（SSID/密码/服务器 IP/端口/图传波特率）集中在
  `HARDWARE/ATK-ESP8266/wifi_config.h`，不再使用 `common.c` 中的旧硬编码变量。
- 配网成功（CWJAP 应答 `WIFI GOT IP`）后、进入透传模式前发送 `AT+CIFSR`
  查询并缓存 STA 本机 IP（`wifista.c` 的 `atk_8266_query_local_ip()`），
  供 UI 网络页面只读显示；透传模式下不再发送 AT 指令。

### 5.4 LCD 访问互斥（互斥量，带优先级继承）

UI 任务与主控任务都会写屏（`UI_Task` 与 `UI_SetDistance/UI_SetObstacle/
CAMERA_Task`），为防止两任务交叉写 FSMC 造成显示撕裂，LCD 访问统一经：

```c
RTOS_LcdLock();   // xSemaphoreTake(xLcdMutex, portMAX_DELAY)
...LCD 操作...
RTOS_LcdUnlock(); // xSemaphoreGive(xLcdMutex)
```

裸机模式下 `RTOS_LcdLock/Unlock` 定义为空操作（`rtos_app.h`），调用点统一。

### 5.5 共享状态与原子性

- `jpeg_data_ok / jpeg_data_len / jpeg_last_frame_bytes`：由 DCMI 中断写、
  JPEG 任务读；32 位读写是 Cortex-M4 的原子操作，无需加锁。
- `adcL/adcM/adcR`：仅主控任务读写；UI 通过 `UI_SetLineSensors()` 接收副本用于数据页显示。
- `UI_GetPage()` 返回的页面号：UI 任务写、主控任务读，单字节原子，允许
  轻微滞后（仅影响画面裁剪判断），可接受。

---

### 6. 延时函数改造（关键点）

原 `delay_us/delay_ms` 用 SysTick 忙等。FreeRTOS 启动后 **SysTick 归内核
独占**（节拍中断），若任务里再用 SysTick 忙等会改写 `LOAD` 并关闭计数器，
直接破坏系统节拍。因此 `SYSTEM\delay\delay.c` 增加 RTOS 分支：

- `delay_us()`：改用 **DWT 周期计数器**（`DWT->CYCCNT`）忙等，不碰 SysTick，
  可在任务/初始化阶段安全使用；`delay_init(168)` 时完成 DWT 使能。
- `delay_ms()`：调度器运行后调用 `vTaskDelay()` **主动让出 CPU**；
  调度器未启动时退化为 DWT 忙等（初始化阶段可用）。
- 原裸机 SysTick 忙等路径（含 `delay_xms`）在 `#if !RTOS_ENABLE` 下原样保留，
  裸机行为不受影响。
- `MOTOR_LineTurnLeft/Right`（内含 `delay_ms(20)`）、`HC_SR04_GetDistance`
  （内含约 30ms `delay_us` 忙等）等“慢”调用在主控任务里会阻塞该任务
  一小段时间，但**不会阻塞其他任务**——这正是 RTOS 并行的收益。

---

### 7. 中断向量处理（移植层关键）

FreeRTOS RVDS/ARM_CM4F 移植层（`portable\RVDS\ARM_CM4F\port.c`）提供的
中断入口名是 `vPortSVCHandler / xPortPendSVHandler / xPortSysTickHandler`，
而工程启动文件向量表使用 `SVC_Handler / PendSV_Handler / SysTick_Handler`。

ARMCC V5 不支持跨编译单元的 `alias`，且 PendSV 的上下文切换依赖异常入口时
`r14 = EXC_RETURN` 的语义，不能套 C 包装函数。因此把三个向量直接定义在
**始终参与编译**的 `USER\stm32f4xx_it.c` 中，RTOS 模式用 `__asm` 内联汇编
写 `B` 跳板（`B` 不触碰 `r14`，与向量表直连等价）：

```c
__asm void PendSV_Handler(void)
{
    IMPORT xPortPendSVHandler
    B      xPortPendSVHandler
}
```

- `B` 跳转不改写 `r14`，与向量表直连完全等价，保证 PendSV 正确保存/恢复
  `EXC_RETURN`。
- 三个向量定义在 `stm32f4xx_it.c`（USER 组，始终编译），不依赖 RTOS 组里
  是否有额外的汇编文件；裸机模式（`RTOS_ENABLE=0`）编译为空实现，内核不会
  被链接进固件（RAM 零开销）。
- 启动文件同步修改：删除 SVC/PendSV/SysTick 三个空向量桩并补 `IMPORT`。

---

### 8. 内存分配与预算

`configTOTAL_HEAP_SIZE = 10KB`（heap_4，支持碎片合并），任务栈、TCB、
队列、互斥量全部从堆分配：

| 占用项 | 大小 |
| --- | --- |
| 主控任务栈 | 2KB |
| UI 任务栈 | 1.5KB |
| WiFi / JPEG 任务栈 | 各 1KB |
| 空闲任务栈 | 0.5KB |
| TCB（5 个）+ 队列 + 互斥量 | ≈1KB |
| 合计 | ≈7KB，堆内剩余 ≈3KB 余量 |

链接结果（RTOS 模式）：`Code=80.5KB / RO=9KB / RW=0.4KB / ZI=119KB`，
总 SRAM 128KB，剩余约 7.5KB（含 MSP 栈区），Flash 远小于 1MB。
裸机模式：`Code=65.3KB / ZI=108.8KB`，与移植前基本一致（内核未链接）。

> 大数组 `camera_frame[140][236]`（66KB，RGB565）与 `jpeg_buf[10240]`
> （40KB）仍在原位置，未动内存布局。

---

### 9. 中断优先级配置

`FreeRTOSConfig.h` 关键配置：

```c
#define configCPU_CLOCK_HZ       168000000UL
#define configTICK_RATE_HZ       1000
#define configPRIO_BITS          4
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  (5 << (8-4))   // 0x50
#define configKERNEL_INTERRUPT_PRIORITY       (15 << (8-4))  // 0xF0
```

- STM32F407 实现 4 位中断优先级；`0x50` 是“可调用 FreeRTOS API 的 ISR
  优先级下限”，本项目 ISR 均不调用 FreeRTOS API，因此全部外设中断都高于
  该阈值也不会触发断言。
- 工程沿用 `NVIC_PriorityGroup_2`（`EXTIX_Init` 设置），经 port.c 断言
  （`ulMaxPRIGROUPValue = (7-4)<<8 = 0x300`，Group2 的 AIRCR=0x200）验证通过，
  **无需改优先级分组**。
- 系统节拍由 SysTick 产生（1ms）；应用时间基准 `EXTIX_GetMillis()`（TIM6，
  1ms 中断）与 SysTick 并存，互不干扰。

---

### 10. 钩子与错误处理

- `vApplicationMallocFailedHook()`：堆耗尽 → 点亮 LED2 并停机。
- `vApplicationStackOverflowHook()`：任务栈溢出（`configCHECK_FOR_STACK_OVERFLOW=2`）
  → 点亮 LED2 并停机。
- 调试建议：若上电后 LED2 常亮，说明堆/栈配置不足，优先检查第 8 节预算。

---

### 11. 已知限制与注意事项

1. **`delay_ms` 与任务节拍**：RTOS 模式下 `delay_ms` 在调度器运行后转为
   `vTaskDelay`，因此不能再在 ISR 中调用（本项目所有 ISR 均未调用）。
2. **主控任务内的“慢”调用**：`MOTOR_LineTurnLeft/Right` 内的 `delay_ms(20)`
   会短暂阻塞主控任务（其他任务照常运行）；若需要更精细的转向控制，可把
   转向改为状态机（当前保留原实现，符合“不改动原有函数”的约束）。
3. **I2C 时序**：`delay_us` 忙等可能被 1ms 节拍抢占，I2C（SCCB/触摸）时序
   会变慢但不会出错（I2C 无最小时钟频率要求）。
4. **UI 页面号跨任务读取**：允许短暂滞后，仅影响摄像头裁剪显示判断。
5. **UCOS 与 FreeRTOS 互斥**：`SYSTEM_SUPPORT_UCOS` 与 `RTOS_ENABLE`
   不应同时为 1（`delay.c` 的取舍按此假设）。
6. **堆大小调节**：若新增任务/功能，先估算栈需求，再在
   `FreeRTOSConfig.h` 调整 `configTOTAL_HEAP_SIZE`，注意 RAM 上限 128KB。

---

### 12. 如何切换与验证

1. 修改 `RTOS\rtos_conf.h` 中 `RTOS_ENABLE` 为 1 或 0。
2. 用 Keil 打开 `USER\AIC_MedRobot.uvproj` 重新编译下载。
3. RTOS 模式运行表现：小车行为与裸机一致，但超声波测距的 30ms 等待期间
   UI/图传/WiFi 依然流畅运行；LED2 常亮表示堆/栈异常。

---

## 编译与烧录

1. 打开 `USER/AIC_MedRobot.uvproj`（Keil µVision）
2. 选择芯片：**STM32F407ZGT6**
3. 点击 **Build** (F7) 编译
4. 生成 `OBJ/AIC_MedRobot.hex`，使用 J-Link / ST-Link 烧录

---

*文档版本: 1.2 | 最后更新: 2026-08-09 | 合并 RTOS 设计说明*
