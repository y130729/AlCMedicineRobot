# P0 任务配送闭环设计方案（录入任务 -> 配送到目的地）

> 文档版本：v1.0 | 适用项目：AIC_MedRobot_stm32f407 + AICMedicineRobotSystem | 状态：设计稿，待实施

## 1. 方案概述与架构原则

**目标**：打通「上位机录入任务 -> 小车导航到取药点 -> 装药确认 -> 导航到病房 -> 取药确认 -> 任务完成」全链路。

**三项核心决策**：

1. **报文极简**：任务报文只下发 `task_id + 取药点站点ID + 目的地站点ID`。药品清单留在上位机本地（用于交接核对、弹窗展示、日志审计），与车端执行无关，不下发。
2. **识别在上位机**：车端不移植图像解码库。上位机将 quirc（纯 C、无依赖）编入 Qt 工程，复用现有 JPEG 视频流识别站点二维码。
3. **停车判断在上位机，执行与兜底在车端**：上位机识别到目标站点后下发 `STOP` 命令，车端执行停车；车端用循迹线编码带触发进站降速，链路异常时按最远距离兜底停车。安全不依赖 WiFi。

**职责划分**：

- 上位机：「看和管」——二维码识别、任务队列管理、装/卸药确认、状态刷新、日志。
- 车端：「跑和停」——循迹导航、进站降速、执行停车、异常兜底、触摸确认。

## 2. 系统数据流

```
[车端] OV2640 --JPEG帧(AA55+len+CRC)--> [上位机] 显示 + quirc识别
        ^                                      |
        |  ENTER_STATION(进识别区)              | STOP(目标站点命中)
        v                                      v
  编码带触发->降速10cm/s --STATUS/ARRIVE/DONE--> 任务队列/弹窗/日志
        ^
        | TASK_START / TASK_CONFIRM / TASK_PAUSE / TASK_RESUME / TASK_CANCEL
        +--------------- TCP 8086 下行命令 ---------------+
```

- 上行：视频帧 + 文本状态；下行：任务控制文本。方向天然分离，沿用现有 `\r\n` 拆分机制（`wifi_dispatch_commands`）。
- 可靠性：所有下行任务命令必须回 `ACK`，上位机 500ms 未收到重发（最多 3 次）。

## 3. 协议设计

### 3.1 下行命令（上位机 -> 车），一律 `\r\n` 结尾

| 命令 | 格式 | 说明 |
| --- | --- | --- |
| 下发任务 | `TASK_START,<task_id>,<from_station>,<to_station>` | 如 `TASK_START,T-2025,S1,S3` |
| 目标站停车 | `STOP,<task_id>,<station_id>` | 上位机识别到目标站点后立即发送 |
| 装/卸确认 | `TASK_CONFIRM,<task_id>,<load\|unload>,<user>` | 人工确认后放行 |
| 暂停任务 | `TASK_PAUSE,<task_id>` | 挂起当前任务 |
| 恢复任务 | `TASK_RESUME,<task_id>` | 恢复执行 |
| 取消任务 | `TASK_CANCEL,<task_id>` | 终止任务回 IDLE |

### 3.2 上行报文（车 -> 上位机）

| 报文 | 格式 | 说明 |
| --- | --- | --- |
| 命令确认 | `ACK,<task_id>,<0\|1>` | 收到命令即回；1=成功 0=拒绝 |
| 进识别区 | `ENTER_STATION,<task_id>,<seq>` | 编码带触发，请开启识别窗口 |
| 状态上报 | `STATUS,<task_id>,<state>` | state: `NAV/APPROACH/STOPPED/WAIT_LOAD/WAIT_UNLOAD/PAUSED/TIMEOUT` |
| 到达停车 | `TASK_ARRIVE,<task_id>,<station_id>` | 已停稳，等待确认 |
| 任务完成 | `TASK_DONE,<task_id>` | 卸药确认后发送 |
| 心跳 | `HEARTBEAT,<battery>,<state>` | 每 1s 一次，更新电量/在线状态 |

### 3.3 站点表（上下位机一致）

| 站点ID | 名称 |
| --- | --- |
| S1 | 药房01 |
| S2 | 病房001 |
| S3 | 病房002 |
| S4 | 车库01 |

- 与上位机 `taskdialog` 的下拉项一一对应；先以编译期常量维护，后续可存入 24CXX EEPROM。

## 4. 车端设计

改动集中在 `USER\car_app.c`、`USER\wifi_cmd.c`、`HARDWARE\UI\ui.c`。

### 4.1 进站触发与降速（纯软件，零新增硬件）

- 站点前约 30cm 的循迹线上贴**三路全黑横带**（3~4cm 宽）。
- 检测：`ADC_ReadLineSensors()` 每 2ms 采样一次（RTOS 主控任务周期），**左中右三路同时 >= LINE_BLACK_THRESHOLD 且连续 >=10 个采样周期（约 20ms）** 判定 `station_trigger`。
- 不会误触发：正常循迹只有中路黑、转弯最多两路黑；三路全黑且持续 20ms 是横带独有模式。
- 触发后动作：降速至 `pwmval ~= 200`（约 10~15cm/s）-> 上报 `ENTER_STATION` -> 进入等待 `STOP` 状态。

### 4.2 停车执行与兜底

```
t0  触发带命中 -> 降速 + 上报 ENTER_STATION
t1  收到 STOP,<task_id>,<station_id>（校验 task_id 且站点匹配目标）-> MOTOR_Stop()
    -> 上报 STATUS,STOPPED -> TASK_ARRIVE
兜底：t0 后 2000ms（或约 30cm）未收到 STOP -> 强制 MOTOR_Stop()
    -> 上报 STATUS,STATION_UNCONFIRMED -> 停车等待人工
```

- 只有**目标站点**的 `STOP` 才执行停车，路径上经过的其他站点继续走。
- 链路断开 / 上位机卡死 / 识别失败，小车都会在站点区内停住，不会冲出。

### 4.3 车端任务状态机（新增 `CAR_StepTask`，与运动状态机分层）

| 状态 | 进入 | 离开 |
| --- | --- | --- |
| `TASK_IDLE` | 上电 / 任务完成 | 收到 `TASK_START` |
| `TASK_NAV_TO_LOAD` | 任务启动，`line_enabled=1` 循迹 | 触发带命中 |
| `TASK_APPROACH_LOAD` | 降速等待 `STOP` | `STOP` 命中 -> 停车 |
| `TASK_WAIT_LOAD` | 停稳（`STATUS=STOPPED`） | `TASK_CONFIRM,load` |
| `TASK_NAV_TO_WARD` | 装药确认 | 触发带命中 |
| `TASK_APPROACH_WARD` | 降速等待 `STOP` | `STOP` 命中 -> 停车 |
| `TASK_WAIT_UNLOAD` | 停稳（上报 `TASK_ARRIVE`） | `TASK_CONFIRM,unload` |
| `TASK_PAUSED` | `TASK_PAUSE` / 异常 | `TASK_RESUME` |
| `TASK_ABORTED` | `TASK_CANCEL` / 超时 | 复位回 `TASK_IDLE` |

兜底规则：

- 每个 `TASK_NAV_*` 最长循迹 5 分钟超时 -> `TASK_ABORTED`；
- 装/卸确认 5 分钟超时 -> 蜂鸣 + 上位机告警，任务保持等待不自动完成；
- 遥控 `wifi_override` 与任务状态并存：遥控期间任务挂起，遥控结束恢复循迹。

### 4.4 命令解析扩展（`wifi_cmd.c`）

- 新增分支处理 `TASK_START / STOP / TASK_CONFIRM / TASK_PAUSE / TASK_RESUME / TASK_CANCEL`，统一调用 `CAR_OnTaskCommand()` 接口；
- 原有遥控命令（`forWard/backWard/lefTurn/righTurn/letStop/speedUp/speedDown/startTask/stopTask`）原样保留。

### 4.5 车端确认按钮（`HARDWARE\UI\ui.c`）

- 触摸界面新增「已装药 / 已取药」确认页，复用现有 `UI_Key` 事件系统；
- 按下后发送 `TASK_CONFIRM,<task_id>,<load|unload>,<user>`，不依赖上位机也能完成交接闭环。

## 5. 上位机设计

改动集中在 `mainwindow.cpp`、`taskdialog.cpp`，新增识别模块。

### 5.1 二维码识别模块（quirc）

- 将 quirc 源码加入 `AICMedicineRobotSystem.pro`（纯 C、无依赖，Windows/Linux 双平台可编译）；所需文件可以上网下载
- 流程：收到 `ENTER_STATION` -> 开启 **2s 识别窗口** -> 对窗口内每帧 JPEG（`QPixmap -> QImage -> 灰度`）尝试解码 -> 命中目标站点即发 `STOP` 并关闭窗口；
- 窗口超时未命中：静默重开一轮，再失败则提示「站点未确认」（车端此时已兜底停车）；
- 去重：同一站点连续命中只发一次；非目标站点命中只记日志不动作。

### 5.2 二维码规格

- 内容：`S1/S2...` 加 1 字节校验位；
- 贴纸 3~5cm，识别区做 20~30cm 长（同 ID 多贴或长条）；
- 车速 10cm/s 时，0.5~1s 延迟窗口内车只走 5~10cm，稳定落在识别区内。

### 5.3 协议解析与任务管理（`mainwindow.cpp`）

- `readVideoMessage()` 增加文本行解析：`ACK / ENTER_STATION / STATUS / TASK_ARRIVE / TASK_DONE / HEARTBEAT`，先判文本前缀再走视频帧逻辑；
- 任务队列绑定 `task_id` 的真实状态（待下发 / 执行中 / 已到达 / 已完成 / 已取消），由 `STATUS` 驱动刷新；
- 暂停/取消按钮下发带 `task_id` 的报文（替代现有笼统的 `stopTask`）；
- `TASK_ARRIVE` -> 弹确认框，展示该任务药品清单（取自上位机本地记录）：「病房002 已到达，请核对取药：阿莫西林胶囊x2盒...」-> 确认发 `TASK_CONFIRM,unload`；
- `HEARTBEAT` -> 更新电量条（替换写死的 78）与在线/离线告警。

### 5.4 任务下发（`taskdialog.cpp` / `mainwindow.cpp`）

- 录入弹窗：取药点、目的地下拉映射为站点 ID，生成 `task_id`（延续 `T-2025` 编号），药品清单存入上位机任务记录；
- 下发：拼 `TASK_START` -> 等 `ACK`，500ms 重发最多 3 次，失败置「下发失败」并提示。

## 6. 完整配送时序示例（药房01 -> 病房002）

```
上位机                 TCP/ESP8266                 车端
  | TASK_START,T-2025,S1,S3 --------------> 回 ACK,T-2025,1
  | <----------- ACK -------  TASK_NAV_TO_LOAD 循迹出发
  |                           触发带命中 -> 降速
  | <------- ENTER_STATION --- 开启识别窗口
  | 识别到 S1（目标取药点）-> STOP,T-2025,S1
  | ------------- STOP ------> 停车，STATUS,STOPPED
  | <------ STATUS,WAIT_LOAD - 弹窗「药房01到，请装药」
  | TASK_CONFIRM,T-2025,load,护士A ----> 出发 TASK_NAV_TO_WARD
  | <------- ENTER_STATION --- 开启识别窗口
  | 识别到 S3（目标病房）-> STOP,T-2025,S3 ----> 停车
  | <--- TASK_ARRIVE,T-2025,S3 - 弹窗「病房002到，请取药」（含药品清单）
  | TASK_CONFIRM,T-2025,unload,护士B ----> TASK_DONE,T-2025
  | <------------ TASK_DONE --- 任务完成，队列移除，车回 IDLE
```

## 7. 实施里程碑

| 阶段 | 内容 | 验证点 |
| --- | --- | --- |
| M1 协议握手 | `TASK_START` 下发 + `ACK` + 周期 `STATUS` + 重发机制 | 日志见完整握手、断线重发 |
| M2 车端跑通 | 触发带检测、降速、STOP 停车、任务状态机（识别暂用命令模拟确认） | 全链路跑通但未接识别 |
| M3 上位机识别 | quirc 接入、识别窗口、`ENTER_STATION`/`STOP` 闭环 | 实车到站自动停车，实测贴纸/车速/距离 |
| M4 收尾 | 装/卸确认弹窗、心跳电量、超时与异常分支、断线提示 | 完整演示脚本可复现 |

## 8. 风险与对策

| 风险 | 对策 |
| --- | --- |
| 触发带误判 | 三路全黑 + 连续 20ms 双重过滤 |
| 上位机识别不到 | 降速进站 + 20~30cm 长识别区 + 窗口重试一轮 + 车端兜底停车 |
| 停车冲过站 | 进站前降速至 10cm/s，延迟窗口内位移 5~10cm，远小于识别区 |
| 链路断开 | 车端 2s 兜底强制停车 + 心跳离线告警（断线自动重连列入 P1） |
| 经过非目标站点 | 只有目标站点才发 `STOP`，其他站只记日志 |
| 命令与视频同连接 | 文本先判前缀再进帧解析，沿用 `\r\n` 拆分 |

