#include "host_shim.h"
#include <stdarg.h>

volatile uint32_t host_gpio_f = 0;
DMA_Stream_TypeDef host_dma2_stream1 = {0};

volatile u8  jpeg_data_ok = 0;
volatile u32 jpeg_data_len = 0;
volatile u32 jpeg_last_frame_bytes = 0;

#define T_MAX_LINES 256
#define T_LINE_LEN  192

static char g_lines[T_MAX_LINES][T_LINE_LEN];
static int g_line_count;
static u32 g_now_ms;

int t_motor_forward_calls;
int t_motor_forward_dual_calls;
int t_motor_backward_calls;
int t_motor_turn_left_calls;
int t_motor_turn_right_calls;
int t_motor_stop_calls;
u16 t_motor_last_left;
u16 t_motor_last_right;
u16 t_motor_last_speed;

static u8 g_ui_confirm = UI_CONFIRM_NONE;
static u32 g_wdg_ticks;

void t_reset(void)
{
    g_line_count = 0;
    g_now_ms = 0;
    g_ui_confirm = UI_CONFIRM_NONE;
    g_wdg_ticks = 0;
    t_motor_forward_calls = 0;
    t_motor_forward_dual_calls = 0;
    t_motor_backward_calls = 0;
    t_motor_turn_left_calls = 0;
    t_motor_turn_right_calls = 0;
    t_motor_stop_calls = 0;
    t_motor_last_left = 0;
    t_motor_last_right = 0;
    t_motor_last_speed = 0;
}

void t_set_ms(u32 ms) { g_now_ms = ms; }
void t_advance_ms(u32 ms) { g_now_ms += ms; }
u32 t_now_ms(void) { return g_now_ms; }
int t_line_count(void) { return g_line_count; }

const char *t_line(int index)
{
    if (index < 0 || index >= g_line_count)
        return "";
    return g_lines[index];
}

int t_has(const char *prefix)
{
    int i;
    size_t n = strlen(prefix);
    for (i = 0; i < g_line_count; i++)
    {
        if (strncmp(g_lines[i], prefix, n) == 0)
            return 1;
    }
    return 0;
}

int t_count_prefix(const char *prefix)
{
    int i, count = 0;
    size_t n = strlen(prefix);
    for (i = 0; i < g_line_count; i++)
    {
        if (strncmp(g_lines[i], prefix, n) == 0)
            count++;
    }
    return count;
}

void u3_printf(char *fmt, ...)
{
    char tmp[T_LINE_LEN];
    va_list ap;
    int n;

    if (fmt == 0)
        return;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    n = (int)strlen(tmp);
    while (n > 0 && (tmp[n - 1] == '\r' || tmp[n - 1] == '\n'))
        tmp[--n] = 0;

    if (g_line_count < T_MAX_LINES)
    {
        strncpy(g_lines[g_line_count], tmp, T_LINE_LEN - 1);
        g_lines[g_line_count][T_LINE_LEN - 1] = 0;
        g_line_count++;
    }
}

u32 EXTIX_GetMillis(void) { return g_now_ms; }
u32 DWT_Micros(void) { return g_now_ms * 1000U; }

void Adc_ReadLine(u16 *l, u16 *m, u16 *r)
{
    if (l) *l = adcL;
    if (m) *m = adcM;
    if (r) *r = adcR;
}

u8 HC_SR04_Start(void) { return 1; }
u8 HC_SR04_Poll(float *distance)
{
    if (distance) *distance = -1.0f;
    return 0;
}
u32 HC_SR04_GetTimeouts(void) { return 0; }

void MOTOR_Forward(u16 speed) { t_motor_forward_calls++; t_motor_last_speed = speed; }
void MOTOR_ForwardDual(u16 left, u16 right)
{
    t_motor_forward_dual_calls++;
    t_motor_last_left = left;
    t_motor_last_right = right;
}
void MOTOR_Backward(u16 speed) { t_motor_backward_calls++; t_motor_last_speed = speed; }
void MOTOR_TurnLeft(u16 speed) { t_motor_turn_left_calls++; t_motor_last_speed = speed; }
void MOTOR_TurnRight(u16 speed) { t_motor_turn_right_calls++; t_motor_last_speed = speed; }
void MOTOR_Stop(void) { t_motor_stop_calls++; }

void UI_Task(void) { }
void UI_SetDistance(float distance_cm) { (void)distance_cm; }
void UI_SetObstacle(u8 present) { (void)present; }
u8 UI_IsAvoidEnabled(void) { return 1; }
u8 UI_GetConfirmRequested(void) { return g_ui_confirm; }
void UI_ClearConfirmRequest(void) { g_ui_confirm = UI_CONFIRM_NONE; }
void UI_SetTaskConfirm(u8 action) { (void)action; }
void UI_SetLineSensors(u16 l, u16 m, u16 r) { (void)l; (void)m; (void)r; }
void UI_SetMotionState(const char *state, u16 pwmval) { (void)state; (void)pwmval; }

void CAMERA_Task(u32 now_ms) { (void)now_ms; }
u8 CAMERA_IsOccluded(void) { return 0; }

void WIFI_LinkOnRxLine(const char *line) { (void)line; }
void WIFI_LinkPoll(void) { }
u8 WIFI_IsReconnecting(void) { return 0; }

void USART3_TextPump(void) { }
u16 USART3_TextOverflow(void) { return 0; }
u32 USART3_GetRxOverflow(void) { return 0; }
u32 USART3_GetRxOre(void) { return 0; }
u16 USART3_GetRxPending(void) { return 0; }
u32 JPEG_TxDropped(void) { return 0; }

u8 USART3_DMA_IsBusy(void) { return 0; }
int jpeg_process_and_send(void) { return 0; }

void WDG_Feed(void) { }
void WDG_TaskTick(u8 id) { (void)id; g_wdg_ticks++; }
u8 WDG_CheckAndFeed(void) { g_wdg_ticks++; return 1; }
u32 WDG_GetMissed(void) { return 0; }

char* atk_8266_at_response_rdCmd(u8 mode) { (void)mode; return 0; }

void DMA_SetCurrDataCounter(DMA_Stream_TypeDef *stream, uint16_t count) { stream->NDTR = count; }
void DMA_Cmd(DMA_Stream_TypeDef *stream, int state) { (void)stream; (void)state; }
void DCMI_CaptureCmd(int state) { (void)state; }
