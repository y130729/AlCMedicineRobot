/* ==================== 小车应用层 ====================
 * 由原 main.c 拆分而来: 循迹/避障/遥控/图传的 7 个步骤函数,
 * 裸机主循环 (CAR_LineFollow) 与 RTOS 主控任务 (vTaskCarControl)
 * 共用同一套步骤函数。
 * WiFi 命令解析见 wifi_cmd.c。 */
#include "stm32f4xx.h"
#include <string.h>
#include "led.h"
#include "exti.h"
#include "timer.h"
#include "adc.h"
#include "ui.h"
#include "camera.h"
#include "common.h"
#include "dcmi.h"
#include "wifi_transfer.h"
#include "motor.h"
#include "rtos_app.h"
#include "wifi_cmd.h"
#include "car_app.h"
#include "usart3.h"
#include "wifista.h"

/* ==================== 循迹/避障参数 ==================== */
#define RANGE_SAMPLE_PERIOD_MS  100
#define ADC_SAMPLE_PERIOD_MS    250
#define OBSTACLE_DISTANCE_CM   25.0f
#define OBSTACLE_CLEAR_CM      28.0f

/* 黑线ADC值高于白色区域 */
#define LINE_BLACK_IS_HIGH          1
#define LINE_BLACK_THRESHOLD     2000
#define LINE_CENTER_TOLERANCE       0
#define LINE_FORWARD_STEP_MS      500
#define LINE_SAMPLE_TIMES           2

/* line lost: stop the car and report instead of searching for 5 minutes */
#define LINE_LOST_TIMEOUT_MS     8000


/* ==================== ADC/LED 杈呭姪 ==================== */
u16 adcL = 0;
u16 adcM = 0;
u16 adcR = 0;

/* ==================== 运动状态(供UI数据页显示) ==================== */
typedef enum
{
	CAR_STATE_STOP = 0,
	CAR_STATE_LINE_LOST,         /* line lost -> stopped */          /* 离线/无方向停止 */
	CAR_STATE_STANDBY,           /* 未使能循迹 */
	CAR_STATE_STARTUP,           /* 起步加速 */
	CAR_STATE_FORWARD,           /* 循迹直行 */
	CAR_STATE_TURN_LEFT,         /* 左转 */
	CAR_STATE_TURN_RIGHT,        /* 右转 */
	CAR_STATE_SEARCH_LEFT,       /* 向左搜索 */
	CAR_STATE_SEARCH_RIGHT,      /* 向右搜索 */
	CAR_STATE_AVOID_STOP,        /* 避障停车 */
	CAR_STATE_OCCLUSION_STOP,    /* 遮挡停车 */
	CAR_STATE_REMOTE             /* 遥控覆盖 */
} CAR_State;

static u8 g_car_state = CAR_STATE_STOP;
static void ADC_ReadLineSensors(void)
{
	adcL = Get_Adc_Average(ADC_Channel_5, LINE_SAMPLE_TIMES);
	adcM = Get_Adc_Average(ADC_Channel_4, LINE_SAMPLE_TIMES);
	adcR = Get_Adc_Average(ADC_Channel_15, LINE_SAMPLE_TIMES);
}

static u8 LINE_IsBlack(u16 adc_value)
{
#if LINE_BLACK_IS_HIGH
	return adc_value >= LINE_BLACK_THRESHOLD;
#else
	return adc_value <= LINE_BLACK_THRESHOLD;
#endif
}


/* 循迹/避障/遥控共享状态: 裸机与RTOS两种模式共用同一组步骤函数 */
typedef struct
{
	float distance;
	u32 now_ms;
	u32 next_range_ms;
	u32 next_lcd_ms;
	u32 startup_end_ms;
	u8  obstacle_active;
	u8  camera_occlusion_active;
	s8  last_direction;
	u16 pwmval;
	u8  line_enabled;
	u8  wifi_override;
	u32 wifi_override_end;
	u8  line_lost;          /* 1 = line-lost timeout latched (no task)   */
	u32 line_lost_ms;       /* 0 = on line, else time of first all-white */
} CAR_Ctx;

/* ==================== 配送任务状态机(P0 任务配送闭环) ==================== */
/* 协议见 P0_task_delivery_design.md:
 * 下行 TASK_START/STOP/TASK_CONFIRM/TASK_PAUSE/TASK_RESUME/TASK_CANCEL,
 * 上行 ACK/ENTER_STATION/STATUS/TASK_ARRIVE/TASK_DONE/HEARTBEAT。
 * 状态机与循迹运动分层: 本模块只负责任务流转/进站触发/停车兜底/超时,
 * 具体电机动作仍由 CAR_StepMotion 按状态执行。 */
#define TASK_APPROACH_SPEED     200     /* 进站降速 PWM(约10~15cm/s) */
#define TASK_TRIGGER_SAMPLES    10      /* 三路全黑连续计数(2ms/次约20ms) */
#define TASK_STOP_FALLBACK_MS   2000    /* ENTER_STATION 后等待 STOP 上限(兜底停车) */
#define TASK_NAV_TIMEOUT_MS     300000  /* 每段循迹 5 分钟超时 */
#define TASK_CONFIRM_TIMEOUT_MS 300000  /* 装/卸确认 5 分钟超时(LED2 告警) */
#define TASK_HEARTBEAT_MS       1000    /* 心跳周期 */
#define TASK_BATTERY_DEFAULT    100     /* 电量暂以固定值上报, 预留电池采样扩展 */

typedef enum
{
	TASK_IDLE = 0,
	TASK_LINE_LOST,        /* line lost: stopped, waiting for host */
	TASK_NAV_TO_LOAD,      /* 前往取药点 */
	TASK_APPROACH_LOAD,    /* 取药点进站缓行 */
	TASK_WAIT_LOAD,        /* 等待装药确认 */
	TASK_NAV_TO_WARD,      /* 前往病房 */
	TASK_APPROACH_WARD,    /* 病房进站缓行 */
	TASK_WAIT_UNLOAD,      /* 等待取药确认 */
	TASK_PAUSED,           /* 任务挂起(遥控/手动暂停) */
	TASK_ABORTED           /* 循迹超时中止 */
} CAR_TaskState;

typedef struct
{
	u8  state;              /* CAR_TaskState */
	u8  paused_state;       /* 暂停前状态, 恢复时使用 */
	char task_id[16];       /* 任务编号 */
	char from_station[4];   /* 取药点站点ID S1~S4 */
	char to_station[4];     /* 目的地站点ID S1~S4 */
	u8  seq;                /* 0=取药点, 1=病房 */
	u8  waiting_stop;       /* 已上报 ENTER_STATION, 等待 STOP */
	u8  trigger_cnt;        /* 三路全黑连续采样计数 */
	u32 enter_ms;           /* 进站触发时刻(兜底计时) */
	u32 nav_start_ms;       /* 当前循迹段开始时刻(5min 超时) */
	u32 wait_start_ms;      /* 等待确认开始时刻(5min 超时) */
	u32 alert_ms;           /* LED2 告警闪烁时刻 */
	u32 last_heartbeat_ms;
	u8  armed;              /* station trigger re-arm (see CAR_TaskEnterStation) */  /* 上次心跳时刻 */
	u32 line_lost_ms;	/* 0 = on line, else timestamp of first all-white */
} CAR_TaskCtx;

static CAR_TaskCtx g_task;/* 任务状态机全局(静态零初始化 = TASK_IDLE) */

/* 任务状态 -> 协议状态文本 */
static const char *CAR_TaskStateText(u8 state)
{
	switch (state)
	{
	case TASK_NAV_TO_LOAD:   return "NAV";
	case TASK_NAV_TO_WARD:   return "NAV";
	case TASK_APPROACH_LOAD: return "APPROACH";
	case TASK_APPROACH_WARD: return "APPROACH";
	case TASK_WAIT_LOAD:     return "WAIT_LOAD";
	case TASK_WAIT_UNLOAD:   return "WAIT_UNLOAD";
	case TASK_PAUSED:        return "PAUSED";
	case TASK_LINE_LOST:     return "LINE_LOST";
	case TASK_ABORTED:       return "TIMEOUT";
	default:                 return "IDLE";
	}
}

/* 从逗号分隔参数中取第 idx 段(0 起)复制到 out; 成功返回 0 */
static u8 CAR_TaskParseField(char *args, u8 idx, char *out, u8 maxlen)
{
	char *p;
	char *q;
	u8 i;
	u16 len;

	if (args == 0 || out == 0 || maxlen == 0)
		return 1;
	p = args;
	for (i = 0; i < idx; i++)
	{
		p = strchr(p, ',');
		if (p == 0)
			return 1;
		p++;
	}
	q = strchr(p, ',');
	if (q != 0)
		len = (u16)(q - p);
	else
		len = (u16)strlen(p);
	if (len == 0)
		return 1;/* empty field = malformed command */
	if (len >= maxlen)
		len = maxlen - 1;
	memcpy(out, p, len);
	out[len] = 0;
	return 0;
}

/* 当前目标站点(seq=0 取药点, seq=1 病房) */
static const char *CAR_TaskTargetStation(void)
{
	if (g_task.seq == 0)
		return g_task.from_station;
	return g_task.to_station;
}

/* 进站触发: 降速 + 上报 ENTER_STATION + 开启 STOP 兜底计时 */
static void CAR_TaskEnterStation(void)
{
	g_task.trigger_cnt = TASK_TRIGGER_SAMPLES;
	g_task.armed = 0;/* re-arm only after the band is cleared */
	g_task.waiting_stop = 1;
	g_task.enter_ms = EXTIX_GetMillis();
	if (g_task.seq == 0)
		g_task.state = TASK_APPROACH_LOAD;
	else
		g_task.state = TASK_APPROACH_WARD;
	u3_printf("ENTER_STATION,%s,%d\r\n", g_task.task_id, g_task.seq);
	u3_printf("STATUS,%s,APPROACH\r\n", g_task.task_id);
}

/* 目标站停车完成 -> 等待人工确认 */
static void CAR_TaskStopAtStation(const char *station_id)
{
	MOTOR_Stop();
	g_task.waiting_stop = 0;
	u3_printf("STATUS,%s,STOPPED\r\n", g_task.task_id);
	u3_printf("TASK_ARRIVE,%s,%s\r\n", g_task.task_id, station_id);
	if (g_task.seq == 0)
	{
		g_task.state = TASK_WAIT_LOAD;
		g_task.wait_start_ms = EXTIX_GetMillis();
	}
	else
	{
		g_task.state = TASK_WAIT_UNLOAD;
		g_task.wait_start_ms = EXTIX_GetMillis();
	}
}

/* 循迹段超时中止 */
/* Line lost: stop and report; stay stopped until the host intervenes. */
static void CAR_TaskLineLost(void)
{
	MOTOR_Stop();
	g_task.waiting_stop = 0;
	g_task.state = TASK_LINE_LOST;
	u3_printf("STATUS,%s,LINE_LOST\r\n", g_task.task_id);
}

static void CAR_TaskAbort(void)
{
	MOTOR_Stop();
	g_task.waiting_stop = 0;
	g_task.state = TASK_ABORTED;
	u3_printf("STATUS,%s,TIMEOUT\r\n", g_task.task_id);
}

/* 上位机任务命令入口(wifi_cmd.c 解析命令前缀后调用) */
void CAR_OnTaskCommand(u8 cmd, char *args, u8 *line_enabled)
{
	char task_id[16];
	char field_a[8];
	char field_b[8];
	u8 ok = 0;
	const char *expect;

	if (args == 0 || line_enabled == 0)
		return;
	task_id[0] = 0;
	field_a[0] = 0;
	field_b[0] = 0;

	switch (cmd)
	{
	case CAR_CMD_TASK_START:
		if (CAR_TaskParseField(args, 0, task_id, sizeof(task_id)) == 0 &&
		    CAR_TaskParseField(args, 1, field_a, sizeof(field_a)) == 0 &&
		    CAR_TaskParseField(args, 2, field_b, sizeof(field_b)) == 0)
		{
			if (g_task.state == TASK_IDLE)
			{
				strncpy(g_task.task_id, task_id, sizeof(g_task.task_id) - 1);
				g_task.task_id[sizeof(g_task.task_id) - 1] = 0;
				strncpy(g_task.from_station, field_a, sizeof(g_task.from_station) - 1);
				g_task.from_station[sizeof(g_task.from_station) - 1] = 0;
				strncpy(g_task.to_station, field_b, sizeof(g_task.to_station) - 1);
				g_task.to_station[sizeof(g_task.to_station) - 1] = 0;
				g_task.seq = 0;
				g_task.waiting_stop = 0;
				g_task.trigger_cnt = 0;
				g_task.line_lost_ms = 0;
				g_task.armed = 0;
				g_task.state = TASK_NAV_TO_LOAD;
				g_task.nav_start_ms = EXTIX_GetMillis();
				*line_enabled = 1;
				ok = 1;
				u3_printf("STATUS,%s,NAV\r\n", g_task.task_id);
			}
		}
		break;

	case CAR_CMD_STOP:
		if (CAR_TaskParseField(args, 0, task_id, sizeof(task_id)) == 0 &&
		    CAR_TaskParseField(args, 1, field_a, sizeof(field_a)) == 0)
		{
			expect = CAR_TaskTargetStation();
			if (g_task.waiting_stop &&
			    strcmp(task_id, g_task.task_id) == 0 &&
			    expect != 0 && strcmp(field_a, expect) == 0)
			{
				ok = 1;
				CAR_TaskStopAtStation(field_a);
			}
		}
		break;

	case CAR_CMD_TASK_CONFIRM:
		if (CAR_TaskParseField(args, 0, task_id, sizeof(task_id)) == 0 &&
		    CAR_TaskParseField(args, 1, field_a, sizeof(field_a)) == 0)
		{
			if (strcmp(task_id, g_task.task_id) == 0)
			{
				if (g_task.state == TASK_WAIT_LOAD && strcmp(field_a, "load") == 0)
				{
					ok = 1;
					g_task.seq = 1;
					g_task.waiting_stop = 0;
					g_task.trigger_cnt = 0;
					g_task.line_lost_ms = 0;
					g_task.armed = 0;
					g_task.state = TASK_NAV_TO_WARD;
					g_task.nav_start_ms = EXTIX_GetMillis();
					*line_enabled = 1;
					u3_printf("STATUS,%s,NAV\r\n", g_task.task_id);
				}
				else if (g_task.state == TASK_WAIT_UNLOAD && strcmp(field_a, "unload") == 0)
				{
					ok = 1;
					u3_printf("TASK_DONE,%s\r\n", g_task.task_id);
					g_task.state = TASK_IDLE;
					g_task.task_id[0] = 0;
					*line_enabled = 0;
				}
			}
		}
		break;

	case CAR_CMD_TASK_PAUSE:
		if (CAR_TaskParseField(args, 0, task_id, sizeof(task_id)) == 0 &&
		    strcmp(task_id, g_task.task_id) == 0)
		{
			if (g_task.state == TASK_NAV_TO_LOAD || g_task.state == TASK_NAV_TO_WARD ||
			    g_task.state == TASK_APPROACH_LOAD || g_task.state == TASK_APPROACH_WARD)
			{
				ok = 1;
				g_task.paused_state = g_task.state;
				g_task.state = TASK_PAUSED;
				MOTOR_Stop();
				u3_printf("STATUS,%s,PAUSED\r\n", g_task.task_id);
			}
		}
		break;

	case CAR_CMD_TASK_RESUME:
		if (CAR_TaskParseField(args, 0, task_id, sizeof(task_id)) == 0 &&
		    strcmp(task_id, g_task.task_id) == 0)
		{
			if (g_task.state == TASK_PAUSED)
			{
				ok = 1;
				g_task.state = g_task.paused_state;
				g_task.nav_start_ms = EXTIX_GetMillis();
				/* 若在进站缓行中被暂停, 恢复后重新开始兜底计时 */
				if (g_task.state == TASK_APPROACH_LOAD || g_task.state == TASK_APPROACH_WARD)
					g_task.enter_ms = EXTIX_GetMillis();
				*line_enabled = 1;
				u3_printf("STATUS,%s,NAV\r\n", g_task.task_id);
			}
		}
		break;

	case CAR_CMD_TASK_CANCEL:
		if (CAR_TaskParseField(args, 0, task_id, sizeof(task_id)) == 0 &&
		    strcmp(task_id, g_task.task_id) == 0)
		{
			ok = 1;
			MOTOR_Stop();
			u3_printf("STATUS,%s,CANCELLED\r\n", task_id);
			g_task.state = TASK_IDLE;
			g_task.task_id[0] = 0;
			*line_enabled = 0;
		}
		break;

	default:
		break;
	}

	/* 命令确认: 1=成功 0=拒绝(参数不合法/任务状态不允许) */
	if (ok)
		u3_printf("ACK,%s,1\r\n", task_id);
	else if (task_id[0] != 0)
		u3_printf("ACK,%s,0\r\n", task_id);
}

/* 任务处于等待/暂停/中止: 强制停车 */
static u8 CAR_TaskHolding(void)
{
	return (g_task.state == TASK_WAIT_LOAD || g_task.state == TASK_WAIT_UNLOAD ||
	        g_task.state == TASK_PAUSED || g_task.state == TASK_ABORTED ||
	        g_task.state == TASK_LINE_LOST);
}

/* 任务处于进站缓行: 循迹降速 */
static u8 CAR_TaskApproaching(void)
{
	return (g_task.state == TASK_APPROACH_LOAD || g_task.state == TASK_APPROACH_WARD);
}

/* 任务状态机步进: 心跳上报 + 进站触发 + 兜底停车 + 超时处理 */
static void CAR_StepTask(CAR_Ctx *ctx)
{
	u32 now = EXTIX_GetMillis();
	/* 车端触摸确认: 同步按钮状态(等待装/卸确认时有效), 轮询触摸请求, 等效 TASK_CONFIRM */
	{
		u8 confirm = UI_CONFIRM_NONE;
		u8 req;

		if (g_task.state == TASK_WAIT_LOAD)
			confirm = UI_CONFIRM_LOAD;
		else if (g_task.state == TASK_WAIT_UNLOAD)
			confirm = UI_CONFIRM_UNLOAD;
		RTOS_LcdLock();
		UI_SetTaskConfirm(confirm);
		RTOS_LcdUnlock();

		req = UI_GetConfirmRequested();
		if (req != UI_CONFIRM_NONE)
		{
			UI_ClearConfirmRequest();
			if (req == UI_CONFIRM_LOAD && g_task.state == TASK_WAIT_LOAD)
			{
				/* 装药确认: 出发前往病房 */
				g_task.seq = 1;
				g_task.waiting_stop = 0;
				g_task.trigger_cnt = 0;
				g_task.state = TASK_NAV_TO_WARD;
				g_task.nav_start_ms = EXTIX_GetMillis();
				ctx->line_enabled = 1;
				u3_printf("STATUS,%s,NAV\r\n", g_task.task_id);
			}
			else if (req == UI_CONFIRM_UNLOAD && g_task.state == TASK_WAIT_UNLOAD)
			{
				/* 取药确认: 任务完成回 IDLE */
				u3_printf("TASK_DONE,%s\r\n", g_task.task_id);
				g_task.state = TASK_IDLE;
				g_task.task_id[0] = 0;
				ctx->line_enabled = 0;
			}
		}
	}

	/* 心跳: 每 1s 上报电量与任务状态(上位机据此刷新电量/在线) */
	if ((s32)(now - g_task.last_heartbeat_ms) >= TASK_HEARTBEAT_MS)
	{
		g_task.last_heartbeat_ms = now;
		u3_printf("HEARTBEAT,%d,%s\r\n", TASK_BATTERY_DEFAULT,
		          CAR_TaskStateText(g_task.state));
	}

	switch (g_task.state)
	{
	case TASK_NAV_TO_LOAD:
	case TASK_NAV_TO_WARD:
		ctx->line_enabled = 1;/* 任务期间强制循迹使能 */
		/* re-arm the trigger once the station band is behind the sensors */
		if (!LINE_IsBlack(adcL) || !LINE_IsBlack(adcM) || !LINE_IsBlack(adcR))
			g_task.armed = 1;
		if (!g_task.waiting_stop && g_task.armed)
		{
			/* 进站触发: 左中右三路全黑且连续 >=10 个采样周期(约 20ms) */
			if (LINE_IsBlack(adcL) && LINE_IsBlack(adcM) && LINE_IsBlack(adcR))
			{
				if (++g_task.trigger_cnt >= TASK_TRIGGER_SAMPLES)
					CAR_TaskEnterStation();
			}
			else
				g_task.trigger_cnt = 0;
		}
		/* 循迹段 5 分钟超时 -> 中止 */
		/* line lost: all-white for too long -> stop and report */
		if (!LINE_IsBlack(adcL) && !LINE_IsBlack(adcM) && !LINE_IsBlack(adcR))
		{
			if (g_task.line_lost_ms == 0)
				g_task.line_lost_ms = now;
			else if ((s32)(now - g_task.line_lost_ms) >= LINE_LOST_TIMEOUT_MS)
				CAR_TaskLineLost();
		}
		else
			g_task.line_lost_ms = 0;
		if ((s32)(now - g_task.nav_start_ms) >= TASK_NAV_TIMEOUT_MS)
			CAR_TaskAbort();
		break;

	case TASK_APPROACH_LOAD:
	case TASK_APPROACH_WARD:
		ctx->line_enabled = 1;
		/* 兜底停车: 进站 2s 内未收到目标站 STOP -> 强制停车等待人工 */
		if ((s32)(now - g_task.enter_ms) >= TASK_STOP_FALLBACK_MS)
		{
			MOTOR_Stop();
			g_task.waiting_stop = 0;
			if (g_task.seq == 0)
			{
				g_task.state = TASK_WAIT_LOAD;
				g_task.wait_start_ms = now;
			}
			else
			{
				g_task.state = TASK_WAIT_UNLOAD;
				g_task.wait_start_ms = now;
			}
			/* 补发到达信号(带 FALLBACK 标记), 上位机弹窗可走人工确认 */
			u3_printf("TASK_ARRIVE,%s,%s,FALLBACK\r\n",
				g_task.task_id, CAR_TaskTargetStation());
			u3_printf("STATUS,%s,TIMEOUT\r\n", g_task.task_id);
		}
		break;

	case TASK_WAIT_LOAD:
	case TASK_WAIT_UNLOAD:
		/* 确认超时: LED2 闪烁告警(蜂鸣器 PF8 已被 TIM13 PWM 占用, 改用指示灯) */
		if ((s32)(now - g_task.wait_start_ms) >= TASK_CONFIRM_TIMEOUT_MS)
		{
			if ((s32)(now - g_task.alert_ms) >= 250)
			{
				g_task.alert_ms = now;
				LED2 = !LED2;
			}
		}
		break;

	default:
		break;
	}
}

/* ==================== 循迹主循环(整合WiFi/JPEG) ==================== */



static void CAR_CtxInit(CAR_Ctx *ctx)
{
	ctx->distance = -1.0f;
	ctx->now_ms = 0;
	ctx->next_range_ms = 0;
	ctx->next_lcd_ms = 0;
	ctx->startup_end_ms = 0;
	ctx->obstacle_active = 0;
	ctx->camera_occlusion_active = 0;
	ctx->last_direction = 0;
	ctx->pwmval = 300;
	ctx->line_enabled = 0;
	ctx->wifi_override = 0;
	ctx->wifi_override_end = 0;
	ctx->line_lost = 0;
	ctx->line_lost_ms = 0;
}

/* ---- 步骤1: WiFi 命令检查 ----
 * 裸机: 直接读 USART3 缓冲并解析;
 * RTOS: 从 WiFi 任务填充的命令队列取一条, 解析执行仍在主控任务完成,
 *       保证电机只由主控任务驱动。 */
static void CAR_StepWifi(CAR_Ctx *ctx)
{
	char *pMsgBuff = 0;

#if RTOS_ENABLE
	WiFiCmdMsg msg;/*创建WiFi命令消息结构体*/
	if (RTOS_WifiCmdReceive(&msg))
	{
		pMsgBuff = msg.cmd;
		WIFI_LinkOnRxLine(pMsgBuff);
		if (wifi_dispatch_commands(pMsgBuff, &ctx->pwmval, &ctx->line_enabled))/*解析执行WiFi命令*/
		{
			/* 任务命令(TASK_/STOP,)不触发遥控覆盖, 避免进站/确认后 500ms 停顿 */
			if (strncmp(pMsgBuff, "TASK_", 5) != 0 && strncmp(pMsgBuff, "STOP,", 5) != 0)
			{
				ctx->wifi_override = 1;
				ctx->wifi_override_end = ctx->now_ms + 500;
			}
		}
	}
#else
	pMsgBuff = atk_8266_at_response_rdCmd(1);
	WIFI_LinkOnRxLine(pMsgBuff);
	if (pMsgBuff)
	{
		if (wifi_dispatch_commands(pMsgBuff, &ctx->pwmval, &ctx->line_enabled))
		{
			/* 任务命令(TASK_/STOP,)不触发遥控覆盖, 避免进站/确认后 500ms 停顿 */
			if (strncmp(pMsgBuff, "TASK_", 5) != 0 && strncmp(pMsgBuff, "STOP,", 5) != 0)
			{
				ctx->wifi_override = 1;
				ctx->wifi_override_end = ctx->now_ms + 500;
			}
		}
	}
#endif
#if !RTOS_ENABLE
	/* bare metal: only step the recovery FSM while the car is idle
	 * (AT steps block and would stall the control loop) */
	if (!ctx->line_enabled)
		WIFI_LinkPoll();
#endif
	if (ctx->wifi_override && (ctx->now_ms >= ctx->wifi_override_end))/*超时后WiFi控制结束*/
	{
		ctx->wifi_override = 0;
		ctx->startup_end_ms = ctx->now_ms + MOTOR_START_TIME_MS;
	}
}

/* ---- 步骤2: JPEG 帧处理与发送 ----
 * 裸机: 在主循环内轮询处理; RTOS: 由独立的图传任务调用, 本函数仅在裸机使用 */
#if !RTOS_ENABLE
static void CAR_StepJpeg(void)
{
	if (WIFI_IsReconnecting())
		return;/* keep AT traffic off the video stream */
	if (jpeg_data_ok == 1)
		jpeg_process_and_send();
	if (!USART3_DMA_IsBusy() && (jpeg_data_ok == 2))
	{
		DMA2_Stream1->NDTR = JPEG_BUF_SIZE;
		DMA_SetCurrDataCounter(DMA2_Stream1, JPEG_BUF_SIZE);
		DMA_Cmd(DMA2_Stream1, ENABLE);
		DCMI_CaptureCmd(ENABLE);
		jpeg_data_ok = 0;
	}
}
#endif

/* ---- 步骤3: UI 任务(裸机); RTOS 下由独立 UI 任务执行 ---- */
#if !RTOS_ENABLE
static void CAR_StepUi(void)
{
#if TOUCH_MODULE_ENABLE
	RTOS_LcdLock();
	UI_Task();
	RTOS_LcdUnlock();
#endif
}
#endif

/* ---- 步骤4: 摄像头任务 + 遮挡检测 ---- */
static void CAR_StepCamera(CAR_Ctx *ctx)
{
	RTOS_LcdLock();
	CAMERA_Task(ctx->now_ms);
	RTOS_LcdUnlock();
	/* 遮挡检测: 遮挡时立即停车, 解除遮挡后延时起步 */
	if (CAMERA_IsOccluded())
	{
		if (!ctx->camera_occlusion_active)
		{
			ctx->camera_occlusion_active = 1;/* 遮挡状态标志位激活 */
			MOTOR_Stop();
		}
	}
	else if (ctx->camera_occlusion_active)
	{
		ctx->camera_occlusion_active = 0;
		ctx->startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;
	}
}

/* ---- 步骤5: 超声波测距 + 避障逻辑 ---- */
static void CAR_StepRange(CAR_Ctx *ctx)
{
	if (ctx->now_ms >= ctx->next_range_ms)
	{
		ctx->distance = HC_SR04_GetDistance();
		RTOS_LcdLock();
		UI_SetDistance(ctx->distance);
		RTOS_LcdUnlock();
		ctx->next_range_ms = EXTIX_GetMillis() + 50;

		if (!UI_IsAvoidEnabled())
		{
			ctx->obstacle_active = 0;
			RTOS_LcdLock();
			UI_SetObstacle(0);
			RTOS_LcdUnlock();
		}
		else if (ctx->distance >= 0.0f)
		{
			if (!ctx->obstacle_active && (ctx->distance <= OBSTACLE_DISTANCE_CM))
			{
				ctx->obstacle_active = 1;
				MOTOR_Stop();
				RTOS_LcdLock();
				UI_SetObstacle(1);
				RTOS_LcdUnlock();
			}
			else if (ctx->obstacle_active && (ctx->distance >= OBSTACLE_CLEAR_CM))
			{
				ctx->obstacle_active = 0;
				ctx->startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;
				RTOS_LcdLock();
				UI_SetObstacle(0);
				RTOS_LcdUnlock();
			}
		}
	}
}

/* ---- 步骤6: ADC 采样 + LED 心跳 + 数据页刷新 ---- */
static void CAR_StepAdc(CAR_Ctx *ctx)
{
	char state_buf[12];

	ADC_ReadLineSensors();

	/* non-task line following: latch a stop after LINE_LOST_TIMEOUT_MS */
	if (!ctx->line_enabled)
	{
		ctx->line_lost = 0;/* stopTask clears the latch */
		ctx->line_lost_ms = 0;
	}
	else if (g_task.state == TASK_IDLE)
	{
		if (!LINE_IsBlack(adcL) && !LINE_IsBlack(adcM) && !LINE_IsBlack(adcR))
		{
			if (ctx->line_lost_ms == 0)
				ctx->line_lost_ms = EXTIX_GetMillis();
			else if ((s32)(EXTIX_GetMillis() - ctx->line_lost_ms) >= LINE_LOST_TIMEOUT_MS)
				ctx->line_lost = 1;
		}
		else
			ctx->line_lost_ms = 0;
	}
	CAR_GetState(state_buf);
	RTOS_LcdLock();
	UI_SetLineSensors(adcL, adcM, adcR);
	UI_SetMotionState(state_buf, ctx->pwmval);
	RTOS_LcdUnlock();
	if (EXTIX_GetMillis() >= ctx->next_lcd_ms)
	{
		LED2 = !LED2;
		ctx->next_lcd_ms = EXTIX_GetMillis() + ADC_SAMPLE_PERIOD_MS;
	}
}

/* ---- 步骤7: 避障/遮挡/遥控暂停 -> 起步加速 -> 循迹逻辑 ---- */
static void CAR_StepMotion(CAR_Ctx *ctx)
{
	u8 state = CAR_STATE_STOP;

	if (ctx->obstacle_active || ctx->camera_occlusion_active || ctx->wifi_override)
	{
		if (ctx->wifi_override)
			state = CAR_STATE_REMOTE;
		else if (ctx->obstacle_active)
			state = CAR_STATE_AVOID_STOP;
		else
			state = CAR_STATE_OCCLUSION_STOP;
		if (ctx->obstacle_active || ctx->camera_occlusion_active || !ctx->line_enabled)
			MOTOR_Stop();
	}
	/* 未收到 startTask 前不执行循迹, 小车保持静止 */
	else if (!ctx->line_enabled)
	{
		MOTOR_Stop();
		state = CAR_STATE_STANDBY;
	}
	/* line lost (non-task line following): stay stopped */
	else if (ctx->line_lost)
	{
		MOTOR_Stop();
		state = CAR_STATE_LINE_LOST;
	}

	/* 配送任务: 等待确认/暂停/中止期间强制停车 */
	else if (g_task.state == TASK_LINE_LOST)
	{
		MOTOR_Stop();
		state = CAR_STATE_LINE_LOST;
	}
	else if (CAR_TaskHolding())
	{
		MOTOR_Stop();
		state = CAR_STATE_STOP;
	}
	else if ((s32)(ctx->startup_end_ms - EXTIX_GetMillis()) > 0)
	{
		MOTOR_ForwardDual(MOTOR_LEFT_START_SPEED, MOTOR_START_SPEED);
		state = CAR_STATE_STARTUP;
	}
	/* 循迹逻辑: 仅在 WiFi 遥控未激活时执行 */
	else if (!LINE_IsBlack(adcL) && !LINE_IsBlack(adcM) && !LINE_IsBlack(adcR))
	{
		if (ctx->last_direction < 0)
		{
			MOTOR_LineSearchLeft();
			state = CAR_STATE_SEARCH_LEFT;
		}
		else if (ctx->last_direction > 0)
		{
			MOTOR_LineSearchRight();
			state = CAR_STATE_SEARCH_RIGHT;
		}
		else
		{
			MOTOR_Stop();
			state = CAR_STATE_STOP;
		}
	}
	else if (((u32)adcM + LINE_CENTER_TOLERANCE >= adcL) &&
	         ((u32)adcM + LINE_CENTER_TOLERANCE >= adcR))
	{
		if (CAR_TaskApproaching())
			MOTOR_LineForwardSpeed(TASK_APPROACH_SPEED);/* 进站缓行降速 */
		else
			MOTOR_LineForward();
		ctx->last_direction = 0;
		state = CAR_STATE_FORWARD;
	}
	else if (adcL > adcR)
	{
		MOTOR_LineTurnLeft();
		ctx->last_direction = -1;
		state = CAR_STATE_TURN_LEFT;
	}
	else
	{
		MOTOR_LineTurnRight();
		ctx->last_direction = 1;
		state = CAR_STATE_TURN_RIGHT;
	}

	g_car_state = state;
}

/* ---- 运动状态文本(供 UI 数据页显示) ---- */
void CAR_GetState(char *buf)
{
	switch(g_car_state)
	{
	case CAR_STATE_REMOTE:         strcpy(buf, "REMOTE");     break;
	case CAR_STATE_AVOID_STOP:     strcpy(buf, "AVOID STOP"); break;
	case CAR_STATE_OCCLUSION_STOP: strcpy(buf, "BLOCK STOP"); break;
	case CAR_STATE_STARTUP:        strcpy(buf, "STARTING");   break;
	case CAR_STATE_STANDBY:        strcpy(buf, "STANDBY");    break;
	case CAR_STATE_LINE_LOST:      strcpy(buf, "LINE LOST"); break;
	case CAR_STATE_FORWARD:        strcpy(buf, "FORWARD");    break;
	case CAR_STATE_TURN_LEFT:      strcpy(buf, "TURN L");     break;
	case CAR_STATE_TURN_RIGHT:     strcpy(buf, "TURN R");     break;
	case CAR_STATE_SEARCH_LEFT:    strcpy(buf, "SEARCH L");   break;
	case CAR_STATE_SEARCH_RIGHT:   strcpy(buf, "SEARCH R");   break;
	default:                       strcpy(buf, "STOP");       break;
	}
}

/* ==================== 裸机主循环: 保持原有行为 ==================== */
void CAR_LineFollow(void)
{
	CAR_Ctx ctx;

	CAR_CtxInit(&ctx);
	ctx.startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;

	while (1)
	{
		ctx.now_ms = EXTIX_GetMillis();

		CAR_StepWifi(&ctx);
#if !RTOS_ENABLE
		CAR_StepJpeg();
		CAR_StepUi();
#endif
		CAR_StepCamera(&ctx);
		CAR_StepRange(&ctx);
		CAR_StepAdc(&ctx);
		CAR_StepTask(&ctx);
		CAR_StepMotion(&ctx);
		WDG_Feed();/* single loop: a full pass proves liveness */
		USART3_TextPump();
	}
}

#if RTOS_ENABLE
/* ==================== RTOS 主控任务 ====================
 * 周期 2ms, 复用与裸机完全相同的步骤函数(除 UI/JPEG 由独立任务承担)。
 * WiFi 命令经队列送达, 超声波测距的 30ms 忙等不再阻塞 UI/图传任务。 */
#define CAR_CONTROL_PERIOD_TICKS   2

void vTaskCarControl(void *pvParameters)
{
	CAR_Ctx ctx;/*创建小车参数结构体*/
	TickType_t xLastWake;
	u32 wdg_last_ms = 0;/*获取当前系统滴答数*/

	(void)pvParameters;/*避免编译器警告*/
	CAR_CtxInit(&ctx);
	ctx.startup_end_ms = EXTIX_GetMillis() + MOTOR_START_TIME_MS;/*小车启动时间*/

	xLastWake = xTaskGetTickCount();
	while (1)
	{
		vTaskDelayUntil(&xLastWake, CAR_CONTROL_PERIOD_TICKS);

		WDG_TaskTick(WDG_TASK_CONTROL);
		if ((u32)(EXTIX_GetMillis() - wdg_last_ms) >= 200)
		{
			wdg_last_ms = EXTIX_GetMillis();
			WDG_CheckAndFeed();/* reset the IWDG only if every task is alive */
		}
		USART3_TextPump();/* flush queued reports when the link is free */

		ctx.now_ms = EXTIX_GetMillis();

		CAR_StepWifi(&ctx);
		CAR_StepCamera(&ctx);
		CAR_StepRange(&ctx);
		CAR_StepAdc(&ctx);
		CAR_StepTask(&ctx);
		CAR_StepMotion(&ctx);
	}
}
#endif
