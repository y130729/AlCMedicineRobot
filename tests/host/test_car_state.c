/*
 * Host-side unit tests for the real task state machine and command dispatch.
 *
 * car_app.c is included with the "static" storage class removed so the test
 * can drive CAR_StepTask() directly. The source file itself is not modified.
 * Hardware calls are satisfied by tests/host/stubs.c.
 */

#include "host_shim.h"

#define static
#include "car_app.c"
#undef static

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                           \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

#define CHECK_HAS(prefix)                                                     \
    do {                                                                      \
        g_checks++;                                                           \
        if (!t_has(prefix)) {                                                 \
            g_failures++;                                                     \
            printf("FAIL %s:%d: missing output '%s'\n",                       \
                   __FILE__, __LINE__, prefix);                               \
        }                                                                     \
    } while (0)

static void fresh(CAR_Ctx *ctx)
{
    t_reset();
    memset(&g_task, 0, sizeof(g_task));
    g_car_state = CAR_STATE_STOP;
    memset(ctx, 0, sizeof(*ctx));
    CAR_CtxInit(ctx);
    adcL = 100;
    adcM = 100;
    adcR = 100;
}

static int dispatch(CAR_Ctx *ctx, const char *text)
{
    char buf[160];
    u8 enabled = ctx->line_enabled;
    int handled;

    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    handled = (int)wifi_dispatch_commands(buf, &ctx->pwmval, &enabled);
    ctx->line_enabled = enabled;
    return handled;
}

static void step(CAR_Ctx *ctx, u32 ms)
{
    t_set_ms(ms);
    ctx->now_ms = ms;
    CAR_StepTask(ctx);
}

static void arm_and_enter(CAR_Ctx *ctx, u32 t0)
{
    adcL = 100;
    adcM = 100;
    adcR = 100;
    step(ctx, t0);             /* band clear -> armed */
    adcL = 3000;
    adcM = 3000;
    adcR = 3000;
    step(ctx, t0 + 5);
    step(ctx, t0 + 10);
    step(ctx, t0 + 15);
    step(ctx, t0 + 20);        /* 4 samples -> ENTER_STATION */
}

static void test_remote_dispatch(void)
{
    CAR_Ctx ctx;

    fresh(&ctx);
    CHECK(dispatch(&ctx, "forWard") == 1);
    CHECK(t_motor_forward_calls == 1);
    CHECK(t_motor_last_speed == 300);

    fresh(&ctx);
    CHECK(dispatch(&ctx, "forWardX") == 0);
    CHECK(t_motor_forward_calls == 0);

    fresh(&ctx);
    CHECK(dispatch(&ctx, "forWard\r\nletStop\r\n") == 1);
    CHECK(t_motor_forward_calls == 1);
    CHECK(t_motor_stop_calls == 1);

    fresh(&ctx);
    CHECK(dispatch(&ctx, "speedUp") == 1);
    CHECK(ctx.pwmval == 350);
    CHECK(dispatch(&ctx, "speedDown") == 1);
    CHECK(ctx.pwmval == 300);
    ctx.pwmval = 500;
    CHECK(dispatch(&ctx, "speedUp") == 1);
    CHECK(ctx.pwmval == 500);
    ctx.pwmval = 50;
    CHECK(dispatch(&ctx, "speedDown") == 1);
    CHECK(ctx.pwmval == 50);
}

static void test_task_start_and_idempotency(void)
{
    CAR_Ctx ctx;

    fresh(&ctx);
    CHECK(dispatch(&ctx, "TASK_START,T1,S1,S2,seq=1") == 1);
    CHECK(ctx.line_enabled == 1);
    CHECK(g_task.state == TASK_NAV_TO_LOAD);
    CHECK(strcmp(g_task.task_id, "T1") == 0);
    CHECK(strcmp(g_task.from_station, "S1") == 0);
    CHECK(strcmp(g_task.to_station, "S2") == 0);
    CHECK_HAS("ACK,T1,1,OK,seq=1");
    CHECK_HAS("STATUS,T1,NAV");
    CHECK(t_count_prefix("STATUS,T1,NAV") == 1);

    t_reset();
    CHECK(dispatch(&ctx, "TASK_START,T1,S1,S2,seq=1") == 1);
    CHECK_HAS("ACK,T1,1,DUP,seq=1");
    CHECK(t_count_prefix("STATUS,") == 0);

    CHECK(dispatch(&ctx, "TASK_START,T1,S1,S2,seq=2") == 1);
    CHECK_HAS("ACK,T1,0,BUSY,seq=2");

    t_set_ms(30001);
    CHECK(dispatch(&ctx, "TASK_START,T1,S1,S2,seq=2") == 1);
    CHECK_HAS("ACK,T1,0,BUSY,seq=2");
    CHECK(t_count_prefix("ACK,T1,0,DUP") == 0);

    /* Only one station field after the task id: the parser must reject it. */
    CHECK(dispatch(&ctx, "TASK_START,T1,seq=3") == 1);
    CHECK_HAS("ACK,T1,0,BADREQ,seq=3");
}

static void test_station_flow_and_confirm(void)
{
    CAR_Ctx ctx;

    fresh(&ctx);
    CHECK(dispatch(&ctx, "TASK_START,T1,S1,S2,seq=1") == 1);
    arm_and_enter(&ctx, 1000);
    CHECK(g_task.state == TASK_APPROACH_LOAD);
    CHECK(g_task.waiting_stop == 1);
    CHECK_HAS("ENTER_STATION,T1,0");
    CHECK_HAS("STATUS,T1,APPROACH");

    t_reset();
    CHECK(dispatch(&ctx, "STOP,T1,S2,seq=9") == 1);
    CHECK_HAS("ACK,T1,0,STATE,seq=9");
    CHECK(dispatch(&ctx, "STOP,T1,S1,seq=10") == 1);
    CHECK_HAS("ACK,T1,1,OK,seq=10");
    CHECK_HAS("STATUS,T1,STOPPED");
    CHECK_HAS("TASK_ARRIVE,T1,S1");
    CHECK(g_task.state == TASK_WAIT_LOAD);

    t_reset();
    CHECK(dispatch(&ctx, "STOP,T1,S1,seq=10") == 1);
    CHECK_HAS("ACK,T1,1,DUP,seq=10");
    CHECK(t_count_prefix("TASK_ARRIVE") == 0);

    t_reset();
    CHECK(dispatch(&ctx, "TASK_CONFIRM,T1,load,ui,seq=11") == 1);
    CHECK_HAS("ACK,T1,1,OK,seq=11");
    CHECK_HAS("STATUS,T1,NAV");
    CHECK(g_task.state == TASK_NAV_TO_WARD);
    CHECK(g_task.seq == 1);
    CHECK(ctx.line_enabled == 1);

    arm_and_enter(&ctx, 2000);
    CHECK_HAS("ENTER_STATION,T1,1");
    CHECK(dispatch(&ctx, "STOP,T1,S2,seq=12") == 1);
    CHECK_HAS("TASK_ARRIVE,T1,S2");
    CHECK(g_task.state == TASK_WAIT_UNLOAD);

    CHECK(dispatch(&ctx, "TASK_CONFIRM,T1,unload,ui,seq=13") == 1);
    CHECK_HAS("TASK_DONE,T1");
    CHECK(g_task.state == TASK_IDLE);
    CHECK(g_task.task_id[0] == 0);
    CHECK(ctx.line_enabled == 0);
}

static void test_stop_fallback(void)
{
    CAR_Ctx ctx;

    fresh(&ctx);
    CHECK(dispatch(&ctx, "TASK_START,T2,S3,S4,seq=30") == 1);
    arm_and_enter(&ctx, 1000);
    step(&ctx, 3021);          /* 2 s after ENTER_STATION, no STOP */
    CHECK_HAS("TASK_ARRIVE,T2,S3,FALLBACK");
    CHECK_HAS("STATUS,T2,TIMEOUT");
    CHECK(g_task.state == TASK_WAIT_LOAD);

    CHECK(dispatch(&ctx, "TASK_CONFIRM,T2,load,ui,seq=31") == 1);
    CHECK(g_task.state == TASK_NAV_TO_WARD);
}

static void test_line_lost_timeout(void)
{
    CAR_Ctx ctx;

    fresh(&ctx);
    CHECK(dispatch(&ctx, "TASK_START,T3,S1,S2,seq=40") == 1);
    step(&ctx, 1000);
    CHECK(g_task.armed == 1);
    step(&ctx, 9001);          /* all white for > 8 s */
    CHECK_HAS("STATUS,T3,LINE_LOST");
    CHECK(g_task.state == TASK_LINE_LOST);
    CHECK(t_motor_stop_calls > 0);

    CHECK(dispatch(&ctx, "TASK_CANCEL,T3,seq=41") == 1);
    CHECK_HAS("STATUS,T3,CANCELLED");
    CHECK(g_task.state == TASK_IDLE);
}

static void test_pause_resume_cancel(void)
{
    CAR_Ctx ctx;

    fresh(&ctx);
    CHECK(dispatch(&ctx, "TASK_START,T4,S1,S2,seq=50") == 1);
    CHECK(dispatch(&ctx, "TASK_PAUSE,T4,seq=51") == 1);
    CHECK_HAS("STATUS,T4,PAUSED");
    CHECK(g_task.state == TASK_PAUSED);
    CHECK(t_motor_stop_calls > 0);

    CHECK(dispatch(&ctx, "TASK_RESUME,T4,seq=52") == 1);
    CHECK_HAS("STATUS,T4,NAV");
    CHECK(g_task.state == TASK_NAV_TO_LOAD);

    CHECK(dispatch(&ctx, "TASK_CANCEL,T4,seq=53") == 1);
    CHECK_HAS("STATUS,T4,CANCELLED");
    CHECK(g_task.state == TASK_IDLE);
    CHECK(ctx.line_enabled == 0);
}

static void test_heartbeat(void)
{
    CAR_Ctx ctx;

    fresh(&ctx);
    CHECK(dispatch(&ctx, "TASK_START,T5,S1,S2,seq=60") == 1);
    t_reset();
    step(&ctx, 1000);
    CHECK_HAS("HEARTBEAT,100,NAV");
}

static void test_pure_helpers(void)
{
    u16 values[5] = {5, 1, 3, 2, 4};
    char args1[] = "A,BC,D";
    char args2[] = "A,,D";
    char args3[] = "A,123456789";
    char out[8];
    char state[12];

    CHECK(CAR_Percentile(values, 5, 0) == 1);
    CHECK(CAR_Percentile(values, 5, 50) == 3);
    CHECK(CAR_Percentile(values, 5, 99) == 5);

    CHECK(CAR_TaskParseField(args1, 1, out, sizeof(out)) == 0);
    CHECK(strcmp(out, "BC") == 0);
    CHECK(CAR_TaskParseField(args2, 1, out, sizeof(out)) == 1);
    CHECK(CAR_TaskParseField(args3, 1, out, 4) == 0);
    CHECK(strcmp(out, "123") == 0);
    CHECK(CAR_TaskParseSeq("T1,S1,S2,seq=42") == 42);
    CHECK(CAR_TaskParseSeq("no-seq") == 0);

    g_car_state = CAR_STATE_STOP;
    CAR_GetState(state);
    CHECK(strcmp(state, "STOP") == 0);
    g_car_state = CAR_STATE_FORWARD;
    CAR_GetState(state);
    CHECK(strcmp(state, "FORWARD") == 0);
}

int main(void)
{
    test_remote_dispatch();
    test_task_start_and_idempotency();
    test_station_flow_and_confirm();
    test_stop_fallback();
    test_line_lost_timeout();
    test_pause_resume_cancel();
    test_heartbeat();
    test_pure_helpers();

    if (g_failures != 0)
    {
        printf("HOST_TESTS_FAILED checks=%d failures=%d\n", g_checks, g_failures);
        return 1;
    }
    printf("HOST_TESTS_OK checks=%d\n", g_checks);
    return 0;
}
