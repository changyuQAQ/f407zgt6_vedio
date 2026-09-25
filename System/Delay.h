#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f4xx.h"
#include "stdio.h"
#include "string.h"



#define Delay_1us()	{\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
	__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();\
}

void Delay_us(uint64_t time);//函数声明
void Delay_ms(uint64_t time);


#endif
