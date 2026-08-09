/*
 * wifi_config.h - WiFi network parameters (single source of truth)
 *
 * All network parameters used by atk_8266_wifista_config() are centralized
 * here as compile-time macros. Edit this file to change the target AP or the
 * TCP server without touching the driver code.
 */
#ifndef __WIFI_CONFIG_H__
#define __WIFI_CONFIG_H__

/* Target AP (STA mode) */
#define WIFI_STA_SSID       "ChinaNet-328D"
#define WIFI_STA_PASSWORD   "wh37kz37"

/* TCP server that the car connects to (upper computer listens on this port) */
#define WIFI_SERVER_IP      "192.168.2.137"
#define WIFI_SERVER_PORT    "8086"

/* Transparent-mode UART baud rate used while streaming video.
 * The module is configured at 115200 during init; once the TCP link is up the
 * baud rate is raised to WIFI_VIDEO_BAUDRATE for higher JPEG throughput. */
#define WIFI_VIDEO_BAUDRATE  921600

#endif /* __WIFI_CONFIG_H__ */
