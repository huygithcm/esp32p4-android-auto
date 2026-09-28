/* Host tests use independent literal big-endian protocol fixtures, not the
 * production buffer append functions. Whole production translation units are
 * linked; only platform/task/transmit boundaries below are replaced. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "vesc_can/comm_can.h"
#include "vesc_can/vesc_io_data.h"
#include "vesc_can/vesc_rt_data.h"

static uint32_t now_ms = 1000;
static int failures;
static uint32_t sent_mask;
static uint8_t sent_target;
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); failures++; \
} } while (0)
#define CLOSE(actual, expected) CHECK(fabsf((actual) - (expected)) < 0.0002f)

int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
                                 uint32_t stack, void *arg, unsigned priority,
                                 TaskHandle_t *handle, int core)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    (void)handle; (void)core; return pdPASS;
}
void comm_can_send_buffer_sync(uint8_t id, const uint8_t *data, unsigned len,
                               uint8_t send, uint32_t timeout)
{
    (void)send; (void)timeout;
    if (len==5 && data[0]==51) {
        sent_target=id;
        sent_mask=((uint32_t)data[1]<<24)|((uint32_t)data[2]<<16)|((uint32_t)data[3]<<8)|data[4];
    }
}
void vesc_lisp_poll_once_loop(void) {}
void vesc_lisp_panel_dash_loop(void) {}
void vesc_lisp_panel_poll_loop(void) {}
void vesc_ride_mode_poll_loop(void) {}
void vesc_lisp_panel_pas_loop(void) {}

static void dispatch(const uint8_t *data, unsigned len)
{
    vesc_rt_data_process_response(data, len);
    vesc_io_data_process_response(data, len);
}

static void seed_temperatures(void)
{
    /* Command 51, mask FET + motor, 30.0 C and 12.5 C. */
    static const uint8_t packet[] = {51, 0,0,0,3, 0x01,0x2c, 0x00,0x7d};
    dispatch(packet, sizeof(packet));
    CLOSE(vesc_rt_data_get_latest()->temp_motor, 12.5f);
}

static void video_temperature_values(void)
{
    /* Values selected from the video: display truncates toward integer C.
     * -99.9 is a representative encoded value displaying -99, not a raw
     * packet captured from the hardware. */
    static const uint8_t motor_bytes[][2] = {
        {0xfc,0x19}, {0x00,0x14}, {0x00,0x0a}, {0xff,0x56}
    };
    static const float expected[] = {-99.9f, 2.0f, 1.0f, -17.0f};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        uint8_t packet[] = {51, 0,0,0,3, 0x01,0x2c, 0,0};
        packet[7] = motor_bytes[i][0]; packet[8] = motor_bytes[i][1];
        now_ms += 100;
        dispatch(packet, sizeof(packet));
        const vesc_setup_values_t *rt = vesc_rt_data_get_latest();
        CLOSE(rt->temp_mos, 30.0f);
        CLOSE(rt->temp_motor, expected[i]);
        CHECK(rt->rx_time == now_ms);
        CHECK(vesc_rt_data_is_fresh());
    }
}

static void adc_does_not_change_temperature(void)
{
    seed_temperatures();
    const vesc_setup_values_t before = *vesc_rt_data_get_latest();
    static const uint8_t adc[][17] = {
        {32, 0,0,0,0, 0,0x0f,0x42,0x40, 0,0,0,0, 0,0,0,0},
        {32, 0,0x07,0xa1,0x20, 0,0x1e,0x84,0x80, 0,0,0,0, 0,0,0,0},
        {32, 0,0x0f,0x42,0x40, 0,0x2d,0xc6,0xc0, 0,0x03,0xd0,0x90, 0,0x16,0xe3,0x60}
    };
    for (unsigned i = 0; i < 3; ++i) {
        now_ms += 100;
        dispatch(adc[i], sizeof(adc[i]));
        CHECK(memcmp(&before, vesc_rt_data_get_latest(), sizeof(before)) == 0);
        CLOSE(vesc_io_data_get_latest()->adc1, (float)i * 0.5f);
        CLOSE(vesc_io_data_get_latest()->adc1_voltage, (float)i + 1.0f);
    }
    CLOSE(vesc_io_data_get_latest()->adc2, 0.25f);
    CLOSE(vesc_io_data_get_latest()->adc2_voltage, 1.5f);
}

static void selective_motor_only(void)
{
    seed_temperatures();
    /* No FET field: motor starts directly after the mask. */
    const uint8_t packet[] = {51, 0,0,0,2, 0xff,0x56};
    dispatch(packet, sizeof(packet));
    CLOSE(vesc_rt_data_get_latest()->temp_mos, 30.0f);
    CLOSE(vesc_rt_data_get_latest()->temp_motor, -17.0f);
}

static void full_setup_layout(void)
{
    /* Official command 47 has no echoed mask. All fields through uptime
     * follow commands.c bits 0..21. Includes signed currents and tail data. */
    const uint8_t packet[] = {
        47, 0x01,0x2c, 0xff,0x56, /* FET30, motor-17 */
        0xff,0xff,0xff,0x83, 0,0,0,0xfa, /* motor current -1.25, input 2.5 */
        0,0x7d, 0,0,0x04,0xd2, /* duty .125, rpm1234 */
        0,0,0x09,0xc4, 0x01,0xe0, 0x02,0xee, /* 2.5m/s,48V,.75 battery */
        0,0,0x27,0x10, 0,0,0,0, /* Ah1, charged0 */
        0,0x01,0x86,0xa0, 0,0,0,0, /* Wh10, charged0 */
        0,0,0x03,0xe8, 0,0,0x07,0xd0, /* distance1, abs2 */
        0,0x07,0xa1,0x20, /* position .5 */
        0,10,1, /* no fault, id10, one VESC */
        0,0x01,0x86,0xa0, /* battery100Wh */
        0,0,0x04,0xd2, 0,0x01,0xe2,0x40 /* odometer1234, uptime123456 */
    };
    dispatch(packet, sizeof(packet));
    const vesc_setup_values_t *rt = vesc_rt_data_get_latest();
    CLOSE(rt->temp_mos, 30.0f); CLOSE(rt->temp_motor, -17.0f);
    CLOSE(rt->current_motor, -1.25f); CLOSE(rt->current_in, 2.5f);
    CLOSE(rt->duty_now, .125f); CLOSE(rt->rpm, 1234.0f);
    CLOSE(rt->speed, 2.5f); CLOSE(rt->v_in, 48.0f);
    CLOSE(rt->battery_level, .75f); CLOSE(rt->amp_hours, 1.0f);
    CLOSE(rt->watt_hours, 10.0f); CLOSE(rt->tachometer, 1.0f);
    CLOSE(rt->tachometer_abs, 2.0f); CLOSE(rt->position, .5f);
    CHECK(rt->fault_code == 0 && rt->vesc_id == 10 && rt->num_vescs == 1);
    CLOSE(rt->battery_wh, 100.0f);
    CHECK(rt->odometer == 1234 && rt->uptime_ms == 123456);

    /* Every known selective bit, with changed temperature/current/time:
     * rejection must fail this check rather than preserve matching values. */
    vesc_setup_values_t expected = *rt;
    uint8_t selective[sizeof(packet) + 4] = {51, 0,0x3f,0xff,0xff};
    memcpy(selective + 5, packet + 1, sizeof(packet) - 1);
    selective[7] = 0; selective[8] = 0x14; /* motor 2.0 C */
    selective[9] = 0; selective[10] = 0;
    selective[11] = 0x06; selective[12] = 0x27; /* motor current 15.75 A */
    now_ms += 100;
    expected.temp_motor = 2.0f;
    expected.current_motor = 15.75f;
    expected.rx_time = now_ms;
    dispatch(selective, sizeof(selective));
    CLOSE(vesc_rt_data_get_latest()->temp_motor, 2.0f);
    CLOSE(vesc_rt_data_get_latest()->current_motor, 15.75f);
    CHECK(vesc_rt_data_get_latest()->rx_time == now_ms);
    CHECK(memcmp(&expected, vesc_rt_data_get_latest(), sizeof(expected)) == 0);
}

static void selective_mixed_fields(void)
{
    seed_temperatures();
    /* Bits2,7,21: motor current, input voltage, uptime. No temperatures. */
    const uint8_t packet[] = {51, 0,0x20,0,0x84,
                             0xff,0xff,0xff,0x83, 0x01,0xe0,
                             0,0x01,0xe2,0x40};
    now_ms += 100;
    dispatch(packet, sizeof(packet));
    const vesc_setup_values_t *rt = vesc_rt_data_get_latest();
    CLOSE(rt->temp_motor, 12.5f); CLOSE(rt->temp_mos, 30.0f);
    CLOSE(rt->current_motor, -1.25f); CLOSE(rt->v_in, 48.0f);
    CHECK(rt->uptime_ms == 123456 && rt->rx_time == now_ms);
}

static void unknown_trailing_field(void)
{
    seed_temperatures();
    /* Bit31 represents a future appended field; preserve known motor data
     * and tolerate the trailing bytes instead of requiring exact length. */
    const uint8_t packet[] = {51, 0x80,0,0,2, 0xff,0x56, 0xde,0xad,0xbe,0xef};
    now_ms += 100;
    dispatch(packet, sizeof(packet));
    CLOSE(vesc_rt_data_get_latest()->temp_motor, -17.0f);
    CLOSE(vesc_rt_data_get_latest()->temp_mos, 30.0f);
    CHECK(vesc_rt_data_get_latest()->rx_time == now_ms);
}

static void legacy_all_prefixes_rejected(void)
{
    /* VESC 6.05 commands.c sends every bit0..21 for command47: 70 bytes.
     * A complete all-zero payload is valid; no shorter prefix is valid. */
    uint8_t packet[70] = {47};
    for (unsigned len=0; len<sizeof(packet); ++len) {
        vesc_rt_data_init(10,100);
        now_ms=1000;seed_temperatures();
        const vesc_setup_values_t before=*vesc_rt_data_get_latest();
        now_ms+=6000;
        dispatch(packet,len);
        CHECK(memcmp(&before,vesc_rt_data_get_latest(),sizeof(before))==0);
        CHECK(!vesc_rt_data_is_fresh());
        dispatch(packet,sizeof(packet));
        CLOSE(vesc_rt_data_get_latest()->temp_motor,0);
        CHECK(vesc_rt_data_is_fresh());
    }
}

static void truncated_adc(void)
{
    const uint8_t packet[] = {32, 0,0x0f,0x42,0x40, 0,0x2d,0xc6,0xc0,
                            0,0x03,0xd0,0x90, 0,0x16,0xe3,0x60};
    dispatch(packet, sizeof(packet));
    const vesc_io_data_t before = *vesc_io_data_get_latest();
    now_ms += 5000;
    for (unsigned len = 0; len < sizeof(packet); ++len) {
        dispatch(packet, len);
        CHECK(memcmp(&before, vesc_io_data_get_latest(), sizeof(before)) == 0);
    }
}

static void truncated_selective_no_refresh(void)
{
    seed_temperatures();
    const uint32_t before = vesc_rt_data_get_latest()->rx_time;
    now_ms += 5000;
    CHECK(!vesc_rt_data_is_fresh());
    const uint8_t packet[] = {51, 0,0,0,3}; /* mask demands absent FET and motor */
    dispatch(packet, sizeof(packet));
    CHECK(vesc_rt_data_get_latest()->rx_time == before);
    CHECK(!vesc_rt_data_is_fresh());
}

static void truncated_selective_atomic(void)
{
    seed_temperatures();
    const vesc_setup_values_t before = *vesc_rt_data_get_latest();
    now_ms += 100;
    /* Mask also requires 4-byte motor current, missing entirely. */
    const uint8_t packet[] = {51, 0,0,0,7, 0x01,0x90, 0xfc,0x19};
    dispatch(packet, sizeof(packet));
    CHECK(memcmp(&before, vesc_rt_data_get_latest(), sizeof(before)) == 0);
}

static void truncated_current_no_field_shift(void)
{
    seed_temperatures();
    const vesc_setup_values_t before = *vesc_rt_data_get_latest();
    now_ms += 100;
    /* Mask current(4 bytes)+duty(2): only 2 current bytes arrived. They must
     * never become duty .5 or refresh the previous temperature snapshot. */
    const uint8_t packet[] = {51, 0,0,0,0x14, 0x01,0xf4};
    dispatch(packet, sizeof(packet));
    CHECK(memcmp(&before, vesc_rt_data_get_latest(), sizeof(before)) == 0);
}

static void all_selective_prefixes_and_recovery(void)
{
    /* Bits0,1,2,4,16,21 cover 1-, 2-, and 4-byte fields with gaps in the
     * mask: FET40, motor-17, current15.75, duty.125, fault3, uptime123456. */
    const uint8_t packet[] = {
        51, 0,0x21,0,0x17, 0x01,0x90, 0xff,0x56,
        0,0,0x06,0x27, 0,0x7d, 3, 0,0x01,0xe2,0x40
    };
    for (unsigned len = 0; len < sizeof(packet); ++len) {
        vesc_rt_data_init(10, 100);
        now_ms = 1000;
        seed_temperatures();
        const vesc_setup_values_t before = *vesc_rt_data_get_latest();
        now_ms += 5000;
        CHECK(!vesc_rt_data_is_fresh());
        const int previous_failures = failures;
        dispatch(packet, len);
        CHECK(memcmp(&before, vesc_rt_data_get_latest(), sizeof(before)) == 0);
        CHECK(vesc_rt_data_get_latest()->rx_time == before.rx_time);
        CHECK(!vesc_rt_data_is_fresh());
        if (failures != previous_failures)
            printf("Rejected-prefix contract failed at %u/%u bytes\n",
                   len, (unsigned)sizeof(packet));

        /* Every rejection must permit the next complete reply to recover. */
        now_ms += 100;
        dispatch(packet, sizeof(packet));
        const vesc_setup_values_t *rt = vesc_rt_data_get_latest();
        CLOSE(rt->temp_mos, 40.0f); CLOSE(rt->temp_motor, -17.0f);
        CLOSE(rt->current_motor, 15.75f); CLOSE(rt->duty_now, .125f);
        CHECK(rt->fault_code == 3 && rt->uptime_ms == 123456);
        CHECK(rt->rx_time == now_ms && vesc_rt_data_is_fresh());
    }
}

static void short_header(void)
{
    seed_temperatures();
    const vesc_setup_values_t before = *vesc_rt_data_get_latest();
    const uint8_t packet[] = {51, 0,0,0};
    now_ms += 100;
    for (unsigned len = 0; len <= sizeof(packet); ++len) {
        dispatch(packet, len);
        CHECK(memcmp(&before, vesc_rt_data_get_latest(), sizeof(before)) == 0);
    }
}

static void other_commands(void)
{
    seed_temperatures();
    const vesc_setup_values_t before = *vesc_rt_data_get_latest();
    const uint8_t packets[][9] = {
        {36,'V','P',0x84,0,0,0,0,0}, /* Lisp DASH custom command */
        {31,0,0,0,0,0,0x16,0xe3,0x60}, /* decoded PPM */
        {4,0xfc,0x19,0xff,0x56,0,0,0,0} /* other values command */
    };
    now_ms += 100;
    for (unsigned i = 0; i < 3; ++i) {
        dispatch(packets[i], sizeof(packets[i]));
        CHECK(memcmp(&before, vesc_rt_data_get_latest(), sizeof(before)) == 0);
    }
}

static void can_response(const uint8_t *data,unsigned len,int sender) {
#ifdef TEST_BASELINE_LEGACY_RT
    /* Original main.c dispatch had no sender-aware RT wrapper. */
    (void)sender;vesc_rt_data_process_response(data,len);
#else
    vesc_rt_data_process_can_response(data,len,sender);
#endif
}
static void request_includes_motor_identity(void) {
    vesc_rt_data_request();CHECK(sent_target==10);CHECK(sent_mask & (1u<<17));
    CHECK(sent_mask & (1u<<1));
}
static void can_motor_identity(void) {
    const uint8_t motor11[]={51,0,2,0,3,1,44,0,20,11};
    const uint8_t motor10[]={51,0,2,0,3,1,44,0xff,0x56,10};
    const uint8_t no_id[]={51,0,0,0,3,1,44,0,20};
    seed_temperatures();const vesc_setup_values_t before=*vesc_rt_data_get_latest();
    now_ms+=6000;
    can_response(motor11,sizeof motor11,10); /* base matches target, logical motor wrong */
    CHECK(memcmp(&before,vesc_rt_data_get_latest(),sizeof before)==0);
    can_response(no_id,sizeof no_id,10); /* ambiguous even with matching envelope */
    CHECK(memcmp(&before,vesc_rt_data_get_latest(),sizeof before)==0);
    for(unsigned len=0;len<sizeof motor10;len++) {
        can_response(motor10,len,10);
        CHECK(memcmp(&before,vesc_rt_data_get_latest(),sizeof before)==0);
    }
    can_response(motor10,sizeof motor10,-1);
    can_response(motor10,sizeof motor10,255);
    can_response(motor10,sizeof motor10,256);
    can_response(motor10,sizeof motor10,11); /* unrelated base cannot claim motor10 */
    CHECK(memcmp(&before,vesc_rt_data_get_latest(),sizeof before)==0);
    can_response(motor10,sizeof motor10,10);
    CLOSE(vesc_rt_data_get_latest()->temp_motor,-17);CHECK(vesc_rt_data_is_fresh());
    vesc_rt_data_init(11,100);
    can_response(motor11,sizeof motor11,10); /* official dual motor: base10, motor11 */
    CLOSE(vesc_rt_data_get_latest()->temp_motor,2);CHECK(vesc_rt_data_is_fresh());
    CHECK(vesc_rt_data_get_latest()->vesc_id==11);
    /* Non-selective47 ID follows fault at byte56; selected motor owns identity. */
    uint8_t legacy[70]={47};legacy[3]=0xff;legacy[4]=0x56;legacy[56]=11;
    can_response(legacy,sizeof legacy,10);CLOSE(vesc_rt_data_get_latest()->temp_motor,-17);
    const vesc_setup_values_t accepted=*vesc_rt_data_get_latest();
    legacy[56]=10;now_ms+=100;can_response(legacy,sizeof legacy,10);
    CHECK(memcmp(&accepted,vesc_rt_data_get_latest(),sizeof accepted)==0);
    vesc_rt_data_init(0,100);legacy[56]=0;
    can_response(legacy,sizeof legacy,254); /* official alias wraps254 to0 */
    CLOSE(vesc_rt_data_get_latest()->temp_motor,-17);CHECK(vesc_rt_data_is_fresh());
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    vesc_rt_data_init(10, 100);
    vesc_io_data_init(10, 150);
#define RUN(name) if (strcmp(argv[1], #name) == 0) { name(); goto done; }
    RUN(video_temperature_values)
    RUN(adc_does_not_change_temperature)
    RUN(selective_motor_only)
    RUN(full_setup_layout)
    RUN(selective_mixed_fields)
    RUN(unknown_trailing_field)
    RUN(legacy_all_prefixes_rejected)
    RUN(truncated_adc)
    RUN(truncated_selective_no_refresh)
    RUN(truncated_selective_atomic)
    RUN(truncated_current_no_field_shift)
    RUN(all_selective_prefixes_and_recovery)
    RUN(short_header)
    RUN(other_commands)
    RUN(request_includes_motor_identity)
    RUN(can_motor_identity)
    fprintf(stderr, "Unknown case: %s\n", argv[1]); return 2;
done:
    printf("%s %s\n", failures ? "FAIL" : "PASS", argv[1]);
    return failures ? 1 : 0;
}
