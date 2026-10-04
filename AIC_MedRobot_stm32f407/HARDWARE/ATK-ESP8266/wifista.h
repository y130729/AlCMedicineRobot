#ifndef __WIFISTA_H__
#define __WIFISTA_H__

void atk_8266_wifista_config(void);

/* Network info accessors (read-only, for UI display etc.) */
const char* ATK_8266_GetSSID(void);       /* target AP SSID          */
const char* ATK_8266_GetLocalIP(void);    /* module STA local IP     */
const char* ATK_8266_GetServerIP(void);   /* TCP server IP           */
const char* ATK_8266_GetServerPort(void); /* TCP server port         */
u8 ATK_8266_IsOnline(void);               /* AP connected & got IP   */

/* ==================== Link supervision / recovery ==================== */
#define WIFI_LINK_DOWN   0
#define WIFI_LINK_UP     1

void WIFI_LinkInit(void);                 /* start supervision (link is UP)   */
void WIFI_LinkPoll(void);                 /* step the recovery FSM (~10ms)    */
void WIFI_LinkOnRxLine(const char *line); /* feed received lines (loss detect)*/
u8   WIFI_IsReconnecting(void);           /* 1 = pause video / AT in progress */
u8   WIFI_GetLinkState(void);             /* WIFI_LINK_UP / WIFI_LINK_DOWN    */
u16  WIFI_GetReconnectCount(void);        /* number of recovery attempts      */

#endif
