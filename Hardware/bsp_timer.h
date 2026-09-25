#ifndef __BSP_TIMER_H
#define __BSP_TIMER_H

#include "stm32f4xx.h"
//#include "gpio_config.h"
extern volatile unsigned int g_sys_tick;  
uint32_t GetTick(void);
void SysTick_Init(void);
void HseClock_Init(uint32_t PLL_M,uint32_t PLL_N,uint32_t PLL_P,uint32_t PLL_Q);
void Delay_ms(uint32_t ms);

#endif
