/**
  ******************************************************************************
  * @file    Project/STM32F4xx_StdPeriph_Templates/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @version V1.8.1
  * @date    27-January-2022
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "stm32f4xx_usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "audio.h"

extern void vPortSVCHandler( void );
extern void xPortPendSVHandler( void );
extern void xPortSysTickHandler( void );

/** @addtogroup Template_Project
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/* 直接写 USART1 寄存器的调试输出（不依赖 printf / 互斥量 / 任务上下文）。
 * 崩溃或栈溢出时用，实现在文件下方。 */
void debug_puts(const char *s);
void debug_puts_hex(uint32_t v);

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
/* ============================================================================
 *  硬件异常诊断
 *
 *  原本 HardFault 就是一句 while(1)，什么线索都不留。
 *  现象变成"串口输出从中间断掉 / 第一帧不显示"这种莫名其妙的症状。
 *
 *  这里改成：直接往 USART1 寄存器写（不用 printf、不拿互斥量、不依赖任务
 *  上下文 —— 崩溃的时候这些都不可信），把关键状态打出来。
 *
 *  怎么看结果：
 *    CFSR  —— 故障类型，按位看：
 *              bit25 DIVBYZERO    除零
 *              bit24 UNALIGNED    非对齐访问
 *              bit16 UNDEFINSTR   执行了非法指令（多半是跑飞了）
 *              bit10 PRECISERR    精确总线错误，BFAR 就是出错地址
 *              bit 1 DACCVIOL     数据访问违例
 *              bit 0 IACCVIOL     取指违例
 *    BFAR  —— 出错的数据地址（PRECISERR 时有效）
 *    MMFAR —— 出错地址（MMARVALID 时有效）
 *    MSP/PSP —— 出错时用的哪个栈；PSP 是任务栈，MSP 是 main/中断栈
 * ========================================================================== */
static void fault_putc(char c)
{
    while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)c);
}

static void fault_puts(const char *s)
{
    while(*s) fault_putc(*s++);
}

/* 非 static：给 main.c 里的 vApplicationStackOverflowHook 用。
 * 那个钩子必须打印，否则现象就是"所有任务突然静止，一点线索都没有" */
void debug_puts(const char *s)
{
    fault_puts(s);
}

void debug_puts_hex(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    int i;
    fault_puts("0x");
    for(i = 28; i >= 0; i -= 4) fault_putc(h[(v >> i) & 0xF]);
}

static void fault_hex(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    int i;
    fault_puts("0x");
    for(i = 28; i >= 0; i -= 4) fault_putc(h[(v >> i) & 0xF]);
}

static void fault_reg(const char *name, uint32_t v)
{
    fault_puts(name);
    fault_puts(" = ");
    fault_hex(v);
    fault_puts("\r\n");
}

void HardFault_Report(uint32_t *sf)
{
    fault_puts("\r\n*** HARD FAULT ***\r\n");
    if(sf != 0)
    {
        /* 异常压栈的 8 个字：R0 R1 R2 R3 R12 LR PC xPSR */
        fault_reg("R0  ", sf[0]);
        fault_reg("R1  ", sf[1]);
        fault_reg("R2  ", sf[2]);
        fault_reg("R3  ", sf[3]);
        fault_reg("R12 ", sf[4]);
        fault_reg("LR  ", sf[5]);
        fault_reg("PC  ", sf[6]);   /* ★ 出错的那条指令地址，去 .map 里查 */
        fault_reg("xPSR", sf[7]);
    }
    fault_reg("CFSR", SCB->CFSR);
    fault_reg("HFSR", SCB->HFSR);
    fault_reg("MMFAR", SCB->MMFAR);
    fault_reg("BFAR", SCB->BFAR);
    fault_reg("MSP ", __get_MSP());
    fault_reg("PSP ", __get_PSP());
    fault_puts("*** 停住 ***\r\n");

    for(;;)
    {
    }
}

/* 用嵌入汇编取出"出错时压栈的寄存器组"：
 * EXC_RETURN(bit2) 决定用的是 MSP 还是 PSP */
__asm void HardFault_Handler(void)
{
    IMPORT HardFault_Report
    TST    lr, #4
    ITE    EQ
    MRSEQ  r0, msp
    MRSNE  r0, psp
    B      HardFault_Report
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
	if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
		vPortSVCHandler();
	}
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
	if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
		xPortPendSVHandler();
	}
}

void SysTick_Handler(void)
{
	if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
		xPortSysTickHandler();
	}
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/******************************************************************************/

/**
  * @brief  I2S2 TX 音频 DMA（DMA1_Stream4）中断：半缓冲边界续填双缓冲
  *         实现在 audio.c，见 audio.h 的 Audio_DMA_IRQHandler。
  */
void DMA1_Stream4_IRQHandler(void)
{
    Audio_DMA_IRQHandler();
}

/**
  * @}
  */ 


