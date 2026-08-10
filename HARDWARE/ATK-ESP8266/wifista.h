#ifndef __WIFISTA_H__
#define __WIFISTA_H__

void atk_8266_wifista_config(void);

/* Network info accessors (read-only, for UI display etc.) */
const char* ATK_8266_GetSSID(void);       /* target AP SSID          */
const char* ATK_8266_GetLocalIP(void);    /* module STA local IP     */
const char* ATK_8266_GetServerIP(void);   /* TCP server IP           */
const char* ATK_8266_GetServerPort(void); /* TCP server port         */
u8 ATK_8266_IsOnline(void);               /* AP connected & got IP   */

#endif
