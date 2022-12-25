#pragma once
#include "driver/gpio.h"

typedef enum
{
    RELAY_TYPE_NORMAL = 0,
    RELAY_TYPE_LATCHING,
} RelayType_t;

typedef struct {
    RelayType_t type;
    bool isHighEffective;
    int pinA;
    int pinB;
    bool state;
    
} Relay_t;




void relay_init(Relay_t* p, RelayType_t type ,uint8_t pinA, uint8_t pinB, bool isHighEffective);
void relay_switch(Relay_t* p, bool state);
void relay_toggle(Relay_t* p);
bool relay_get_state(Relay_t* p);

/* The following interfaces please implement in main.c */
extern void set_relay_state(int relayno, int state);
extern void set_timed_relay_state(int relayno, int state, uint32_t timems);

/* implement the following variables in main.c */
#define N_RELAYS 1
extern const int RELAY_TYPES[N_RELAYS];
extern const int PIN_RELAYS_A[N_RELAYS];
extern const int PIN_RELAYS_B[N_RELAYS];
extern Relay_t Relays[N_RELAYS];
extern uint64_t RelaysTimemsRemain[N_RELAYS];