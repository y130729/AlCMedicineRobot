#include "ui.h"
#include "lcd.h"
#include "touch.h"
#include "ui_zh_font.h"
#include "exti.h"
#include "ov2640.h"
#include "camera.h"
#include "wifista.h"

#define UI_DARK       0x1082
#define UI_PANEL      0x18E3
#define UI_BORDER     0x7BEF
#define UI_DOUBLE_TAP_MS 650

typedef enum
{
	UI_KEY_NONE = 0,
	UI_KEY_UP,
	UI_KEY_DOWN,
	UI_KEY_LEFT,
	UI_KEY_RIGHT,
	UI_KEY_AVOID,
	UI_KEY_TAB_CTRL,
	UI_KEY_TAB_DATA,
	UI_KEY_TAB_NET
} UI_Key;

static UI_Key pressed_key = UI_KEY_NONE;
static UI_Event pending_event = UI_EVENT_NONE;
static UI_Page current_page = UI_PAGE_CONTROL;
static u8 avoid_enabled = 1;
static u8 obstacle_present = 0;
static u16 last_distance = 0xFFFF;
static float saved_distance_cm = 0.0f;
static u32 color_red_count = 0;
static u32 color_green_count = 0;
static u8 color_result = 0;
static u32 drawn_red_count = 0XFFFFFFFF;
static u32 drawn_green_count = 0XFFFFFFFF;
static u8 drawn_color_result = 0XFF;
static u8 image_settings_drawn = 0;
static u8 image_settings_applied = 0;
static u8 drawn_image_param = IMAGE_PARAM_BRIGHTNESS;
static u8 drawn_brightness = 2;
static u8 drawn_contrast = 2;
static u8 drawn_saturation = 2;
static u8 touch_was_down = 0;
static u8 touch_target_locked = 0;
static u8 camera_tap_candidate = 0;
static u8 camera_tap_pending = 0;
static u32 camera_last_tap_ms = 0;
static UI_Key nav_candidate = UI_KEY_NONE;
static u8 nav_stable_count = 0;

static const u8 text_title[]       = {ZH_YAO,ZH_PIN,ZH_PEI,ZH_SONG,ZH_END};
static const u8 text_avoid_on[]    = {ZH_BI,ZH_ZHANG,ZH_YI,ZH_KAI,ZH_QI,ZH_END};
static const u8 text_avoid_off[]   = {ZH_BI,ZH_ZHANG,ZH_YI,ZH_GUAN,ZH_BIHE,ZH_END};
static const u8 text_obstacle[]    = {ZH_JIAN,ZH_CE,ZH_ZHANG,ZH_AI,ZH_END};
static const u8 text_enable[]      = {ZH_KAI,ZH_QI,ZH_XUN,ZH_XIAN,ZH_BI,ZH_ZHANG,ZH_END};
static const u8 text_disable[]     = {ZH_GUAN,ZH_BIHE,ZH_XUN,ZH_XIAN,ZH_BI,ZH_ZHANG,ZH_END};
static const u8 text_ctrl[]        = {ZH_ZHU,ZH_KONG,ZH_END};
static const u8 text_data[]        = {ZH_SHU,ZH_JU,ZH_END};
static const u8 text_net[]         = {ZH_WANG,ZH_LUO,ZH_END};
static const u8 text_brightness[]  = {ZH_LIANG,ZH_DU,ZH_END};
static const u8 text_contrast[]    = {ZH_DUI,ZH_BI_COMPARE,ZH_DU,ZH_END};
static const u8 text_saturation[]  = {ZH_SE,ZH_DU,ZH_END};

static void UI_ShowText(u16 x, u16 y, u16 color, u16 back, u8 *text)
{
	POINT_COLOR = color;
	BACK_COLOR = back;
	LCD_ShowString(x, y, 220, 16, 16, text);
}

static void UI_ShowZh(u16 x, u16 y, u16 color, u16 back, const u8 *text)
{
	UI_ZH_DrawText(x, y, text, color, back);
}

static void UI_DrawAvoidStatus(void)
{
	LCD_Fill(112, 207, 237, 237, UI_PANEL);
	if(obstacle_present)
		UI_ShowZh(116, 214, RED, UI_PANEL, text_obstacle);
	else if(avoid_enabled)
		UI_ShowZh(116, 214, GREEN, UI_PANEL, text_avoid_on);
	else
		UI_ShowZh(116, 214, GRAY, UI_PANEL, text_avoid_off);
}

static void UI_DrawAvoidButton(u8 down)
{
	u16 fill = down ? GRAYBLUE : UI_DARK;
	LCD_Fill(112, 243, 237, 281, fill);
	POINT_COLOR = down ? BLACK : WHITE;
	LCD_DrawRectangle(112, 243, 237, 281);
	UI_ShowZh(126, 255, down ? BLACK : WHITE, fill,
	          avoid_enabled ? text_disable : text_enable);
}

static void UI_DrawImageSettingRow(u8 param, u8 selected, u8 value)
{
	u16 y;
	u16 color;
	const u8 *text;

	if(param == IMAGE_PARAM_BRIGHTNESS)
	{
		y = 134;
		text = text_brightness;
	}
	else if(param == IMAGE_PARAM_CONTRAST)
	{
		y = 150;
		text = text_contrast;
	}
	else
	{
		y = 166;
		text = text_saturation;
	}
	color = selected == param ? GREEN : GRAY;
	LCD_Fill(5, y, 108, y + 15, BLACK);
	UI_ShowText(7, y, color, BLACK,
	            (u8 *)(selected == param ? ">" : " "));
	UI_ShowZh(23, y, color, BLACK, text);

	POINT_COLOR = WHITE;
	BACK_COLOR = BLACK;
	LCD_ShowNum(88, y, value, 1, 16);
}

static void UI_DrawImageSettings(u8 selected, u8 brightness,
	                             u8 contrast, u8 saturation)
{
	UI_DrawImageSettingRow(IMAGE_PARAM_BRIGHTNESS, selected, brightness);
	UI_DrawImageSettingRow(IMAGE_PARAM_CONTRAST, selected, contrast);
	UI_DrawImageSettingRow(IMAGE_PARAM_SATURATION, selected, saturation);
}

static void UI_DrawTabs(UI_Key down_key)
{
	u16 c0 = down_key == UI_KEY_TAB_CTRL ? GRAYBLUE : UI_DARK;
	u16 c1 = down_key == UI_KEY_TAB_DATA ? GRAYBLUE : UI_DARK;
	u16 c2 = down_key == UI_KEY_TAB_NET ? GRAYBLUE : UI_DARK;
	u16 t0 = current_page == UI_PAGE_CONTROL ? GREEN : GRAY;
	u16 t1 = current_page == UI_PAGE_DATA ? GREEN : GRAY;
	u16 t2 = current_page == UI_PAGE_NETWORK ? GREEN : GRAY;

	LCD_Fill(0, 289, 79, 319, c0);
	LCD_Fill(80, 289, 159, 319, c1);
	LCD_Fill(160, 289, 239, 319, c2);
	POINT_COLOR = UI_BORDER;
	LCD_DrawRectangle(0, 289, 79, 319);
	LCD_DrawRectangle(80, 289, 159, 319);
	LCD_DrawRectangle(160, 289, 239, 319);
	UI_ShowZh(24, 298, t0, c0, text_ctrl);
	UI_ShowZh(104, 298, t1, c1, text_data);
	UI_ShowZh(184, 298, t2, c2, text_net);
}

static void UI_DrawDirectionKey(UI_Key key, u8 down)
{
	switch(key)
	{
		case UI_KEY_UP:
			if(down) LCD_UpButtonDown(); else LCD_UpButtonUp();
			break;
		case UI_KEY_DOWN:
			if(down) LCD_DownButtonDown(); else LCD_DownButtonUp();
			break;
		case UI_KEY_LEFT:
			if(down) LCD_LeftButtonDown(); else LCD_LeftButtonUp();
			break;
		case UI_KEY_RIGHT:
			if(down) LCD_RightButtonDown(); else LCD_RightButtonUp();
			break;
		default:
			break;
	}
}

static void UI_DrawPressedKey(UI_Key key)
{
	if(key >= UI_KEY_UP && key <= UI_KEY_RIGHT)
		UI_DrawDirectionKey(key, 1);
	else if(key == UI_KEY_AVOID)
		UI_DrawAvoidButton(1);
	else if(key >= UI_KEY_TAB_CTRL)
		UI_DrawTabs(key);
}

static UI_Key UI_HitTest(u16 x, u16 y)
{
	if(y >= 289 && x < 80) return UI_KEY_TAB_CTRL;
	if(y >= 289 && x < 160) return UI_KEY_TAB_DATA;
	if(y >= 289) return UI_KEY_TAB_NET;
	if(current_page != UI_PAGE_CONTROL) return UI_KEY_NONE;
	if(x >= 44 && x <= 76 && y >= 194 && y <= 224) return UI_KEY_UP;
	if(x >= 44 && x <= 76 && y >= 256 && y <= 286) return UI_KEY_DOWN;
	if(x >= 12 && x <= 43 && y >= 225 && y <= 255) return UI_KEY_LEFT;
	if(x >= 77 && x <= 108 && y >= 225 && y <= 255) return UI_KEY_RIGHT;
	if(x >= 112 && x <= 237 && y >= 243 && y <= 281) return UI_KEY_AVOID;
	return UI_KEY_NONE;
}

static UI_Event UI_KeyToEvent(UI_Key key)
{
	switch(key)
	{
		case UI_KEY_UP: return UI_EVENT_UP;
		case UI_KEY_DOWN: return UI_EVENT_DOWN;
		case UI_KEY_LEFT: return UI_EVENT_LEFT;
		case UI_KEY_RIGHT: return UI_EVENT_RIGHT;
		case UI_KEY_AVOID: return UI_EVENT_AVOID_TOGGLE;
		case UI_KEY_TAB_CTRL: return UI_EVENT_TAB_CTRL;
		case UI_KEY_TAB_DATA: return UI_EVENT_TAB_DATA;
		case UI_KEY_TAB_NET: return UI_EVENT_TAB_NET;
		default: return UI_EVENT_NONE;
	}
}

static void UI_DrawHeader(void)
{
	LCD_Fill(0, 0, 239, 23, UI_DARK);
	UI_ShowZh(5, 4, WHITE, UI_DARK, text_title);
	UI_ShowText(176, 4, WHITE, UI_DARK, "WiFi");
	LCD_Fill(220, 7, 227, 14, GREEN);
}

static void UI_DrawColorRecognition(void)
{
	if(color_red_count != drawn_red_count)
	{
		POINT_COLOR = RED;
		BACK_COLOR = UI_PANEL;
		LCD_ShowNum(42, 207, color_red_count, 5, 16);
		drawn_red_count = color_red_count;
	}
	if(color_green_count != drawn_green_count)
	{
		POINT_COLOR = GREEN;
		BACK_COLOR = UI_PANEL;
		LCD_ShowNum(154, 207, color_green_count, 5, 16);
		drawn_green_count = color_green_count;
	}
	if(color_result != drawn_color_result)
	{
		LCD_Fill(10, 238, 229, 278, UI_PANEL);
		UI_ShowText(70, 239, LIGHTBLUE, UI_PANEL, "MAIN COLOR");
		if(color_result == 1)
			UI_ShowText(104, 260, RED, UI_PANEL, "RED");
		else if(color_result == 2)
			UI_ShowText(94, 260, GREEN, UI_PANEL, "GREEN");
		else
			UI_ShowText(88, 260, GRAY, UI_PANEL, "UNCLEAR");
		drawn_color_result = color_result;
	}
}

static void UI_DrawControlPage(void)
{
	LCD_Fill(0, 24, 239, 288, BLACK);
	last_distance = 0XFFFF;
	UI_SetDistance(saved_distance_cm);

	POINT_COLOR = UI_BORDER;
	LCD_DrawRectangle(0, 45, 239, 188);
	UI_ShowText(94, 110, GRAY, BLACK, "CAM 01");

	LCD_Fill(0, 189, 239, 288, UI_PANEL);
	LCD_UpButtonUp();
	LCD_DownButtonUp();
	LCD_LeftButtonUp();
	LCD_RightButtonUp();
	LCD_Fill(44, 225, 76, 255, UI_DARK);
	POINT_COLOR = UI_BORDER;
	LCD_DrawRectangle(44, 225, 76, 255);
	UI_ShowText(57, 232, RED, UI_DARK, "S");

	UI_DrawAvoidStatus();
	UI_DrawAvoidButton(0);
	image_settings_drawn = 0;
	if(EXTIX_IsImageMenuVisible())
	{
		UI_DrawImageSettings(drawn_image_param, drawn_brightness,
		                     drawn_contrast, drawn_saturation);
		image_settings_drawn = 1;
	}
	UI_DrawTabs(UI_KEY_NONE);
}

static void UI_DrawDataPage(void)
{
	LCD_Fill(0, 24, 239, 288, BLACK);
	UI_ShowText(72, 27, WHITE, BLACK, "COLOR DATA");
	POINT_COLOR = UI_BORDER;
	LCD_DrawRectangle(0, 45, 239, 188);
	UI_ShowText(94, 110, GRAY, BLACK, "CAM 01");
	LCD_Fill(0, 189, 239, 288, UI_PANEL);
	UI_ShowText(18, 207, RED, UI_PANEL, "R:");
	UI_ShowText(130, 207, GREEN, UI_PANEL, "G:");
	drawn_red_count = 0XFFFFFFFF;
	drawn_green_count = 0XFFFFFFFF;
	drawn_color_result = 0XFF;
	UI_DrawColorRecognition();
	image_settings_drawn = 0;
	UI_DrawTabs(UI_KEY_NONE);
}

static void UI_DrawNetworkPage(void)
{
	LCD_Fill(0, 24, 239, 288, BLACK);
	UI_ShowText(76, 27, LIGHTBLUE, BLACK, "NETWORK INFO");

	UI_ShowText(5, 60, GRAY, BLACK, "WiFi:");
	UI_ShowText(62, 60, WHITE, BLACK, (u8 *)ATK_8266_GetSSID());

	UI_ShowText(5, 82, GRAY, BLACK, "IP:");
	UI_ShowText(62, 82, WHITE, BLACK, (u8 *)ATK_8266_GetLocalIP());

	UI_ShowText(5, 104, GRAY, BLACK, "Server:");
	UI_ShowText(62, 104, WHITE, BLACK, (u8 *)ATK_8266_GetServerIP());

	UI_ShowText(5, 126, GRAY, BLACK, "Port:");
	UI_ShowText(62, 126, WHITE, BLACK, (u8 *)ATK_8266_GetServerPort());

	if(ATK_8266_IsOnline())
		UI_ShowText(5, 160, GREEN, BLACK, "State: Online");
	else
		UI_ShowText(5, 160, RED, BLACK, "State: Offline");

	image_settings_drawn = 0;
	UI_DrawTabs(UI_KEY_NONE);
}

static void UI_SwitchPage(UI_Page page)
{
	if(page == current_page)
	{
		UI_DrawTabs(UI_KEY_NONE);
		return;
	}
	current_page = page;
	if(page == UI_PAGE_CONTROL)
		UI_DrawControlPage();
	else if(page == UI_PAGE_DATA)
		UI_DrawDataPage();
	else
		UI_DrawNetworkPage();
}

void UI_Init(void)
{
	LCD_Clear(BLACK);
	BACK_COLOR = BLACK;
	current_page = UI_PAGE_CONTROL;
	UI_DrawHeader();
	UI_DrawControlPage();
}

void UI_SetDistance(float distance_cm)
{
	u16 value;
	saved_distance_cm = distance_cm;
	if(current_page != UI_PAGE_CONTROL) return;
	if(distance_cm < 0.0f)
	{
		if(last_distance == 0xFFFE) return;
		last_distance = 0xFFFE;
		LCD_Fill(0, 24, 239, 44, BLACK);
		UI_ShowText(5, 27, LIGHTBLUE, BLACK, "DIST");
		UI_ShowText(55, 27, RED, BLACK, "--- cm");
		return;
	}
	value = (u16)(distance_cm + 0.5f);
	if(value == last_distance) return;
	last_distance = value;
	LCD_Fill(0, 24, 239, 44, BLACK);
	UI_ShowText(5, 27, LIGHTBLUE, BLACK, "DIST");
	POINT_COLOR = LIGHTBLUE;
	BACK_COLOR = BLACK;
	LCD_ShowNum(55, 27, value, 3, 16);
	UI_ShowText(82, 27, LIGHTBLUE, BLACK, "cm");
}

void UI_SetObstacle(u8 present)
{
	if(obstacle_present != present)
	{
		obstacle_present = present;
		if(current_page == UI_PAGE_CONTROL)
			UI_DrawAvoidStatus();
	}
}

u8 UI_IsAvoidEnabled(void)
{
	return avoid_enabled;
}

UI_Page UI_GetPage(void)
{
	return current_page;
}

void UI_SetColorRecognition(u32 red_count, u32 green_count, u8 result)
{
	color_red_count = red_count;
	color_green_count = green_count;
	color_result = result;
	if(current_page == UI_PAGE_DATA)
		UI_DrawColorRecognition();
}

void UI_Task(void)
{
	UI_Key key;
	u8 selected;
	u8 brightness;
	u8 contrast;
	u8 saturation;
	u8 menu_visible;

	/*
	 * 第一部分：图像参数（亮度/对比度/饱和度）处理。
	 * 从按键菜单读取用户设置的图像参数，应用到 OV2640 摄像头，
	 * 并负责在控制页面上绘制/更新/清除对应的参数显示行。
	 */
	if(EXTIX_GetImageSettings(&selected, &brightness,
	                          &contrast, &saturation, &menu_visible))
	{
		/*
		 * 首次应用或数值发生变化时才写寄存器，避免反复操作
		 * OV2640 造成不必要的开销。
		 */
		if(!image_settings_applied || brightness != drawn_brightness)
			OV2640_Brightness(brightness);
		if(!image_settings_applied || contrast != drawn_contrast)
			OV2640_Contrast(contrast);
		if(!image_settings_applied || saturation != drawn_saturation)
			OV2640_Color_Saturation(saturation);
		image_settings_applied = 1;

		/*
		 * 不在控制页面时无需显示图像设置，仅复位绘制标记。
		 */
		if(current_page != UI_PAGE_CONTROL)
		{
			image_settings_drawn = 0;
		}
		/*
		 * 菜单已关闭：若之前画过设置区域则用黑色清除该区域。
		 */
		else if(!menu_visible)
		{
			if(image_settings_drawn)
				LCD_Fill(5, 132, 108, 181, BLACK);
			image_settings_drawn = 0;
		}
		/*
		 * 菜单首次显示：一次性绘制全部三行参数。
		 */
		else if(!image_settings_drawn)
		{
			UI_DrawImageSettings(selected, brightness, contrast, saturation);
			image_settings_drawn = 1;
		}
		/*
		 * 菜单已显示：只重绘“值变化”或“选中项变化”的行，
		 * 减少 LCD 刷新次数。
		 */
		else
		{
			if(brightness != drawn_brightness ||
			   selected == IMAGE_PARAM_BRIGHTNESS ||
			   drawn_image_param == IMAGE_PARAM_BRIGHTNESS)
				UI_DrawImageSettingRow(IMAGE_PARAM_BRIGHTNESS,
				                       selected, brightness);
			if(contrast != drawn_contrast ||
			   selected == IMAGE_PARAM_CONTRAST ||
			   drawn_image_param == IMAGE_PARAM_CONTRAST)
				UI_DrawImageSettingRow(IMAGE_PARAM_CONTRAST,
				                       selected, contrast);
			if(saturation != drawn_saturation ||
			   selected == IMAGE_PARAM_SATURATION ||
			   drawn_image_param == IMAGE_PARAM_SATURATION)
				UI_DrawImageSettingRow(IMAGE_PARAM_SATURATION,
				                       selected, saturation);
		}
		/* 记录本次已绘制的参数状态，供下次对比。 */
		drawn_image_param = selected;
		drawn_brightness = brightness;
		drawn_contrast = contrast;
		drawn_saturation = saturation;
	}

	/* 第二部分：触摸屏扫描与按下状态处理。 */
	tp_dev.scan(0);

	/* 手指按下：进入触摸处理流程。 */
	if(tp_dev.sta & TP_PRES_DOWN)
	{
		/* 按下的第一帧：复位所有触摸状态，等待重新确定目标。 */
		if(!touch_was_down)
		{
			touch_was_down = 1;
			touch_target_locked = 0;
			camera_tap_candidate = 0;
			pressed_key = UI_KEY_NONE;
			nav_candidate = UI_KEY_NONE;
			nav_stable_count = 0;
		}

		/* 目标尚未锁定，先判断手指落在哪个区域。 */
		if(!touch_target_locked)
		{
			/*
			 * 判断是否点中摄像头画面区域（网络页面除外）。
			 * 命中后直接锁定，等待抬起时做“双击缩放”判定。
			 */
			camera_tap_candidate =
				(current_page != UI_PAGE_NETWORK) &&
				(tp_dev.x[0] >= CAMERA_X) &&
				(tp_dev.x[0] < CAMERA_X + CAMERA_WIDTH) &&
				(tp_dev.y[0] >= CAMERA_Y) &&
				(tp_dev.y[0] < CAMERA_Y + CAMERA_HEIGHT);
			if(camera_tap_candidate)
			{
				touch_target_locked = 1;
			}
			else
			{
				/* 未命中摄像头，则按控件区域做命中测试。 */
				key = UI_HitTest(tp_dev.x[0], tp_dev.y[0]);
				/*
				 * 顶部标签页（控制/数据/网络）紧邻触摸屏边缘，
				 * 采用“连续两帧命中同一标签才锁定”的防误触机制。
				 */
				if(key >= UI_KEY_TAB_CTRL)
				{
					if(key == nav_candidate)
						nav_stable_count++;
					else
					{
						nav_candidate = key;
						nav_stable_count = 1;
					}
					if(nav_stable_count >= 2)
					{
						touch_target_locked = 1;
						pressed_key = key;
						UI_DrawPressedKey(key);
					}
				}
				else
				{
					/* 普通按钮或空白区域首次命中后立即锁定。 */
					touch_target_locked = 1;
					pressed_key = key;
					UI_DrawPressedKey(key);
				}
			}
		}
	}
	else
	{
		/* 手指已抬起：结束本次触摸并派发事件。 */
		if(touch_was_down)
		{
			touch_was_down = 0;
			/*
			 * 若本次触摸锁定的目标是摄像头区域，则根据两次
			 * 单击的时间间隔判断是否为双击，双击则切换画面缩放。
			 */
			if(touch_target_locked && camera_tap_candidate)
			{
				u32 now = EXTIX_GetMillis();
				if(camera_tap_pending &&
				   ((now - camera_last_tap_ms) <= UI_DOUBLE_TAP_MS))
				{
					camera_tap_pending = 0;
					CAMERA_ToggleZoom();
				}
				else
				{
					/* 记录第一次单击时刻，等待可能的第二次单击。 */
					camera_tap_pending = 1;
					camera_last_tap_ms = now;
				}
			}
			/* 清除本次触摸的全部临时状态。 */
			camera_tap_candidate = 0;
			touch_target_locked = 0;
			nav_candidate = UI_KEY_NONE;
			nav_stable_count = 0;
		}

		/*
		 * 若有被按下的按键，将按键转换为事件放入事件队列，
		 * 并恢复按键的抬起绘制、执行相应的动作。
		 */
		if(pressed_key != UI_KEY_NONE)
		{
			pending_event = UI_KeyToEvent(pressed_key);
			if(pressed_key >= UI_KEY_UP && pressed_key <= UI_KEY_RIGHT)
				UI_DrawDirectionKey(pressed_key, 0);   /* 方向键：恢复未按下样式 */
			else if(pressed_key == UI_KEY_AVOID)
			{
				/* 避障按钮：切换避障开关并刷新按钮与状态显示。 */
				avoid_enabled = !avoid_enabled;
				UI_DrawAvoidButton(0);
				UI_DrawAvoidStatus();
			}
			else if(pressed_key == UI_KEY_TAB_CTRL)
				UI_SwitchPage(UI_PAGE_CONTROL);   /* 标签页：切换到控制页 */
			else if(pressed_key == UI_KEY_TAB_DATA)
				UI_SwitchPage(UI_PAGE_DATA);      /* 标签页：切换到数据页 */
			else if(pressed_key == UI_KEY_TAB_NET)
				UI_SwitchPage(UI_PAGE_NETWORK);   /* 标签页：切换到网络页 */
			pressed_key = UI_KEY_NONE;            /* 本次按键处理完毕 */
		}
	}
}

UI_Event UI_GetEvent(void)
{
	UI_Event event = pending_event;
	pending_event = UI_EVENT_NONE;
	return event;
}
