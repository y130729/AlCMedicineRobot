# STM32F407 药品配送车固件

车端固件运行于 STM32F407ZGT6，负责三路循迹、超声波避障、OV2640 JPEG 图传、
触摸屏交互和药品配送任务状态机。固件通过 ESP8266 与 Qt 上位机建立 TCP 连接，
支持 FreeRTOS 与裸机两种运行模式。

## 功能

- 三路模拟循迹：ADC3 扫描 PF7/PF6/PF5，TIM2 触发、DMA 循环采样。
- 运动控制：左右轮独立 PWM、比例差速循迹、丢线搜索与遥控覆盖。
- 超声波避障：HC-SR04 双边沿捕获，距离小于 25 cm 停车，大于 28 cm 恢复。
- JPEG 图传：OV2640 QVGA，DCMI + DMA 采集，USART3 分片发送。
- 触摸屏 UI：控制、数据、网络三页，支持车端装药/取药确认。
- 任务配送：进站检测、目标站点停车、装/卸确认、超时与兜底处理。
- 可靠性：IWDG、任务级心跳、幂等协议、文本发送队列、接收 DMA 环形缓冲。

## 硬件平台

| 项目 | 内容 |
| --- | --- |
| MCU | STM32F407ZGT6，Cortex-M4F，168 MHz，1 MB Flash |
| 摄像头 | OV2640，DCMI 8 位并行 + SCCB，JPEG QVGA 320×240 |
| WiFi | ATK-ESP8266，USART3 AT 指令与 TCP 透传 |
| 显示 | TFT-LCD，FSMC 接口，电容触摸 |
| 电机 | 2× 直流电机，TIM13/TIM14 PWM，GPIO 方向控制 |
| 循迹 | 3× 模拟循迹传感器，ADC3 |
| 测距 | HC-SR04，GPIO 触发 + EXTI 回波 |
| 调试 | J-Link / ST-Link，SWD |

## 引脚与资源

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| 循迹左 | PF7 | ADC3_IN5 |
| 循迹中 | PF6 | ADC3_IN4 |
| 循迹右 | PF5 | ADC3_IN15 |
| 左电机 PWM | PF8 | TIM13_CH1 |
| 右电机 PWM | PF9 | TIM14_CH1 |
| 左电机方向 | PC2 / PC3 | 方向与调速 |
| 右电机方向 | PC4 / PC5 | 方向与调速 |
| 超声波 TRIG | PF2 | 推挽输出 |
| 超声波 ECHO | PF3 | 双边沿 EXTI |
| USART3 TX / RX | PB10 / PB11 | 连接 ESP8266 |
| 状态指示灯 | PF10 | LED2 |
| 按键 | PB8 / PB9 | KEY1 / KEY0 |
| 摄像头 | DCMI + SCCB | 8 位并行数据 |
| 触摸屏 | FSMC | 显示与触摸 |

### 资源复用约束

- **PF9** 由 TIM14_CH1 右电机 PWM 占用，LED1 不可用；状态指示统一使用 PF10（LED2）。
- **PF8** 由 TIM13_CH1 左电机 PWM 占用，蜂鸣器不可用；告警使用 LED2。
- **PB8/PB9** 为按键 EXTI 输入。24CXX 软件 IIC 仅在电阻屏校准分支初始化；
  当前使用电容屏，不启用 24CXX。

循迹 ADC 通道依据 STM32F407 官方引脚复用表定义：左 PF7(ADC3_CH5)、
中 PF6(ADC3_CH4)、右 PF5(ADC3_CH15)，与 ADC3 扫描顺序一致。

## 目录结构

```text
AIC_MedRobot_stm32f407/
├── CORE/              # Cortex-M4 内核文件与启动文件
├── FWLIB/             # STM32F4 标准外设库
├── HARDWARE/          # 外设驱动
│   ├── ADC/           # 三路循迹采样
│   ├── ATK-ESP8266/   # WiFi AT 指令与链路管理
│   ├── CAMERA/        # 摄像头与遮挡检测
│   ├── DCMI/          # 数字摄像头接口
│   ├── MOTOR/         # 电机与运动控制
│   ├── TIMER/         # 定时器与超声波捕获
│   ├── UI/            # 触摸屏界面
│   ├── USART3/        # WiFi 串口、文本队列与 DMA 接收
│   └── WIFI_TRANSFER/ # JPEG 分片图传
├── RTOS/              # FreeRTOS 内核、配置与任务层
├── SYSTEM/            # delay/sys/usart 等基础支持
└── USER/
    ├── main.c         # 初始化、裸机循环与 RTOS 控制任务
    ├── car_app.c      # 控制逻辑、任务状态机与协议处理
    ├── wifi_cmd.c     # WiFi 命令解析
    └── AIC_MedRobot.uvproj
```

## 软件架构

### 运行模式

`RTOS/rtos_conf.h` 中的 `RTOS_ENABLE` 选择运行模式：

```c
#define RTOS_ENABLE  1   /* 1 = FreeRTOS，0 = 裸机大循环 */
```

两种模式共用同一套初始化代码和步骤函数。RTOS 模式下，控制任务以 5 ms 周期执行；
裸机模式下由 `CAR_LineFollow()` 顺序调用相同步骤。

### 任务划分

| 任务 | 函数 | 优先级 | 周期 | 栈 | 职责 |
| --- | --- | ---: | ---: | ---: | --- |
| 控制任务 | `vTaskCarControl` | 4 | 5 ms | 2 KB | 采样、避障、循迹、任务状态机、电机 |
| WiFi 任务 | `vTaskWifi` | 3 | 10 ms | 1 KB | 读取 USART3 命令并入队 |
| 图传任务 | `vTaskJpeg` | 3 | 5 ms | 1 KB | 发送视频分片、维护 DCMI |
| UI 任务 | `vTaskUi` | 1 | 20 ms | 1.5 KB | 触摸扫描与页面刷新 |
| 空闲任务 | 内核 | 0 | - | 0.5 KB | 系统空闲 |

设计约束：

- 电机只由控制任务驱动，WiFi 任务只负责接收和入队，避免并发写 PWM/GPIO。
- 图传与 WiFi 任务同优先级，时间片轮转；图传由 DMA 发送，轮询开销小。
- UI 任务优先级最低，LCD 刷新不会阻塞运动控制。

### 控制优先级

1. 摄像头遮挡：立即停车。
2. 超声波避障：小于 25 cm 停车，大于 28 cm 恢复。
3. WiFi 遥控覆盖：收到遥控命令后 500 ms 内优先执行。
4. 黑线循迹：默认运动模式。

### 控制环

- ADC3 由 TIM2 TRGO 以 1 kHz 触发，DMA2 Stream0 循环搬运三路采样；控制任务
  每 5 ms 读取一次平均值。
- HC-SR04 使用 PF2 触发、PF3 双边沿 EXTI 与 DWT 时间戳，`HC_SR04_Poll()`
  非阻塞，超时 45 ms。
- 循迹采用比例差速；全白时按上次转向执行 pivot/settle 搜索状态机。
- `STATS` 输出控制周期与循环体耗时分位数：

```text
STATS,dt50=<us>,dt99=<us>,dtmax=<us>,w50=<us>,w99=<us>,wmax=<us>,ovr=<n>,n=<cycles>,wdg=<n>,txov=<n>
STATS2,vid_drop=<n>,rxov=<n>,ore=<n>
```

## 任务配送

### 状态机

```text
TASK_IDLE
  → NAV_TO_LOAD → APPROACH_LOAD → WAIT_LOAD
  → NAV_TO_WARD → APPROACH_WARD → WAIT_UNLOAD
  → TASK_IDLE
```

异常状态：`TASK_PAUSED`、`TASK_LINE_LOST`、`TASK_ABORTED`。

### 进站与停车

- 三路循迹同时检测到黑带，并连续确认 4 个控制周期（5 ms/次，约 20 ms）后，
  降速至 PWM 200 并上报 `ENTER_STATION`。
- 进站触发后必须等待三路全部离开黑带（出现白区）才允许下一次触发。
- 收到目标站点 `STOP` 后停车并上报 `TASK_ARRIVE`。
- 2 s 未收到 `STOP` 时兜底停车，补发 `TASK_ARRIVE,<task>,<station>,FALLBACK`
  并上报 `STATUS,TIMEOUT`。

### 超时

| 场景 | 超时 | 行为 |
| --- | ---: | --- |
| 单段导航 | 5 min | 中止任务并上报 |
| 装/卸确认 | 5 min | LED2 闪烁告警 |
| 循迹连续全白 | 8 s | 停车并上报 `LINE_LOST` |

### 人工确认

上位机与车端触摸屏均可完成装/卸确认。车端控制页在等待确认时高亮
“确认装药/确认取药”按钮，按下后等效发送 `TASK_CONFIRM`，WiFi 断开时仍可现场交接。

## 通信协议

### TCP 连接

- 上位机作为 TCP 服务端，端口 **8086**；ESP8266 作为客户端接入。
- 配网阶段 USART3 使用 **115200** 8N1；链路建立后可切换至 **921600** 发送图传。
- 链路低于 **460800** 时车端跳过图传，仅保留控制与状态报文。

### 遥控命令

每条命令以 `\r\n` 结尾；同一帧内的多条命令按行依次执行。

| 命令 | 功能 |
| --- | --- |
| `forWard` / `backWard` | 前进 / 后退 |
| `lefTurn` / `righTurn` | 左转 / 右转 |
| `letStop` | 停止 |
| `speedUp` / `speedDown` | PWM ±50，范围 50~500 |
| `startTask` / `stopTask` | 使能 / 停止循迹 |
| `STATS` | 输出控制与链路统计 |

### 任务命令

| 命令 | 功能 |
| --- | --- |
| `TASK_START,<task_id>,<from>,<to>[,seq=<n>]` | 创建并启动任务 |
| `STOP,<task_id>,<station_id>[,seq=<n>]` | 目标站点停车 |
| `TASK_CONFIRM,<task_id>,<load\|unload>,<user>[,seq=<n>]` | 装药 / 取药确认 |
| `TASK_PAUSE,<task_id>[,seq=<n>]` | 暂停 |
| `TASK_RESUME,<task_id>[,seq=<n>]` | 恢复 |
| `TASK_CANCEL,<task_id>[,seq=<n>]` | 取消并回到 IDLE |

### ACK 与幂等

所有任务命令返回：

```text
ACK,<task_id>,<0|1>,<reason>,seq=<n>
```

`reason`：`OK / DUP / BUSY / STATE / BADREQ / UNKNOWN`。

- 上位机 500 ms 未收到 ACK 自动重发，最多 3 次。
- 车端缓存最近一次 `cmd + task_id + seq` 的结果 30 s；重复命令直接回放 `DUP`。
- 命令解析使用整词/前缀匹配并校验字段，空字段或缺字段返回 `BADREQ`。

### 上行状态

| 报文 | 说明 |
| --- | --- |
| `ACK,<task_id>,<0|1>,<reason>,seq=<n>` | 下行命令确认 |
| `ENTER_STATION,<task_id>,<seq>` | 检测到进站黑带，请求开启识别窗口 |
| `STATUS,<task_id>,<state>` | 任务状态：NAV/APPROACH/STOPPED/WAIT_LOAD/WAIT_UNLOAD/PAUSED/TIMEOUT/CANCELLED |
| `TASK_ARRIVE,<task_id>,<station_id>[,FALLBACK]` | 已停稳，等待人工确认 |
| `TASK_DONE,<task_id>` | 取药确认完成 |
| `HEARTBEAT,<battery>,<state>` | 1 s 心跳 |

### 视频分片

```text
AA 55 A5 5A | total(2B LE) | offset(2B LE) | len(2B LE) | payload(len B) | CRC16(2B LE)
```

- `payload` 最大 256 B，CRC16-CCITT 覆盖本片数据，上位机按 `offset` 重组。
- 文本行与视频分片共用 TCP 连接；文本队列优先于下一片视频启动。
- 上位机兼容旧格式 `AA55 + len32 + JPEG + CRC16`。

## 触摸屏 UI

| 页面 | 内容 |
| --- | --- |
| 控制 | 装药/取药确认按钮 |
| 数据 | 三路循迹 ADC 原始值、运动状态、PWM 速度 |
| 网络 | WiFi 名称、本机 IP、服务器 IP/端口、在线状态 |

字库为 16×16 中文点阵，覆盖界面所需汉字。

## 可靠性设计

### 看门狗

- IWDG 在 LCD、ADC、USART、摄像头初始化前启动，LSI 约 32 kHz、32 分频、
  重载 1600，超时约 1.6 s。
- 看门狗时基独立于 TIM6：RTOS 使用 `xTaskGetTickCount()`，裸机使用 DWT。
- 控制、WiFi、图传、UI 四个任务分别递增 progress 计数并记录时间戳；控制任务
  每 200 ms 调用 `WDG_CheckAndFeed()`，只有四个任务均在 1.2 s 内前进才喂狗。
- `WDG_GetMissed()` 统计未通过的检查次数，`STATS` 通过 `wdg=` 输出。

### 启动与链路

- WiFi 配网、AT 等待与 TCP 连接均有次数/时间上限。
- 设备离线时进入重连退避，链路恢复后重新建立 TCP 连接。
- 调度器启动前的 AT 等待只更新启动进度；调度器运行后，AT 等待只更新 WiFi 任务进度，
  不直接喂狗。

### 串口与队列

- USART3 接收使用 DMA circular + IDLE 中断 + 环形缓冲，不使用逐字节 RXNE 中断。
- 文本发送使用有界队列，`u3_printf()` 通过 `vsnprintf` 限制长度。
- 视频发送前检查文本队列；DMA 完成中断会立即尝试发送待发文本，降低 ACK 等待。

### 协议与状态机

- `seq` 幂等键覆盖上位机重发，30 s 内回放上次 ACK。
- 进站触发带“离带再武装”，避免停车点仍在黑带上时重复触发。
- 丢线 8 s 停车并上报，不等待 5 min 导航超时。
- 兜底停车仍上报 `TASK_ARRIVE`，上位机可走人工确认流程。

## FreeRTOS 设计

### 内核配置

| 项目 | 值 |
| --- | --- |
| 内核版本 | FreeRTOS Kernel V11.1.0 |
| 编译器 | ARMCC V5.06 update 4 |
| 节拍 | 1000 Hz |
| 堆 | `configTOTAL_HEAP_SIZE = 10 KB`，heap_4 |
| 中断优先级位 | 4 |
| 可调用 FreeRTOS API 的 ISR 下限 | `0x50` |

### 任务间通信

- **WiFi 命令队列**：深度 4、每项 64 B；满时丢弃新命令，避免遥控积压。
- **LCD 互斥量**：带优先级继承；UI 与控制任务的所有 LCD 操作统一加锁。
- **共享状态**：32 位 JPEG 标志由中断写、任务读，Cortex-M4 上原子；ADC 值仅控制任务读写。

### 延时与中断向量

- `delay_us()` 使用 DWT 周期计数器，不占用 SysTick。
- `delay_ms()` 在调度器运行后调用 `vTaskDelay()`；调度器未启动时使用 DWT 忙等。
- SVC/PendSV/SysTick 向量在 `stm32f4xx_it.c` 中用 `B` 跳板转发到 FreeRTOS 移植层，
  保留 `EXC_RETURN` 语义。

### 内存预算

| 占用项 | 大小 |
| --- | ---: |
| 控制任务栈 | 2 KB |
| UI 任务栈 | 1.5 KB |
| WiFi / JPEG 任务栈 | 各 1 KB |
| 空闲任务栈 | 0.5 KB |
| TCB、队列、互斥量 | 约 1 KB |
| 合计 | 约 7 KB，堆内剩余约 3 KB |

### 钩子

- `vApplicationMallocFailedHook()`：堆耗尽时点亮 LED2 并停机。
- `vApplicationStackOverflowHook()`：任务栈溢出时点亮 LED2 并停机。
- LED2 常亮表示堆/栈配置异常，优先检查任务栈与 `configTOTAL_HEAP_SIZE`。

## 构建与烧录

1. 用 Keil µVision 打开 `USER/AIC_MedRobot.uvproj`。
2. 选择芯片 **STM32F407ZGT6**。
3. 在 `RTOS/rtos_conf.h` 设置 `RTOS_ENABLE`。
4. Build 生成 `OBJ/AIC_MedRobot.hex`。
5. 使用 J-Link / ST-Link 烧录。

## 当前限制

- 图传帧率受 USART3 带宽限制；提高帧率需要更高链路带宽或更低 JPEG 体积。
- USART2 为预留初始化，当前未使用。
- 心跳电量暂为固定值 100，尚未接入电池电压采样。
- 站点 ID 为编译期常量，现场调整需要重新编译。
- 上行文本行以 `\r\n` 分帧，尚未增加行级 CRC。
- `STATS` 的分位数需要在台架运行后记录，作为控制抖动基线。

## 相关文档

- 仓库总览与系统架构：`../README.md`
- 测试计划与执行记录：`../TESTING.md`
- 测试实现与扩展方法：`../tests/README.md`
