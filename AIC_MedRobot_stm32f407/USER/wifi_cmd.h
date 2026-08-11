#ifndef __WIFI_CMD_H
#define __WIFI_CMD_H

#include "sys.h"

/* 逐条解析并执行一条 WiFi 命令(不含 \r\n)。
 * pMsgBuff: 命令串; pwmval: 当前速度(可被 speedUp/Down 修改);
 * line_enabled: 循迹使能标志(startTask/stopTask 修改)。
 * 返回 1 = 命令被识别执行, 0 = 未识别。 */
u8 handle_wifi_command(char *pMsgBuff, u16 *pwmval, u8 *line_enabled);

/* 按 \r\n 拆分一帧命令串并逐条执行, 返回是否至少执行了一条命令。 */
u8 wifi_dispatch_commands(char *pBuf, u16 *pwmval, u8 *line_enabled);

#endif /* __WIFI_CMD_H */
