#include "motor.h"
#include "stm32f4xx.h"
#include "delay.h"
#include "pwm.h"

/* ==================== 电机 GPIO 初始化 ==================== */
static void MOTOR_RightIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
	GPIO_ResetBits(GPIOC, GPIO_Pin_4 | GPIO_Pin_5);
}

static void MOTOR_LeftIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
	GPIO_ResetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_3);
}

void MOTOR_Init(void)
{
	TIM13_PWM_Init(PWM_PERIOD - 1, 84 - 1);
	TIM14_PWM_Init(PWM_PERIOD - 1, 84 - 1);
	MOTOR_RightIO_Init();
	MOTOR_LeftIO_Init();
}

/* ==================== 速度与停止 ==================== */
void MOTOR_SetSpeed(u16 left_speed, u16 right_speed)
{
	TIM_SetCompare1(TIM13, left_speed);
	TIM_SetCompare1(TIM14, right_speed);
}

void MOTOR_Stop(void)
{
	MOTOR_SetSpeed(PWM_PERIOD, PWM_PERIOD);
}

/* ==================== 通用方向运动 ==================== */
/* 左轮: PC2/PC3(方向), 右轮: PC4/PC5(方向)
   前进 = PC3+PC4高, PC2+PC5低 */

void MOTOR_Forward(u16 speed)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_4);
	GPIO_ResetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_5);
	MOTOR_SetSpeed(speed, speed);
}

void MOTOR_ForwardDual(u16 left_speed, u16 right_speed)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_4);
	GPIO_ResetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_5);
	MOTOR_SetSpeed(left_speed, right_speed);
}

void MOTOR_Backward(u16 speed)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_5);
	GPIO_ResetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_4);
	MOTOR_SetSpeed(speed, speed);
}

void MOTOR_TurnLeft(u16 speed)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_4);
	GPIO_ResetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_5);
	MOTOR_SetSpeed(speed, speed);
}

void MOTOR_TurnRight(u16 speed)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_5);
	GPIO_ResetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_4);
	MOTOR_SetSpeed(speed, speed);
}

/* ==================== 循迹专用 ==================== */
void MOTOR_LineForward(void)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_4);
	GPIO_ResetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_5);
	MOTOR_SetSpeed(MOTOR_LEFT_RUN_SPEED, MOTOR_RUN_SPEED);
}

void MOTOR_LineTurnLeft(void)
{
	MOTOR_TurnLeft(LINE_PIVOT_SPEED);
	delay_ms(LINE_PIVOT_STEP_MS);
	MOTOR_Stop();
}

void MOTOR_LineTurnRight(void)
{
	MOTOR_TurnRight(LINE_PIVOT_SPEED);
	delay_ms(LINE_PIVOT_STEP_MS);
	MOTOR_Stop();
}

void MOTOR_LineSearchLeft(void)
{
	MOTOR_LineTurnLeft();
}

void MOTOR_LineSearchRight(void)
{
	MOTOR_LineTurnRight();
}

/* ==================== 定时转向 ==================== */
void MOTOR_TurnClockwise(u16 time_ms)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_5);
	GPIO_ResetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_4);
	MOTOR_SetSpeed(MOTOR_TURN_SPEED, MOTOR_TURN_SPEED);
	delay_ms(time_ms);
}

void MOTOR_TurnCounterClockwise(u16 time_ms)
{
	GPIO_SetBits(GPIOC, GPIO_Pin_2 | GPIO_Pin_4);
	GPIO_ResetBits(GPIOC, GPIO_Pin_3 | GPIO_Pin_5);
	MOTOR_SetSpeed(MOTOR_TURN_SPEED, MOTOR_TURN_SPEED);
	delay_ms(time_ms);
}

/* ==================== 避障 ==================== */
void CAR_AvoidObstacle(void)
{
	static u8 turn_right = 1;

	MOTOR_Stop();
	delay_ms(AVOID_STOP_TIME_MS);
	MOTOR_Backward(MOTOR_LEFT_START_SPEED);
	delay_ms(AVOID_BACK_TIME_MS);
	MOTOR_Stop();
	delay_ms(AVOID_STOP_TIME_MS);

	if (turn_right)
		MOTOR_TurnClockwise(TURN_90_TIME_MS);
	else
		MOTOR_TurnCounterClockwise(TURN_90_TIME_MS);

	turn_right = !turn_right;
}
