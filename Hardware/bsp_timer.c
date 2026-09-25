#include "bsp_timer.h"
#include <stdio.h>
volatile unsigned int g_sys_tick = 0;
void SysTick_Init(void)
{
  if (SysTick_Config(SystemCoreClock / 1000))  // SystemCoreClock 通常定义为 72000000
    { 
      while (1);
    }
}

uint32_t GetTick(void)
{
  return g_sys_tick;
}


void HseClock_Init(uint32_t PLL_M,uint32_t PLL_N,uint32_t PLL_P,uint32_t PLL_Q) 
{
    // 1. 重置RCC时钟配置到默认状态 (可选但建议)
    RCC_DeInit();

    // 2. 开启HSE外部晶振
    RCC_HSEConfig(RCC_HSE_ON);
    // 等待HSE就绪
    while (RCC_WaitForHSEStartUp() != SUCCESS);

    // 3. 配置PLL
    // 假设 HSE_VALUE = 8MHz
    // 配置目标: SYSCLK = 168MHz
    // PLL_M = 8, PLL_N = 336, PLL_P = 2
    // PLL_VCO输入 = 8MHz / 8 = 1MHz
    // PLL_VCO输出 = 1MHz * 336 = 336MHz
    // SYSCLK = 336MHz / 2 = 168MHz[reference:5]
    RCC_PLLConfig(RCC_PLLSource_HSE, PLL_M, PLL_N, PLL_P, PLL_Q); // PLL_Q=7, 用于USB等, 这里暂不关心

    // 4. 使能PLL
    RCC_PLLCmd(ENABLE);
    // 等待PLL锁定
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

    // 5. 配置AHB, APB1, APB2总线分频器
    // HCLK = SYSCLK / 1 = 168MHz
    // PCLK1 (APB1) = HCLK / 4 = 42MHz (最大42MHz)
    // PCLK2 (APB2) = HCLK / 2 = 84MHz (最大84MHz)[reference:6][reference:7]
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div4);
    RCC_PCLK2Config(RCC_HCLK_Div2);

    // 6. 选择PLL作为系统时钟源
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);

    // 可选: 更新SystemCoreClock全局变量, 供SysTick等使用
    SystemCoreClockUpdate(); 
}

void Delay_ms(uint32_t ms)
{
    uint32_t start = g_sys_tick;
    while ((g_sys_tick - start) < ms);
}


