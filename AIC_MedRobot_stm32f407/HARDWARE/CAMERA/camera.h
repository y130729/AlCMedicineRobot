#ifndef __CAMERA_H
#define __CAMERA_H

#include "sys.h"

#define CAMERA_X       2
#define CAMERA_Y      47
#define CAMERA_WIDTH 236
#define CAMERA_HEIGHT 140

u8 CAMERA_Init(void);
void CAMERA_SetJpegMode(u8 enable);
void CAMERA_Task(u32 now_ms);
void CAMERA_ToggleZoom(void);
u8 CAMERA_IsZoomed(void);
u8 CAMERA_IsOccluded(void);
void CAMERA_GetOcclusionStats(u32 *average, u32 *variance, u32 *gradient);

#endif
