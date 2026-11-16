#include "common.h"
#include "stdlib.h"
#include "stdio.h"
#include "wifista.h"
#include "wifi_config.h"
#include "lcd.h"
#include "exti.h"
#include "wifi_transfer.h"

/* ==================== Network info cache (for UI display) ==================== */
static char wifi_local_ip[16] = "0.0.0.0"; /* STA local IP, cached at config time */
static u8   wifi_online = 0;               /* 1 = AP connected and got IP         */

/* Query AT+CIFSR and cache the STA local IP.
 * Must run BEFORE entering transparent mode: after AT+CIPSEND the AT commands
 * are forwarded to the TCP server as payload data. */
static void atk_8266_query_local_ip(void)
{
	u8 *p;
	u8 *q;
	u16 len;

	strcpy(wifi_local_ip, "0.0.0.0");
	if (atk_8266_send_cmd("AT+CIFSR", "OK", 100))  /* query failed, keep 0.0.0.0 */
		return;

	USART3_RX_BUF[USART3_RX_STA & 0x7FFF] = 0;
	p = (u8 *)strstr((const char *)USART3_RX_BUF, "+CIFSR:STAIP,");
	if (p == 0)
		return;

	p += strlen("+CIFSR:STAIP,");
	if (*p == '"')  /* tolerate quoted / unquoted reply formats */
		p++;
	q = p;
	while (*q && *q != '"' && *q != '\r' && *q != '\n')
		q++;
	len = (u16)(q - p);
	if (len == 0)
		return;
	if (len >= sizeof(wifi_local_ip))
		len = sizeof(wifi_local_ip) - 1;
	memcpy(wifi_local_ip, p, len);
	wifi_local_ip[len] = 0;
}
//ATK-ESP8266 WIFI STA模式
//用于连接TCP/UDP服务器
//返回值:0,成功
//    其他,失败
//网络参数统一在 wifi_config.h 中配置

/* Bounded boot sequence. Returns 0 when the TCP link is up, 1 on timeout.
 * Every step has a retry cap and the whole sequence has a deadline, so a
 * missing AP / wrong password / missing server can no longer hang the boot
 * (the old code retried forever before the watchdog was even started). */
#define WIFI_BOOT_TIMEOUT_MS   30000UL
#define WIFI_BOOT_RETRY        5

u8 atk_8266_wifista_config(void)
{
	char cmd[96];
	u8 step = 0;
	u8 retry = 0;
	u32 t0 = EXTIX_GetMillis();

	while (step < 8)
	{
		if ((s32)(EXTIX_GetMillis() - t0) > (s32)WIFI_BOOT_TIMEOUT_MS)
		{
			printf("BOOT_TIMEOUT,step=%d\r\n", step);
			wifi_online = 0;
			return 1;
		}

		switch (step)
		{
		case 0:  /* module reset */
			if (atk_8266_send_cmd("AT+RST", "OK", 50) == 0) { step++; retry = 0; }
			else if (++retry >= WIFI_BOOT_RETRY) { wifi_online = 0; return 1; }
			else { LCD_ShowString(30, 170, 210, 24, 16, "RST          "); delay_ms(500); }
			break;

		case 1:  /* station mode */
			if (atk_8266_send_cmd("AT+CWMODE=1", "OK", 50) == 0) { step++; retry = 0; }
			else if (++retry >= WIFI_BOOT_RETRY) { wifi_online = 0; return 1; }
			else { LCD_ShowString(30, 170, 210, 24, 16, "CWMODE       "); delay_ms(500); }
			break;

		case 2:  /* join AP */
			sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", WIFI_STA_SSID, WIFI_STA_PASSWORD);
			if (atk_8266_send_cmd((u8 *)cmd, "WIFI GOT IP", 600) == 0)
			{
				atk_8266_query_local_ip();
				step++;
				retry = 0;
			}
			else if (++retry >= 3)
			{
				wifi_online = 0;
				return 1;
			}
			else
			{
				LCD_ShowString(30, 170, 210, 24, 16, "CWJAP        ");
				delay_ms(500);
			}
			break;

		case 3:  /* optional 921600 baud, never fatal */
			if (atk_8266_send_cmd("AT+UART_DEF=921600,8,1,0,0", "OK", 200) == 0)
			{
				usart3_set_baudrate(921600);
				delay_ms(50);
			}
			step++;
			retry = 0;
			break;

		case 4:  /* single connection */
			if (atk_8266_send_cmd("AT+CIPMUX=0", "OK", 200) == 0) { step++; retry = 0; }
			else if (++retry >= WIFI_BOOT_RETRY) { wifi_online = 0; return 1; }
			else { LCD_ShowString(30, 170, 210, 24, 16, "CIPMUX       "); delay_ms(200); }
			break;

		case 5:  /* open TCP */
			sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%s", WIFI_SERVER_IP, WIFI_SERVER_PORT);
			if (atk_8266_send_cmd((u8 *)cmd, "OK", 300) == 0) { step++; retry = 0; }
			else if (++retry >= WIFI_BOOT_RETRY) { wifi_online = 0; return 1; }
			else { LCD_ShowString(30, 170, 210, 24, 16, "CIPSTART     "); delay_ms(200); }
			break;

		case 6:
			atk_8266_send_cmd("AT+CIPMODE=1", "OK", 200);
			step++;
			break;

		case 7:
			if (atk_8266_send_cmd("AT+CIPSEND", "OK", 200) == 0)
			{
				wifi_online = 1;
				WIFI_LinkInit();
				step++;
			}
			else if (++retry >= 3)
			{
				wifi_online = 0;
				return 1;
			}
			break;

		default:
			step++;
			break;
		}
	}

	delay_ms(200);
	return 0;
}

/* ==================== Network info accessors ==================== */
const char* ATK_8266_GetSSID(void)
{
	return WIFI_STA_SSID;
}

const char* ATK_8266_GetLocalIP(void)
{
	return wifi_local_ip;
}

const char* ATK_8266_GetServerIP(void)
{
	return WIFI_SERVER_IP;
}

const char* ATK_8266_GetServerPort(void)
{
	return WIFI_SERVER_PORT;
}

u8 ATK_8266_IsOnline(void)
{
	return wifi_online;
}

/* ==================== Link supervision / recovery ====================
 * The ESP8266 runs in transparent (CIPMODE=1) mode, where AT commands are not
 * accepted. When the socket drops, the module prints CLOSED and discards
 * payload, so WIFI_LinkOnRxLine() watches the received lines for such markers
 * and WIFI_LinkPoll() then runs a non-blocking recovery ladder:
 *   escape transparent -> reopen TCP -> (if needed) rejoin AP -> re-enter transparent
 * Video is paused while recovering (WIFI_IsReconnecting()) because the AT
 * traffic and the JPEG stream share USART3; every AT step also kicks the WiFi
 * watchdog tick so longer waits never look like a hung task.
 */
#define WIFI_LK_UP        0
#define WIFI_LK_ESCAPE    1
#define WIFI_LK_TCP       2
#define WIFI_LK_AP        3
#define WIFI_LK_TRANS     4
#define WIFI_LK_BACKOFF   5

#define WIFI_LK_GUARD_MS      20
#define WIFI_LK_AT_RETRY      3
#define WIFI_LK_TCP_RETRY     8
#define WIFI_LK_AP_RETRY      5
#define WIFI_LK_AP_WAIT       600
#define WIFI_LK_BACKOFF_MIN   1000
#define WIFI_LK_BACKOFF_MAX   30000
#define WIFI_LK_RST_ROUNDS    4

static volatile u8  wifi_lk_st = WIFI_LK_UP;
static volatile u8  wifi_lk_recovering = 0;
static volatile u16 wifi_lk_count = 0;
static u8  wifi_lk_step = 0;
static u8  wifi_lk_retry = 0;
static u8  wifi_lk_round = 0;
static u32 wifi_lk_step_ms = 0;
static u32 wifi_lk_backoff_ms = WIFI_LK_BACKOFF_MIN;

static void wifi_lk_goto(u8 st)
{
	wifi_lk_st = st;
	wifi_lk_step = 0;
	wifi_lk_step_ms = EXTIX_GetMillis();
}

u8 WIFI_IsReconnecting(void)
{
	return wifi_lk_recovering;
}

u8 WIFI_GetLinkState(void)
{
	return (wifi_lk_st == WIFI_LK_UP) ? WIFI_LINK_UP : WIFI_LINK_DOWN;
}

u16 WIFI_GetReconnectCount(void)
{
	return wifi_lk_count;
}

void WIFI_LinkInit(void)
{
	wifi_lk_st = WIFI_LK_UP;
	wifi_lk_recovering = 0;
	wifi_lk_step = 0;
	wifi_lk_retry = 0;
	wifi_lk_round = 0;
	wifi_lk_backoff_ms = WIFI_LK_BACKOFF_MIN;
}

/* Boot could not bring the link up (bounded sequence timed out). Enter the
 * same non-blocking recovery ladder used for runtime link loss, so the car
 * keeps running offline and retries in the background. */
void WIFI_LinkStartRecovery(void)
{
	if (wifi_lk_st != WIFI_LK_UP || wifi_lk_recovering)
		return;
	wifi_lk_recovering = 1;
	wifi_lk_count++;
	wifi_lk_retry = 0;
	wifi_lk_round = 0;
	wifi_lk_backoff_ms = WIFI_LK_BACKOFF_MIN;
	wifi_online = 0;
	JPEG_TxAbort();/* no partial frame across a new TCP connection */
	u3_printf("LINK,DOWN\r\n");
	wifi_lk_goto(WIFI_LK_ESCAPE);
}

/* Called with every received line: start recovery on link-loss markers. */
void WIFI_LinkOnRxLine(const char *line)
{
	if (line == 0)
		return;
	if (wifi_lk_st != WIFI_LK_UP)
		return;/* already recovering */
	if (strstr(line, "CLOSED") || strstr(line, "link is not valid") ||
	    strstr(line, "WIFI DISCONNECT"))
	{
		wifi_lk_recovering = 1;
		wifi_lk_count++;
		wifi_lk_retry = 0;
		wifi_lk_round = 0;
		wifi_lk_backoff_ms = WIFI_LK_BACKOFF_MIN;
		wifi_online = 0;
		JPEG_TxAbort();/* host will get a fresh frame from offset 0 */
		u3_printf("LINK,DOWN\r\n");
		wifi_lk_goto(WIFI_LK_ESCAPE);
	}
}

/* Non-blocking recovery ladder; call every ~10ms (WiFi task / idle loop). */
void WIFI_LinkPoll(void)
{
	u32 now;
	char cmd[96];

	if (wifi_lk_st == WIFI_LK_UP)
		return;

	now = EXTIX_GetMillis();
	WDG_TaskTick(WDG_TASK_WIFI);/* every AT step below may block briefly */

	switch (wifi_lk_st)
	{
	case WIFI_LK_ESCAPE:
		if (wifi_lk_step == 0)
		{
			USART3_TextDrain(500);/* keep LINK,DOWN/ACK, bounded */
			USART3_WaitIdle();
			wifi_lk_step = 1;
		}
		else if (wifi_lk_step == 1)
		{
			if (USART3_DMA_IsBusy() || USART3_TextPending() > 0)
				wifi_lk_step_ms = EXTIX_GetMillis();/* still busy: keep waiting  */
			else if ((s32)(now - wifi_lk_step_ms) >= WIFI_LK_GUARD_MS)
			{
				if (USART3_SendEscape())
				{
					USART3_WaitIdle();
					wifi_lk_step_ms = EXTIX_GetMillis();
					wifi_lk_step = 2;
				}
			}
		}
		else
		{
			if ((s32)(now - wifi_lk_step_ms) >= WIFI_LK_GUARD_MS)
			{
				if (atk_8266_send_cmd("AT", "OK", 20) == 0)
				{
					wifi_lk_retry = 0;
					wifi_lk_goto(WIFI_LK_TCP);
				}
				else if (++wifi_lk_retry >= WIFI_LK_AT_RETRY)
				{
					wifi_lk_retry = 0;
					wifi_lk_goto(WIFI_LK_BACKOFF);
				}
				else
				{
					wifi_lk_step = 0;
					wifi_lk_step_ms = EXTIX_GetMillis();
				}
			}
		}
		break;

	case WIFI_LK_TCP:
		if (wifi_lk_step == 0)
		{
			atk_8266_send_cmd("AT+CIPCLOSE", 0, 0);/* ignore result: reopen cleanly */
			wifi_lk_step = 1;
			wifi_lk_step_ms = EXTIX_GetMillis();
		}
		else
		{
			sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%s", WIFI_SERVER_IP, WIFI_SERVER_PORT);
			if (atk_8266_send_cmd((u8 *)cmd, "OK", 20) == 0)
			{
				wifi_lk_retry = 0;
				wifi_lk_goto(WIFI_LK_TRANS);
			}
			else if (++wifi_lk_retry >= WIFI_LK_TCP_RETRY)
			{
				wifi_lk_retry = 0;
				wifi_lk_goto(WIFI_LK_AP);
			}
		}
		break;

	case WIFI_LK_AP:
		sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", WIFI_STA_SSID, WIFI_STA_PASSWORD);
		if (atk_8266_send_cmd((u8 *)cmd, "WIFI GOT IP", WIFI_LK_AP_WAIT) == 0)
		{
			wifi_lk_retry = 0;
			atk_8266_query_local_ip();
			wifi_lk_goto(WIFI_LK_TCP);
		}
		else if (++wifi_lk_retry >= WIFI_LK_AP_RETRY)
		{
			wifi_lk_retry = 0;
			wifi_lk_goto(WIFI_LK_BACKOFF);
		}
		break;

	case WIFI_LK_TRANS:
		if (wifi_lk_step == 0)
		{
			atk_8266_send_cmd("AT+CIPMODE=1", "OK", 20);
			wifi_lk_step = 1;
		}
		else
		{
			if (atk_8266_send_cmd("AT+CIPSEND", "OK", 20) == 0)
			{
				wifi_online = 1;
				wifi_lk_recovering = 0;
				wifi_lk_retry = 0;
				wifi_lk_round = 0;
				wifi_lk_backoff_ms = WIFI_LK_BACKOFF_MIN;
				wifi_lk_goto(WIFI_LK_UP);
				u3_printf("LINK,UP\r\n");
			}
			else
			{
				wifi_lk_goto(WIFI_LK_BACKOFF);
			}
		}
		break;

	case WIFI_LK_BACKOFF:
	default:
		if ((s32)(now - wifi_lk_step_ms) >= (s32)wifi_lk_backoff_ms)
		{
			wifi_lk_round++;
			if (wifi_lk_round >= WIFI_LK_RST_ROUNDS)
			{
				wifi_lk_round = 0;
				atk_8266_send_cmd("AT+RST", "ready", 100);/* full module reset */
			}
			wifi_lk_backoff_ms *= 2;
			if (wifi_lk_backoff_ms > WIFI_LK_BACKOFF_MAX)
				wifi_lk_backoff_ms = WIFI_LK_BACKOFF_MAX;
			wifi_lk_goto(WIFI_LK_ESCAPE);
		}
		break;
	}
}
