/* ==================== WiFi 命令解析与执行 ==================== */
/* 上位机指令经 wifi_dispatch_commands() 按 \r\n 拆分后,
 * 由 handle_wifi_command() 逐条匹配执行。 */
#include "sys.h"
#include "string.h"
#include "motor.h"
#include "wifi_cmd.h"
#include "car_app.h"

/* 遥控命令: 手动控制(forward/backward/left/right/stop/speed) 与任务控制
 * (startTask/stopTask)。startTask 使能循迹, stopTask 停止循迹并刹停。
 * 返回 1 表示命令被识别执行, 返回 0 表示未识别。 */
u8 handle_wifi_command(char *pMsgBuff, u16 *pwmval, u8 *line_enabled)
{
	if (pMsgBuff == 0) return 0;

	if (strstr((const char*)pMsgBuff, (const char*)"forWard"))
		{ MOTOR_Forward(*pwmval);  return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"backWard"))
		{ MOTOR_Backward(*pwmval); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"lefTurn"))
		{ MOTOR_TurnLeft(*pwmval); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"righTurn"))
		{ MOTOR_TurnRight(*pwmval); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"letStop"))
		{ MOTOR_Stop(); return 1; }
	if (strstr((const char*)pMsgBuff, (const char*)"speedUp"))
	{
		*pwmval += 50;
		if (*pwmval > 500) *pwmval = 500;
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"speedDown"))
	{
		if (*pwmval >= 100) *pwmval -= 50;
		else *pwmval = 50;
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"startTask"))
	{
		*line_enabled = 1;
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"stopTask"))
	{
		*line_enabled = 0;
		MOTOR_Stop();
		return 1;
	}
	/* ---- 任务配送命令(P0 闭环协议, 见 P0_task_delivery_design.md) ----
	 * TASK_START,<task_id>,<from>,<to> / STOP,<task_id>,<station_id> /
	 * TASK_CONFIRM,<task_id>,<load|unload>,<user> /
	 * TASK_PAUSE,<task_id> / TASK_RESUME,<task_id> / TASK_CANCEL,<task_id>
	 * 统一交给 CAR_OnTaskCommand 处理(状态机在 car_app.c), 命令本身不触发遥控覆盖。 */
	if (strstr((const char*)pMsgBuff, (const char*)"TASK_START"))
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_START, pMsgBuff + 10, line_enabled);
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"TASK_CONFIRM"))
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_CONFIRM, pMsgBuff + 12, line_enabled);
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"TASK_PAUSE"))
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_PAUSE, pMsgBuff + 10, line_enabled);
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"TASK_RESUME"))
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_RESUME, pMsgBuff + 11, line_enabled);
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"TASK_CANCEL"))
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_CANCEL, pMsgBuff + 11, line_enabled);
		return 1;
	}
	if (strstr((const char*)pMsgBuff, (const char*)"STOP,"))
	{
		CAR_OnTaskCommand(CAR_CMD_STOP, pMsgBuff + 5, line_enabled);
		return 1;
	}
	return 0;
}

/* ---- 命令串拆分执行 ----
 * 上位机每条命令以 \r\n 结尾; 当按键按下与松开间隔小于 100ms 时, 多条命令
 * 会合并进同一帧(例如 "forWard\r\nletStop\r\n")。这里按行拆分后逐条执行,
 * 避免快速按键时只执行第一条命令导致小车无法停止。 */
u8 wifi_dispatch_commands(char *pBuf, u16 *pwmval, u8 *line_enabled)
{
	char *token;
	u8 handled = 0;

	if (pBuf == 0) return 0;

	token = strtok(pBuf, "\r\n");
	while (token)
	{
		if (*token != 0)
			handled |= handle_wifi_command(token, pwmval, line_enabled);
		token = strtok(0, "\r\n");
	}
	return handled;
}
