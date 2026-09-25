#include "delay.h"

//us延时
void Delay_us(uint64_t time)
{
	while(time--)
	{
		Delay_1us();
	}
}

//ms延时
void Delay_ms(uint64_t time)
{
	time *= 1000;
	while(time--)
	{
		Delay_1us();
	}	
}

