#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#define SIGNAL_MOT_STOP 0b00001U
#define SIGNAL_MOT_RUN_FORWARD 0b00010U
#define SIGNAL_MOT_RUN_BACKWARD 0b00100U
#define MOT_FORWARD 1
#define MOT_BACKWARD -1
#define MOT_STOP 0
extern EventGroupHandle_t evtTaskMot;
extern SemaphoreHandle_t mtxMot;
extern int8_t motstate;


void task_motor(void* args);