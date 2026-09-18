/*
 * CAN transport hook for the config module. main.c's vesc_packet_dispatch()
 * must call vesc_config_transport_process_response() for every reassembled
 * COMM packet so config GET/SET/FW_VERSION replies are routed here. The handler
 * gates on data[0] (the COMM ID) and ignores packets it doesn't own, so it
 * co-exists with the rt_data / lisp / BLE-forward fan-out.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called from the CAN process task for each reassembled COMM packet (data[0]
 * is the COMM ID). Returns true if it consumed the packet (a config reply),
 * false otherwise. */
bool vesc_config_transport_process_response(const uint8_t *data, unsigned int len);

#ifdef __cplusplus
}
#endif
