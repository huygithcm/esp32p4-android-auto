/*
    Copyright 2026 ESP32-P4 Android Auto / VESC Display (GPL-3.0).

    See vesc_head2.h. Thin settings-gated reader over the CAN STATUS_4 table
    kept by comm_can.c. STATUS_4.rx_time is stamped with xTaskGetTickCount(),
    so freshness is computed in ticks.
*/

#include "vesc_head2.h"

#include "dev_settings.h"
#include "vesc_can/comm_can.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Display/liveness window. The VESC-side aggregation window is a tight 100 ms,
 * but for showing temps and the alive flag a few missed 50 Hz frames are fine —
 * 1 s avoids flicker if the bus hiccups. */
#define HEAD2_FRESH_MS 1000u

bool vesc_head2_get_temps(float *temp_fet, float *temp_motor)
{
    if (!settings_get_second_head_enabled()) return false;

    can_status_msg_4 *m = comm_can_get_status_msg_4_id(settings_get_second_head_id());
    if (!m) return false;

    uint32_t age_ticks = xTaskGetTickCount() - m->rx_time;
    if (age_ticks * portTICK_PERIOD_MS > HEAD2_FRESH_MS) return false;

    if (temp_fet)   *temp_fet   = m->temp_fet;
    if (temp_motor) *temp_motor = m->temp_motor;
    return true;
}

bool vesc_head2_is_fresh(void)
{
    return vesc_head2_get_temps(NULL, NULL);
}
