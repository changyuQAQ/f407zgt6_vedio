#ifndef __UART_H
#define __UART_H

//包含头文件
#include "stm32f4xx.h"
#include "stdio.h"
#include "string.h"

extern int8_t RXDATA;

//串口printf打印端口的选择
#define USARTX USART1

//函数声明
void USART1_Config(uint32_t USART_BaudRate);

/* printf 串行化锁。多个任务都会 printf，必须用一个互斥量把 fputc 保护起来，
 * 否则两个任务同时打印会互相踩（输出从中间断掉，严重时直接崩）。
 * 在 USART1_Config() 之后、vTaskStartScheduler() 之前调用一次。 */
void UART_TxLock_Init(void);

void Usart_SendByte( USART_TypeDef * pUSARTx, uint8_t ch);
void Usart_SendBytes(USART_TypeDef * pUSARTx, uint8_t *buf,uint32_t len);
void Usart_SendArray( USART_TypeDef * pUSARTx, uint8_t *array, uint16_t num);
void Usart_SendString( USART_TypeDef * pUSARTx, char *str);
void Usart_SendHalfWord( USART_TypeDef * pUSARTx, uint16_t ch);

#endif
