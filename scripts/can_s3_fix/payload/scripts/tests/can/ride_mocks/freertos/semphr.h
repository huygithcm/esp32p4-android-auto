#pragma once
#include "FreeRTOS.h"
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
int xSemaphoreGive(SemaphoreHandle_t sem);
