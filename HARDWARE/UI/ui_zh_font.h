#ifndef __UI_ZH_FONT_H
#define __UI_ZH_FONT_H

#include "sys.h"

typedef enum
{
	ZH_YAO, ZH_PIN, ZH_PEI, ZH_SONG, ZH_BI, ZH_ZHANG, ZH_AI, ZH_YI,
	ZH_KAI, ZH_QI, ZH_GUAN, ZH_BIHE, ZH_JIAN, ZH_CE, ZH_QIE, ZH_HUAN,
	ZH_ZHU, ZH_KONG, ZH_SHU, ZH_JU, ZH_WANG, ZH_LUO, ZH_XUN, ZH_XIAN,
	ZH_LIANG, ZH_DU, ZH_DUI, ZH_BI_COMPARE, ZH_SE
} UI_ZhChar;

#define ZH_END 0xFF

void UI_ZH_DrawText(u16 x, u16 y, const u8 *text, u16 color, u16 back);

#endif
