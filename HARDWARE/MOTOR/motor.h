#ifndef __MOTOR_H
#define __MOTOR_H

#include "sys.h"

/* ==================== 电机参数 ==================== */
#define PWM_PERIOD              500

#define MOTOR_RUN_SPEED         300
#define MOTOR_LEFT_RUN_SPEED    300
#define MOTOR_START_SPEED       100
#define MOTOR_LEFT_START_SPEED  100
#define MOTOR_TURN_SPEED        200
#define MOTOR_START_TIME_MS     150

#define LINE_PIVOT_SPEED        150
#define LINE_PIVOT_STEP_MS       20

#define TURN_45_TIME_MS         350
#define TURN_90_TIME_MS         700
#define TURN_180_TIME_MS       1400

#define AVOID_BACK_TIME_MS      700
#define AVOID_STOP_TIME_MS       50

/* ==================== 电机 API ==================== */
void MOTOR_Init(void);
void MOTOR_SetSpeed(u16 left_speed, u16 right_speed);
void MOTOR_Stop(void);

/* 通用方向运动(带速度参数) */
void MOTOR_Forward(u16 speed);
void MOTOR_ForwardDual(u16 left_speed, u16 right_speed);
void MOTOR_Backward(u16 speed);
void MOTOR_TurnLeft(u16 speed);
void MOTOR_TurnRight(u16 speed);

/* 循迹专用 */
void MOTOR_LineForward(void);
void MOTOR_LineTurnLeft(void);
void MOTOR_LineTurnRight(void);
void MOTOR_LineSearchLeft(void);
void MOTOR_LineSearchRight(void);

/* 定时转向 */
void MOTOR_TurnClockwise(u16 time_ms);
void MOTOR_TurnCounterClockwise(u16 time_ms);

/* 避障 */
void CAR_AvoidObstacle(void);

#endif
