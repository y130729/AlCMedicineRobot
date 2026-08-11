# AIC 药品配送机器人（统一仓库）

本仓库以单 git 仓库统一管理药品配送机器人系统的两个关联子项目：

| 子项目 | 目录 | 技术栈 | 说明 |
| --- | --- | --- | --- |
| 车端固件 | `AIC_MedRobot_stm32f407/` | STM32F407 + Keil MDK + FreeRTOS(可选) | 循迹/避障/图传/任务配送状态机 |
| Qt 上位机 | `AICMedicineRobotSystem/` | Qt (C++) | 视频显示、任务管理、quirc 二维码识别 |

## 目录结构

```
AICMedicineRobot/
├── AIC_MedRobot_stm32f407/   # 车端固件（详见其 README.md）
└── AICMedicineRobotSystem/   # Qt 上位机
```

## 构建

- 车端：Keil 打开 `AIC_MedRobot_stm32f407/USER/AIC_MedRobot.uvproj` 编译，产物 `OBJ/AIC_MedRobot.hex`
- 上位机：Qt Creator 打开 `AICMedicineRobotSystem/AICMedicineRobotSystem.pro` 编译

## 通信

上下位机通过 TCP 通信（视频帧 + `\r\n` 文本协议），协议细节见
`AIC_MedRobot_stm32f407/README.md` 的「任务配送（P0 闭环）」章节。

## 仓库约定

- 车端源码编码 GBK/UTF-8 混用（见子项目 README），修改时保持原编码
- 构建产物不入库：`OBJ/`、`build/`、`*.pro.user` 等已被 .gitignore 忽略
