# FreeRTOS 移植设计说明（RTOS_DESIGN_README）

本文档说明本项目（STM32F407 药品配送小车固件）的 FreeRTOS 移植设计与实现细节。
内核版本：**FreeRTOS Kernel V11.1.0**（FreeRTOSv202406.04-LTS，2024-04 发布），
编译器：**ARMCC V5.06 update 4**，Keil MDK 工程 `USER\Template.uvproj`。

---

## 1. 总开关与条件编译机制

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

## 2. 新增/修改文件清单

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
| `USER\Template.uvproj` | 修改 | 新增 FreeRTOS 组（6 个文件）与 3 条包含路径 |

FreeRTOS 内核源码（仅构建所需文件）位于 `RTOS\FreeRTOS-Kernel\`，**未做任何修改**，
仅通过 `RTOS\FreeRTOSConfig.h` 配置；原 `FreeRTOSv202406.04-LTS\` 整包不再参与构建，可整体移除。

---

## 3. 任务划分与优先级

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

## 4. 裸机主循环如何“原样”变成 RTOS 任务

原 `CAR_LineFollow()` 是一个 `while(1)` 大循环。为兼顾“尽量不改动原有函数”
与“多任务并行”，将其循环体按职责拆成 7 个**步骤函数**（均在 `main.c`），
每个步骤内部的逻辑、顺序、局部变量含义与原循环逐行对应：

```
CAR_StepWifi   ->  WiFi 命令检查（裸机直读串口 / RTOS 从队列取命令）
CAR_StepJpeg   ->  JPEG 帧处理与发送（仅裸机；RTOS 由 vTaskJpeg 承担）
CAR_StepUi     ->  UI_Task（仅裸机；RTOS 由 vTaskUi 承担）
CAR_StepCamera ->  CAMERA_Task + 遮挡检测（含 LCD 互斥）
CAR_StepRange  ->  超声波测距 + 避障逻辑（含 LCD 互斥）
CAR_StepAdc    ->  ADC 采样 + LED 心跳
CAR_StepMotion ->  避障/遮挡/遥控暂停 -> 起步加速 -> 循迹逻辑
```

- 裸机模式：`CAR_LineFollow()` 按原顺序调用全部步骤，行为与原来一致。
- RTOS 模式：`vTaskCarControl` 以 2ms 周期调用除 UI/JPEG 外的步骤；
  UI、JPEG、WiFi 由各自任务并行执行。
- 所有原有驱动接口（`MOTOR_*`、`CAMERA_*`、`UI_*`、`HC_SR04_*`、
  `atk_8266_*`、`jpeg_*`、`ADC_*`）均被原样复用，未修改其内部实现。

---

## 5. 任务间通信与同步

### 5.1 WiFi 命令队列（生产者-消费者）

```c
typedef struct { char cmd[64]; } WiFiCmdMsg;   // RTOS/rtos_app.h
```

- `vTaskWifi`：`atk_8266_at_response_rdCmd(1)` 读到命令后拷贝成 64 字节
  消息 `xQueueSend` 入队（队列深 4，满则丢弃——遥控命令不积压，保证最新
  命令尽快生效）。
- `vTaskCarControl`：每周期 `RTOS_WifiCmdReceive()` 非阻塞出队，调用原
  `handle_wifi_command()` 解析并驱动电机。
- 好处：电机唯一写者；主控任务无需等待串口；命令解析逻辑 100% 复用原函数。

### 5.2 LCD 访问互斥（互斥量，带优先级继承）

UI 任务与主控任务都会写屏（`UI_Task` 与 `UI_SetDistance/UI_SetObstacle/
CAMERA_Task`），为防止两任务交叉写 FSMC 造成显示撕裂，LCD 访问统一经：

```c
RTOS_LcdLock();   // xSemaphoreTake(xLcdMutex, portMAX_DELAY)
...LCD 操作...
RTOS_LcdUnlock(); // xSemaphoreGive(xLcdMutex)
```

裸机模式下 `RTOS_LcdLock/Unlock` 定义为空操作（`rtos_app.h`），调用点统一。

### 5.3 共享状态与原子性

- `jpeg_data_ok / jpeg_data_len / jpeg_last_frame_bytes`：由 DCMI 中断写、
  JPEG 任务读；32 位读写是 Cortex-M4 的原子操作，无需加锁。
- `adcL/adcM/adcR`：仅主控任务读写。
- `UI_GetPage()` 返回的页面号：UI 任务写、主控任务读，单字节原子，允许
  轻微滞后（仅影响画面裁剪判断），可接受。

---

## 6. 延时函数改造（关键点）

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

## 7. 中断向量处理（移植层关键）

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

## 8. 内存分配与预算

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

## 9. 中断优先级配置

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

## 10. 钩子与错误处理

- `vApplicationMallocFailedHook()`：堆耗尽 → 点亮 LED1 并停机。
- `vApplicationStackOverflowHook()`：任务栈溢出（`configCHECK_FOR_STACK_OVERFLOW=2`）
  → 点亮 LED1 并停机。
- 调试建议：若上电后 LED1 常亮，说明堆/栈配置不足，优先检查第 8 节预算。

---

## 11. 已知限制与注意事项

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

## 12. 如何切换与验证

1. 修改 `RTOS\rtos_conf.h` 中 `RTOS_ENABLE` 为 1 或 0。
2. 用 Keil 打开 `USER\Template.uvproj` 重新编译下载。
3. RTOS 模式运行表现：小车行为与裸机一致，但超声波测距的 30ms 等待期间
   UI/图传/WiFi 依然流畅运行；LED1 常亮表示堆/栈异常。