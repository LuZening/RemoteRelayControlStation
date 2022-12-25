
#include "relay.h"
#ifdef USE_FREERTOS
#include "freertos/FreeRTOS.h"
#endif
static inline void SET_PIN_LEVEL(int pin, uint8_t level)
{
    gpio_set_level(pin, level);
}



void relay_init(Relay_t* p, RelayType_t type, uint8_t pinA, uint8_t pinB, bool isHighEffective)
{
    p->type = type;
    p->pinA = pinA;
    p->pinB = pinB;
    p->isHighEffective = isHighEffective;
    gpio_set_direction(pinA, GPIO_MODE_OUTPUT);
    gpio_set_direction(pinB, GPIO_MODE_OUTPUT);
    SET_PIN_LEVEL(pinA, (isHighEffective ? 0 : 1));
    SET_PIN_LEVEL(pinB, (isHighEffective ? 0 : 1));
    p->state = false;
}



void relay_switch(Relay_t* p, bool state)
{
    switch(p->type)
    {
        case RELAY_TYPE_NORMAL:
        SET_PIN_LEVEL(p->pinA, state == p->isHighEffective);
        break;
        case RELAY_TYPE_LATCHING:
        SET_PIN_LEVEL(p->pinA, state == p->isHighEffective);
        SET_PIN_LEVEL(p->pinB, state != p->isHighEffective);
        vTaskDelay(pdMS_TO_TICKS(200));
        SET_PIN_LEVEL(p->pinA, !p->isHighEffective);
        SET_PIN_LEVEL(p->pinB, !p->isHighEffective);
        break;
    }
    p->state = state;
}


bool relay_get_state(Relay_t* p)
{
    return p->state;
}

void relay_toggle(Relay_t* p)
{

    // TODO:


}