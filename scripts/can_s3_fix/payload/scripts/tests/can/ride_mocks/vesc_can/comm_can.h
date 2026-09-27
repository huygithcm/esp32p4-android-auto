#pragma once
#include <stdint.h>
uint8_t comm_can_get_local_id(void);
void comm_can_send_buffer_sync(uint8_t id, const uint8_t *data,
                              unsigned len, uint8_t send, uint32_t timeout);
