/*
    Copyright 2025 Super VESC Display
    Copyright 2026 Adapted to ESP-IDF for ESP32-P4

    Periodic VESC RT-data poller. Sends COMM_GET_VALUES_SETUP_SELECTIVE
    to a target VESC over CAN at a configurable interval and parses
    the response into a vesc_setup_values_t snapshot.
*/

#pragma once

#include "esp_err.h"
#include "vesc_can/vesc_datatypes.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise internal state. Safe to call multiple times. */
void vesc_rt_data_init(uint8_t target_vesc_id, uint32_t poll_interval_ms);

/* Pump the poller. Either call vesc_rt_data_loop() periodically from
 * a task you already have, or call vesc_rt_data_start_task() once to
 * spawn a dedicated FreeRTOS task that drives it. */
void      vesc_rt_data_loop(void);
esp_err_t vesc_rt_data_start_task(void);

bool vesc_rt_data_is_active(void);
void vesc_rt_data_start(void);
void vesc_rt_data_stop(void);

/* Manual one-shot request. */
void vesc_rt_data_request(void);

/* Hook this from the comm_can packet handler (e.g. via a wrapper
 * that also forwards to BLE in the future). Filters by command byte,
 * so passing every packet is fine. */
void vesc_rt_data_process_response(const uint8_t *data, unsigned int len);

/* CAN entry point requires the SETUP VESC_ID field to equal the configured
 * logical motor target. Missing IDs are rejected even with a matching sender:
 * official dual-motor controllers put their base ID in the CAN envelope for
 * both motors. sender_id must be0..254 and equal the target or its official
 * dual-motor base (target=(sender+1)%255). These checks select a motor; they
 * do not authenticate CAN traffic. */
void vesc_rt_data_process_can_response(const uint8_t *data, unsigned int len,
                                       int sender_id);

/* Replace the live snapshot with a fully-formed one. Bypasses the CAN
 * parser entirely — used by the VESC emulator (vesc_sim.c) to drive the
 * dashboard without a real controller. Stamps rx_time so is_fresh()==true. */
void vesc_rt_data_inject(const vesc_setup_values_t *src);

/* Latest snapshot. Always non-NULL. is_fresh() covers the last valid reply
 * containing known fields; selective replies may leave other fields older. */
const vesc_setup_values_t *vesc_rt_data_get_latest(void);
bool                       vesc_rt_data_is_fresh(void);

/* Thermal freshness is independent per sensor: selective current/voltage
 * replies cannot refresh either age. Both use the same 5-second deadline
 * as overall RT data, and init (including target changes) invalidates them. */
bool vesc_rt_data_temp_mos_is_fresh(void);
bool vesc_rt_data_temp_motor_is_fresh(void);

/* Convenience getters used by the dashboard. */
float    vesc_rt_data_get_speed_kmh(void);
float    vesc_rt_data_get_trip_km(void);
float    vesc_rt_data_get_odometer_km(void);
float    vesc_rt_data_get_efficiency_whkm(void);
float    vesc_rt_data_get_amp_hours(void);
uint32_t vesc_rt_data_get_uptime_ms(void);

#ifdef __cplusplus
}
#endif
