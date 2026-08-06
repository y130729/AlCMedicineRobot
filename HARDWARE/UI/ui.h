#ifndef __UI_H
#define __UI_H

#include "sys.h"

typedef enum
{
	UI_EVENT_NONE = 0,
	UI_EVENT_UP,
	UI_EVENT_DOWN,
	UI_EVENT_LEFT,
	UI_EVENT_RIGHT,
	UI_EVENT_AVOID_TOGGLE,
	UI_EVENT_TAB_CTRL,
	UI_EVENT_TAB_DATA,
	UI_EVENT_TAB_NET
} UI_Event;

typedef enum
{
	UI_PAGE_CONTROL = 0,
	UI_PAGE_DATA,
	UI_PAGE_NETWORK
} UI_Page;

void UI_Init(void);
void UI_Task(void);
void UI_SetDistance(float distance_cm);
void UI_SetObstacle(u8 present);
u8 UI_IsAvoidEnabled(void);
UI_Event UI_GetEvent(void);
UI_Page UI_GetPage(void);
void UI_SetColorRecognition(u32 red_count, u32 green_count, u8 result);

#endif
