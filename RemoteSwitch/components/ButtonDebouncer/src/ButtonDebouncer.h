/*
 * ButtonDebouncer.h
 * For ESP-IDF
 *  Created on: June 25, 2022
 *      Author: Zening
 */

#ifndef BUTTONDEBOUNCER_H_
#define BUTTONDEBOUNCER_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef void (*btnCallbackFunc_t) (uint8_t);

typedef enum
{
	BTNDEB_STATE_PRESSED = 0,
	BTNDEB_STATE_RELEASED = 1
} ButtonState_t;

#define MAX_LONG_PRESS_TIME_MS 100000 // 100 seconds

typedef struct
{
	// 消抖后的按键状态 Low Effective 1: 高， 0: 低（按下)
	volatile uint8_t state;
	int longPressedTimeMS; // 按下后的时间（ms）
	int GPIOpin;
	int cntdn; // 倒数定时器计数器()
	unsigned int cntdnRefill; // 倒数定时器重装填值(us)
	btnCallbackFunc_t on_flip; // 状态翻转时的回调函数
} ButtonDebouncer_t;

// 初始化按键消抖
// cntdnRetill 延迟us数
void btndeb_init(ButtonDebouncer_t* p, int GPIOpin, unsigned int cntdnRefill);

// 时基脉膊的函数
// 状态翻转则返回相应状态
// 返回 1：翻转 0：保持
// state_external参数可选，可以不依赖gpio，从外部输入按钮状态, LOW EFFECTIVE
int btndeb_tick(ButtonDebouncer_t* p, unsigned int us, uint8_t state_external);

// 注册翻转回调函数
void btndeb_register_flip_callback(ButtonDebouncer_t* p, btnCallbackFunc_t fn);

#endif /* BUTTONDEBOUNCER_H_ */
