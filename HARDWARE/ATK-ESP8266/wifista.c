#include "common.h"
#include "stdlib.h"
#include "stdio.h"
#include "wifista.h"
#include "wifi_config.h"
#include "lcd.h"

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

void atk_8266_wifista_config(void)
{
	while (atk_8266_send_cmd("AT+RST", "OK", 50))
	{
		LCD_ShowString(30, 170, 210, 24, 16, "     ");
		LCD_ShowString(30, 170, 210, 24, 16, "RST");
		delay_ms(1000);
	}

	while (atk_8266_send_cmd("AT+CWMODE=1", "OK", 50))
	{
		LCD_ShowString(30, 170, 210, 24, 16, "     ");
		LCD_ShowString(30, 170, 210, 24, 16, "CWMODE");
		delay_ms(1000);
	}

	/* 连接 wifi_config.h 中配置的 AP */
	{
		char cwjap_cmd[96];
		sprintf(cwjap_cmd, "AT+CWJAP=\"%s\",\"%s\"", WIFI_STA_SSID, WIFI_STA_PASSWORD);
		while (atk_8266_send_cmd((u8 *)cwjap_cmd, "WIFI GOT IP", 600))
		{
			LCD_ShowString(30, 170, 210, 24, 16, "     ");
			LCD_ShowString(30, 170, 210, 24, 16, "CWJAP");
			delay_ms(1000);
		}
	}

	/* CWJAP ok: mark online and cache local IP before transparent mode */
	wifi_online = 1;
	atk_8266_query_local_ip();

	/* 图传前临时提升串口波特率: 模块收到 OK 后切换, 随后把 USART3 调到同速。
	 * 若模块不支持 AT+UART_DEF(旧固件), 则保持 115200 继续, 不影响功能。 */
	if (atk_8266_send_cmd("AT+UART_DEF=921600,8,1,0,0", "OK", 200) == 0)
	{
		usart3_set_baudrate(921600);
		delay_ms(50);
	}

	while (atk_8266_send_cmd("AT+CIPMUX=0", "OK", 200))
	{
		LCD_ShowString(30, 170, 210, 24, 16, "     ");
		LCD_ShowString(30, 170, 210, 24, 16, "CIPMUX");
		delay_ms(200);
	}

	/* 连接 wifi_config.h 中配置的 TCP 服务器 */
	{
		char cipstart_cmd[96];
		sprintf(cipstart_cmd, "AT+CIPSTART=\"TCP\",\"%s\",%s", WIFI_SERVER_IP, WIFI_SERVER_PORT);
		while (atk_8266_send_cmd((u8 *)cipstart_cmd, "OK", 1000))
		{
			LCD_ShowString(30, 170, 210, 24, 16, "     ");
			LCD_ShowString(30, 170, 210, 24, 16, "CIPSTART");
			delay_ms(200);
		}
	}

	atk_8266_send_cmd("AT+CIPMODE=1", "OK", 200);      //设置为透传模式
	delay_ms(200);

	LCD_ShowString(30, 170, 210, 24, 16, "             ");
	LCD_ShowString(30, 170, 210, 24, 16, "connect success");
	atk_8266_send_cmd("AT+CIPSEND", "OK", 200);       //开启透传模式
	delay_ms(200);
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
