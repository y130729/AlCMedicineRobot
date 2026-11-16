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
/* Exact token match: lines are already split on CR/LF, so a command must
 * match the whole token (no substring hits such as "forWardX"). */
static u8 wifi_cmd_is(const char *line, const char *cmd)
{
	u32 n = (u32)strlen(cmd);

	if (strncmp(line, cmd, n) != 0)
		return 0;
	if (line[n] != 0 && line[n] != ',')
		return 0;/* require a token or field boundary after the name */
	return 1;
}

u8 handle_wifi_command(char *pMsgBuff, u16 *pwmval, u8 *line_enabled)
{
	if (pMsgBuff == 0) return 0;

	/* ---- remote control: manual drive / speed / line enable ---- */
	if (wifi_cmd_is(pMsgBuff, "forWard"))  { MOTOR_Forward(*pwmval);  return 1; }
	if (wifi_cmd_is(pMsgBuff, "backWard")) { MOTOR_Backward(*pwmval); return 1; }
	if (wifi_cmd_is(pMsgBuff, "lefTurn"))  { MOTOR_TurnLeft(*pwmval); return 1; }
	if (wifi_cmd_is(pMsgBuff, "righTurn")) { MOTOR_TurnRight(*pwmval);return 1; }
	if (wifi_cmd_is(pMsgBuff, "letStop"))  { MOTOR_Stop(); return 1; }
	if (wifi_cmd_is(pMsgBuff, "speedUp"))
	{
		*pwmval += 50;
		if (*pwmval > 500) *pwmval = 500;
		return 1;
	}
	if (wifi_cmd_is(pMsgBuff, "speedDown"))
	{
		if (*pwmval >= 100) *pwmval -= 50;
		else *pwmval = 50;
		return 1;
	}
	if (wifi_cmd_is(pMsgBuff, "startTask")) { *line_enabled = 1; return 1; }
	if (wifi_cmd_is(pMsgBuff, "stopTask"))  { *line_enabled = 0; MOTOR_Stop(); return 1; }
	if (wifi_cmd_is(pMsgBuff, "STATS"))     { CAR_PrintStats(); return 1; }

	/* ---- P0 task commands: strict prefix including the comma, the argument
	 *      pointer starts right after it (fixes the previous off-by-one that
	 *      left task_id empty) ---- */
	if (strncmp(pMsgBuff, "TASK_START,", 11) == 0)
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_START, pMsgBuff + 11, line_enabled);
		return 1;
	}
	if (strncmp(pMsgBuff, "TASK_CONFIRM,", 13) == 0)
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_CONFIRM, pMsgBuff + 13, line_enabled);
		return 1;
	}
	if (strncmp(pMsgBuff, "TASK_PAUSE,", 11) == 0)
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_PAUSE, pMsgBuff + 11, line_enabled);
		return 1;
	}
	if (strncmp(pMsgBuff, "TASK_RESUME,", 12) == 0)
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_RESUME, pMsgBuff + 12, line_enabled);
		return 1;
	}
	if (strncmp(pMsgBuff, "TASK_CANCEL,", 12) == 0)
	{
		CAR_OnTaskCommand(CAR_CMD_TASK_CANCEL, pMsgBuff + 12, line_enabled);
		return 1;
	}
	if (strncmp(pMsgBuff, "STOP,", 5) == 0)
	{
		CAR_OnTaskCommand(CAR_CMD_STOP, pMsgBuff + 5, line_enabled);
		return 1;
	}
	return 0;/* not a known command */
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
