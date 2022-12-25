
/*
 * ButtonDebouncer.c
 * For ESP-IDF
 *  Created on: June 25, 2022
 *      Author: Zening
 */

#include "ButtonDebouncer.h"
#include "driver/gpio.h"


void btndeb_init(ButtonDebouncer_t* p, int GPIOpin, unsigned int cntdnRefill)
{
	p->state = 1;
	p->longPressedTimeMS = 0;
	p->GPIOpin = GPIOpin;
	p->cntdn = 0;
	p->cntdnRefill = cntdnRefill;
	p->on_flip = NULL;
}


void btndeb_register_flip_callback(ButtonDebouncer_t* p, btnCallbackFunc_t fn)
{
	p->on_flip = fn;
}


int btndeb_tick(ButtonDebouncer_t* p, unsigned int us, uint8_t state_external)
{
	// uint8_t stateNew = HAL_GPIO_ReadPin(p->GPIOport, p->GPIOpin);
	uint8_t stateNew;
	// if GPIO pin number is negative, indicating relying on external input of button states, will recon state_external argument as button state 
	if(p->GPIOpin < 0)
		stateNew = state_external;
	else
		stateNew = gpio_get_level(p->GPIOpin);
	int retval = 0;
	// will confirm the current state if count down reaches 0
	if(p->cntdn > 0)
	{
		p->cntdn -= us;
		if(p->cntdn <= 0)
		{
			p->cntdn = 0;
			if(stateNew != p->state)
			{
				p->state = stateNew;
				if(p->on_flip != NULL)
					p->on_flip(stateNew);
				retval = 1;
			}
		}
	}
	// if the count down has not started yet, initiate a debouncer counting down
	else
	{
		if(stateNew != p->state)
		{
			p->cntdn = p->cntdnRefill;
			p->longPressedTimeMS = 0;
		}
		else
		{
			// p->cntdn = 0;
			// if state stays unchanged, accumulate long press counter
			if(p->state == BTNDEB_STATE_PRESSED)
			{
				if(p->longPressedTimeMS < MAX_LONG_PRESS_TIME_MS)
					p->longPressedTimeMS += us / 1000;
			}
		}
	}
	return retval;
}
