#pragma once

#include "freertos/event_groups.h"
// main event group
#define SIGNAL_RESET 0b01U
#define SIGNAL_SAVE_CONFIG 0b10U

extern EventGroupHandle_t evtgrpMain;


void reset_async();

void write_to_EEPROM_async();