#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
                                  uint32_t stack, void *arg, unsigned priority,
                                  TaskHandle_t *handle, int core);
