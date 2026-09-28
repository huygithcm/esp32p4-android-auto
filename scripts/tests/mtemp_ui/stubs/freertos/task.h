#pragma once
#include "../../../vesc_telemetry/stubs/freertos/task.h"
#define portTICK_PERIOD_MS 1u
TickType_t xTaskGetTickCount(void);
