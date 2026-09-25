#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static SemaphoreHandle_t uart_tx_mutex = NULL;

int8_t RXDATA;


void Usart_SendByte(USART_TypeDef * pUSARTx, uint8_t ch)
{
    /* 发送一个字节数据到 USART */
    USART_SendData(pUSARTx, ch);

    /* 等待发送数据寄存器为空 */
    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);
}


/*****************  发送指定长度的字节 **********************/
void Usart_SendBytes(USART_TypeDef * pUSARTx, uint8_t *buf, uint32_t len)
{
    uint8_t *p = buf;

    while (len--)
    {
        USART_SendData(pUSARTx, *p);
        p++;

        /* 等待数据发送完成 */
        while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);
    }
}


/****************** 发送 8 位的数组 ************************/
void Usart_SendArray(USART_TypeDef * pUSARTx, uint8_t *array, uint16_t num)
{
    uint8_t i;

    for (i = 0; i < num; i++)
    {
        /* 发送一个字节数据到 USART */
        Usart_SendByte(pUSARTx, array[i]);
    }

    /* 等待发送完成 */
    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TC) == RESET);
}


/*****************  发送字符串 **********************/
void Usart_SendString(USART_TypeDef * pUSARTx, char *str)
{
    unsigned int k = 0;

    do
    {
        Usart_SendByte(pUSARTx, *(str + k));
        k++;
    } while (*(str + k) != '\0');

    /* 等待发送完成 */
    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TC) == RESET);
    USART_ClearFlag(pUSARTx, USART_FLAG_TC);
}


/*****************  发送一个 16 位数 **********************/
void Usart_SendHalfWord(USART_TypeDef * pUSARTx, uint16_t ch)
{
    uint8_t temp_h, temp_l;

    /* 取出高八位 */
    temp_h = (ch & 0XFF00) >> 8;
    /* 取出低八位 */
    temp_l = ch & 0XFF;

    /* 发送高八位 */
    USART_SendData(pUSARTx, temp_h);
    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);

    /* 发送低八位 */
    USART_SendData(pUSARTx, temp_l);
    while (USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);
}


/* 重定向 C 库函数 printf 到串口，重定向后可使用 printf 函数 */
int fputc(int ch, FILE *f)
{
    int locked = (uart_tx_mutex != NULL &&
                  xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
                  __get_IPSR() == 0);

    if (locked)
    {
        xSemaphoreTake(uart_tx_mutex, portMAX_DELAY);
    }

    USART_SendData(USARTX, (uint8_t) ch);
    while (USART_GetFlagStatus(USARTX, USART_FLAG_TXE) == RESET);

    if (locked)
    {
        xSemaphoreGive(uart_tx_mutex);
    }

    return ch;
}


/* 在 USART1_Config() 里或启动调度器之前调用一次 */
void UART_TxLock_Init(void)
{
    if (uart_tx_mutex == NULL)
    {
        uart_tx_mutex = xSemaphoreCreateMutex();
    }
}


/* 重定向 C 库函数 scanf 到串口，重写后可使用 scanf、getchar 等函数 */
int fgetc(FILE *f)
{
    /* 等待串口输入数据 */
    while (USART_GetFlagStatus(USARTX, USART_FLAG_RXNE) == RESET);

    return (int)USART_ReceiveData(USARTX);
}


/* 串口 1 初始化 */
void USART1_Config(uint32_t USART_BaudRate)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);  /* 使能 GPIOA 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE); /* 使能 USART1 时钟 */

    /* 2. 配置 GPIO 引脚为复用功能 */
    GPIO_InitTypeDef GPIO_InitStruct;

    /* 配置 PA9 (TX) */
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;       /* 复用功能模式 */
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;  /* 速度 50MHz */
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;     /* 推挽输出 */
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;       /* 上拉 */
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 配置 PA10 (RX) */
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 3. 引脚复用功能映射 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);  /* PA9 映射到 USART1 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1); /* PA10 映射到 USART1 */

    /* 4. 初始化 USART1 */
    USART_InitTypeDef USART_InitStruct;
    USART_InitStruct.USART_BaudRate = USART_BaudRate;                        /* 波特率 */
    USART_InitStruct.USART_WordLength = USART_WordLength_8b;                /* 8 位数据 */
    USART_InitStruct.USART_StopBits = USART_StopBits_1;                     /* 1 位停止位 */
    USART_InitStruct.USART_Parity = USART_Parity_No;                        /* 无校验 */
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None; /* 无硬件流控 */
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;            /* 同时使能收发 */
    USART_Init(USART1, &USART_InitStruct);

    /* 5. 使能 USART1 */
    USART_Cmd(USART1, ENABLE);
}


void USART1_IRQHandler(void)
{
    /* 只在 RXNE 真正触发时才处理 */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        RXDATA = USART_ReceiveData(USART1); /* 读数据自动清除 RXNE */
        /* 不要手动 Clear RXNE！ */
    }
}