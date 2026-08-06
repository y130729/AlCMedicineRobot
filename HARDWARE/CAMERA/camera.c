#include "camera.h"
#include "ov2640.h"
#include "dcmi.h"
#include "lcd.h"
#include "exti.h"
#include "ui.h"
#include "wifi_transfer.h"

#define CAMERA_REFRESH_MS 100
#define COLOR_RECOGNITION_MS 250
#define OCCLUSION_GRAD_THRESHOLD 3
#define OCCLUSION_VAR_THRESHOLD 30
#define OCCLUSION_CLEAR_GRAD_THRESHOLD 6
#define OCCLUSION_CLEAR_VAR_THRESHOLD 60
#define OCCLUSION_CONFIRM_FRAMES 2
#define OCCLUSION_CLEAR_FRAMES 10
#define JPEG_OCCLUSION_SIZE_THRESHOLD 1000

static u16 camera_frame[CAMERA_HEIGHT][CAMERA_WIDTH];
static u8 camera_ready = 0;
static u8 displayed_frame = 0;
static u32 next_refresh_ms = 0;
static u32 next_recognition_ms = 0;
static u32 camera_red_count = 0;
static u32 camera_green_count = 0;
static CAMERA_ColorResult camera_color_result = CAMERA_COLOR_UNCLEAR;
static u8 camera_zoomed = 0;
static u16 zoom_line[CAMERA_WIDTH];
static u16 zoom_source_x[CAMERA_WIDTH];
static u32 occlusion_average = 0;
static u32 occlusion_variance = 0;
static u32 occlusion_gradient = 0;
static u32 filtered_occlusion_variance = 0;
static u32 filtered_occlusion_gradient = 0;
static u8 occlusion_filter_ready = 0;
static u8 camera_occluded = 0;
static u8 occlusion_count = 0;
static u8 occlusion_clear_count = 0;
static u8 camera_jpeg_mode = 0;
static u8 jpeg_occlusion_count = 0;
static u8 jpeg_occlusion_clear_count = 0;

static void CAMERA_DetectOcclusion(void)
{
	u32 sum = 0;
	u32 square_sum = 0;
	u32 gradient_sum = 0;
	u32 count = 0;
	u16 row;
	u16 col;
	u16 pixel;
	u16 next_pixel;
	u8 gray;
	u8 next_gray;
	u8 raw_occluded;
	u8 raw_clear;

	/* 每隔一个像素采样，沿用用户提供的灰度、方差和横向梯度算法。 */
	for(row = 0; row < CAMERA_HEIGHT; row += 2)
	{
		for(col = 0; col < CAMERA_WIDTH; col += 2)
		{
			pixel = camera_frame[row][col];
			gray = (u8)((((pixel >> 11) & 0X1F) * 77 +
			             ((pixel >> 5) & 0X3F) * 150 +
			             (pixel & 0X1F) * 29) >> 8);
			sum += gray;
			square_sum += (u32)gray * gray;

			if(col + 2 < CAMERA_WIDTH)
			{
				next_pixel = camera_frame[row][col + 2];
				next_gray = (u8)((((next_pixel >> 11) & 0X1F) * 77 +
				                  ((next_pixel >> 5) & 0X3F) * 150 +
				                  (next_pixel & 0X1F) * 29) >> 8);
				gradient_sum += gray > next_gray ?
				                gray - next_gray : next_gray - gray;
			}
			count++;
		}
	}

	if(count == 0) return;
	occlusion_average = sum / count;
	occlusion_variance = square_sum / count -
	                     occlusion_average * occlusion_average;
	occlusion_gradient = gradient_sum / count;
	if(!occlusion_filter_ready)
	{
		filtered_occlusion_variance = occlusion_variance;
		filtered_occlusion_gradient = occlusion_gradient;
		occlusion_filter_ready = 1;
	}
	else
	{
		/* 1/4 新数据 + 3/4 历史数据，抑制曝光和单帧噪声波动。 */
		filtered_occlusion_variance =
			(filtered_occlusion_variance * 3 + occlusion_variance) / 4;
		filtered_occlusion_gradient =
			(filtered_occlusion_gradient * 3 + occlusion_gradient) / 4;
	}

	/* 进入遮挡使用当前帧，保证快速停车；平滑值只用于稳定解除。 */
	raw_occluded =
		(occlusion_gradient < OCCLUSION_GRAD_THRESHOLD) &&
		(occlusion_variance < OCCLUSION_VAR_THRESHOLD);
	raw_clear =
		(filtered_occlusion_gradient >= OCCLUSION_CLEAR_GRAD_THRESHOLD) ||
		(filtered_occlusion_variance >= OCCLUSION_CLEAR_VAR_THRESHOLD);

	if(!camera_occluded)
	{
		occlusion_clear_count = 0;
		if(raw_occluded)
		{
			if(occlusion_count < OCCLUSION_CONFIRM_FRAMES)
				occlusion_count++;
		}
		else
		{
			occlusion_count = 0;
		}
		if(occlusion_count >= OCCLUSION_CONFIRM_FRAMES)
		{
			camera_occluded = 1;
			occlusion_clear_count = 0;
		}
	}
	else
	{
		occlusion_count = 0;
		if(raw_clear)
		{
			if(occlusion_clear_count < OCCLUSION_CLEAR_FRAMES)
				occlusion_clear_count++;
		}
		else
		{
			occlusion_clear_count = 0;
		}
		if(occlusion_clear_count >= OCCLUSION_CLEAR_FRAMES)
		{
			camera_occluded = 0;
			occlusion_clear_count = 0;
		}
	}
}

static void CAMERA_DetectJpegOcclusion(void)
{
	u8 occluded;

	occluded = (jpeg_last_frame_bytes > 0) &&
	           (jpeg_last_frame_bytes < JPEG_OCCLUSION_SIZE_THRESHOLD);

	if(!camera_occluded)
	{
		jpeg_occlusion_clear_count = 0;
		if(occluded)
		{
			if(jpeg_occlusion_count < OCCLUSION_CONFIRM_FRAMES)
				jpeg_occlusion_count++;
		}
		else
		{
			jpeg_occlusion_count = 0;
		}
		if(jpeg_occlusion_count >= OCCLUSION_CONFIRM_FRAMES)
		{
			camera_occluded = 1;
			jpeg_occlusion_clear_count = 0;
		}
	}
	else
	{
		jpeg_occlusion_count = 0;
		if(!occluded)
		{
			if(jpeg_occlusion_clear_count < OCCLUSION_CLEAR_FRAMES)
				jpeg_occlusion_clear_count++;
		}
		else
		{
			jpeg_occlusion_clear_count = 0;
		}
		if(jpeg_occlusion_clear_count >= OCCLUSION_CLEAR_FRAMES)
		{
			camera_occluded = 0;
			jpeg_occlusion_clear_count = 0;
		}
	}
}

void CAMERA_SetJpegMode(u8 enable)
{
	camera_jpeg_mode = enable;
	camera_occluded = 0;
	occlusion_count = 0;
	occlusion_clear_count = 0;
	jpeg_occlusion_count = 0;
	jpeg_occlusion_clear_count = 0;
}

u8 CAMERA_IsOccluded(void)
{
	return camera_occluded;
}

void CAMERA_GetOcclusionStats(u32 *average, u32 *variance, u32 *gradient)
{
	*average = occlusion_average;
	*variance = occlusion_variance;
	*gradient = occlusion_gradient;
}

void CAMERA_ToggleZoom(void)
{
	camera_zoomed = !camera_zoomed;
}

u8 CAMERA_IsZoomed(void)
{
	return camera_zoomed;
}

static void CAMERA_DisplayZoomed(u8 avoid_menu)
{
	u16 row;
	u16 col;
	u16 source_row;
	const u16 crop_height = 93;
	const u16 crop_y = (CAMERA_HEIGHT - crop_height) / 2;

	for(row = 0; row < CAMERA_HEIGHT; row++)
	{
		source_row = crop_y +
		             (u16)(((u32)row * crop_height) / CAMERA_HEIGHT);
		for(col = 0; col < CAMERA_WIDTH; col++)
			zoom_line[col] = camera_frame[source_row][zoom_source_x[col]];

		if(avoid_menu && row >= 85 && row <= 134)
		{
			LCD_Color_Fill(109, CAMERA_Y + row,
			               CAMERA_X + CAMERA_WIDTH - 1, CAMERA_Y + row,
			               &zoom_line[107]);
		}
		else
		{
			LCD_Color_Fill(CAMERA_X, CAMERA_Y + row,
			               CAMERA_X + CAMERA_WIDTH - 1, CAMERA_Y + row,
			               zoom_line);
		}
	}
}

static void CAMERA_RecognizeColor(void)
{
	u16 row;
	u16 col;
	u16 pixel;
	u8 red;
	u8 green;
	u8 blue;
	u32 red_count = 0;
	u32 green_count = 0;

	/* 沿用 red_green_recognition_temp 的 RGB565 阈值识别逻辑。 */
	for(row = 0; row < CAMERA_HEIGHT; row++)
	{
		for(col = 0; col < CAMERA_WIDTH; col++)
		{
			pixel = camera_frame[row][col];
			red = (u8)(((pixel & 0XF800) >> 11) << 3);
			green = (u8)(((pixel & 0X07E0) >> 5) << 2);
			blue = (u8)((pixel & 0X001F) << 3);
			if(red > 150 && green < 100 && blue < 100)
				red_count++;
			else if(green > 150 && red < 100 && blue < 100)
				green_count++;
		}
	}

	camera_red_count = red_count;
	camera_green_count = green_count;
	if(red_count * 5 > green_count * 6)
		camera_color_result = CAMERA_COLOR_RED;
	else if(green_count * 5 > red_count * 6)
		camera_color_result = CAMERA_COLOR_GREEN;
	else
		camera_color_result = CAMERA_COLOR_UNCLEAR;
	UI_SetColorRecognition(camera_red_count, camera_green_count,
	                       (u8)camera_color_result);
}

void CAMERA_GetColorRecognition(u32 *red_count, u32 *green_count,
	                            CAMERA_ColorResult *result)
{
	*red_count = camera_red_count;
	*green_count = camera_green_count;
	*result = camera_color_result;
}

static void CAMERA_ShowMessage(const char *message, u16 color)
{
	LCD_Fill(CAMERA_X, CAMERA_Y,
	         CAMERA_X + CAMERA_WIDTH - 1,
	         CAMERA_Y + CAMERA_HEIGHT - 1, BLACK);
	POINT_COLOR = color;
	BACK_COLOR = BLACK;
	LCD_ShowString(82, 108, 100, 16, 16, (u8 *)message);
}

u8 CAMERA_Init(void)
{
	u8 result;
	u16 col;
	const u16 crop_width = 157;
	const u16 crop_x = (CAMERA_WIDTH - crop_width) / 2;

	CAMERA_ShowMessage("CAM INIT", LIGHTBLUE);
	result = OV2640_Init();
	if(result != 0)
	{
		CAMERA_ShowMessage("CAM ERR", RED);
		LCD_ShowNum(150, 108, result, 1, 16);
		return result;
	}

	OV2640_RGB565_Mode();
	result = OV2640_OutSize_Set(CAMERA_WIDTH, CAMERA_HEIGHT);
	if(result != 0)
	{
		CAMERA_ShowMessage("SIZE ERR", RED);
		return (u8)(0x10 + result);
	}

	My_DCMI_Init();
	DCMI_DMA_Init((u32)camera_frame,
	              (u16)(sizeof(camera_frame) / 4),
	              DMA_MemoryDataSize_HalfWord,
	              DMA_MemoryInc_Enable);
	ov_frame = 0;
	displayed_frame = 0;
	camera_ready = 1;
	DCMI_Start();
	for(col = 0; col < CAMERA_WIDTH; col++)
		zoom_source_x[col] = crop_x +
		                     (u16)(((u32)col * crop_width) / CAMERA_WIDTH);
	CAMERA_ShowMessage("CAM WAIT", LIGHTBLUE);
	return 0;
}

void CAMERA_Task(u32 now_ms)
{
	u8 frame;
	u16 row;

	if(!camera_ready || ((s32)(now_ms - next_refresh_ms) < 0))
		return;

	frame = ov_frame;
	if(frame == displayed_frame)
		return;

	displayed_frame = frame;
	next_refresh_ms = now_ms + CAMERA_REFRESH_MS;
	if(camera_jpeg_mode)
	{
		CAMERA_DetectJpegOcclusion();
	}
	else
	{
		CAMERA_DetectOcclusion();
		if((s32)(now_ms - next_recognition_ms) >= 0)
		{
			CAMERA_RecognizeColor();
			next_recognition_ms = now_ms + COLOR_RECOGNITION_MS;
		}
	}

	if(camera_jpeg_mode)
	{
		static u8 jpeg_status_shown = 0;
		if(!jpeg_status_shown)
		{
			CAMERA_ShowMessage("JPEG TX", LIGHTBLUE);
			jpeg_status_shown = 1;
		}
		return;
	}

	if(UI_GetPage() == UI_PAGE_NETWORK)
		return;
	if(camera_zoomed)
	{
		CAMERA_DisplayZoomed((UI_GetPage() == UI_PAGE_CONTROL) &&
		                     EXTIX_IsImageMenuVisible());
	}
	else if((UI_GetPage() != UI_PAGE_CONTROL) ||
	        !EXTIX_IsImageMenuVisible())
	{
		LCD_Color_Fill(CAMERA_X, CAMERA_Y,
		               CAMERA_X + CAMERA_WIDTH - 1,
		               CAMERA_Y + CAMERA_HEIGHT - 1,
		               (u16 *)camera_frame);
	}
	else
	{
		/* 菜单显示时，摄像头刷新绕开 x=5..108、y=132..181。 */
		LCD_Color_Fill(CAMERA_X, CAMERA_Y,
		               CAMERA_X + CAMERA_WIDTH - 1, 131,
		               (u16 *)camera_frame);
		for(row = 85; row < CAMERA_HEIGHT; row++)
		{
			LCD_Color_Fill(109, CAMERA_Y + row,
			               CAMERA_X + CAMERA_WIDTH - 1, CAMERA_Y + row,
			               &camera_frame[row][107]);
		}
	}
}
