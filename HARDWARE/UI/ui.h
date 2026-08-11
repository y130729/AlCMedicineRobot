#ifndef __UI_H
#define __UI_H

#include "sys.h"

typedef enum
{
	UI_CONFIRM_NONE = 0,
	UI_CONFIRM_LOAD,
	UI_CONFIRM_UNLOAD
} UI_ConfirmAction;

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
u8 UI_GetConfirmRequested(void);
void UI_ClearConfirmRequest(void);
void UI_SetTaskConfirm(u8 action);
UI_Page UI_GetPage(void);
void UI_SetLineSensors(u16 l, u16 m, u16 r);
void UI_SetMotionState(const char *state, u16 pwmval);

#endif
