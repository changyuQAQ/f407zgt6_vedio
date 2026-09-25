#include "lcd_ili9488.h"
#include "stm32f4xx_fsmc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "Delay.h"
#include <stdio.h>
#include "ff.h"
#include "stm32f4xx_dma.h"


/**
 * @brief  写命令到 ILI9488
 * @param  cmd: 命令字节
 */
void ILI9488_WriteCmd(uint8_t cmd)
{
    LCD_CMD_ADDR = cmd;
}

/**
 * @brief  写数据到 ILI9488
 * @param  data: 数据字节 (16位)
 */
void ILI9488_WriteData(uint16_t data)
{
    LCD_DATA_ADDR = data;
}

/**
 * @brief  初始化 FSMC 接口
 *         配置 Bank1 NOR/SRAM4, 16位数据宽度, 读写时序
 */
void ILI9488_Pin_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* ======================== 1. 开启时钟 ======================== */
    // FSMC 外设时钟
    RCC_AHB3PeriphClockCmd(RCC_AHB3Periph_FSMC, ENABLE);
    // 所有涉及的 GPIO 端口时钟一次性开启
    RCC_AHB1PeriphClockCmd(
        RCC_AHB1Periph_GPIOB |
        RCC_AHB1Periph_GPIOC |
        RCC_AHB1Periph_GPIOD |
        RCC_AHB1Periph_GPIOE |
        RCC_AHB1Periph_GPIOF |
        RCC_AHB1Periph_GPIOG,
        ENABLE
    );

    /* ======================== 2. FSMC 数据总线 D0-D15 ======================== */
    // D0=PD14, D1=PD15, D2=PD0, D3=PD1, D13=PD8, D14=PD9, D15=PD10
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    // PD: D0(14), D1(15), D2(0), D3(1), D13(8), D14(9), D15(10)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_8 |
                               GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOD, &GPIO_InitStruct);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource0,  GPIO_AF_FSMC);  // D2
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource1,  GPIO_AF_FSMC);  // D3
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource8,  GPIO_AF_FSMC);  // D13
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource9,  GPIO_AF_FSMC);  // D14
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource10, GPIO_AF_FSMC);  // D15
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource14, GPIO_AF_FSMC);  // D0
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource15, GPIO_AF_FSMC);  // D1

    // PE: D4(7), D5(8), D6(9), D7(10), D8(11), D9(12), D10(13), D11(14), D12(15)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                               GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOE, &GPIO_InitStruct);
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource7,  GPIO_AF_FSMC);  // D4
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource8,  GPIO_AF_FSMC);  // D5
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource9,  GPIO_AF_FSMC);  // D6
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource10, GPIO_AF_FSMC);  // D7
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource11, GPIO_AF_FSMC);  // D8
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource12, GPIO_AF_FSMC);  // D9
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource13, GPIO_AF_FSMC);  // D10
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource14, GPIO_AF_FSMC);  // D11
    GPIO_PinAFConfig(GPIOE, GPIO_PinSource15, GPIO_AF_FSMC);  // D12

    /* ======================== 3. FSMC 控制信号 ======================== */
    // NWE=PD5, NOE=PD4, NE4=PG12, A6=PF12
    // PD4(NOE), PD5(NWE)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOD, &GPIO_InitStruct);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource4, GPIO_AF_FSMC);  // NOE
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource5, GPIO_AF_FSMC);  // NWE

    // PG12 (NE4 / CS)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOG, &GPIO_InitStruct);
    GPIO_PinAFConfig(GPIOG, GPIO_PinSource12, GPIO_AF_FSMC); // NE4

    // PF12 (A6 / RS)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOF, &GPIO_InitStruct);
    GPIO_PinAFConfig(GPIOF, GPIO_PinSource12, GPIO_AF_FSMC); // A6

    /* ======================== 4. 触摸 SPI 引脚 ======================== */
    // T_SCK=PB0, T_MOSI=PF11, T_MISO=PB2, T_CS=PC13, T_PEN=PB1
    // 注意: 触摸通常使用软件模拟SPI或硬件SPI，此处按通用输出/输入配置
    //       若使用硬件SPI需改为AF模式，下面按【软件模拟SPI】配置

    // PB0(T_SCK) - 推挽输出
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB2(T_MISO) - 浮空输入
    GPIO_InitStruct.GPIO_Pin  = GPIO_Pin_2;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PF11(T_MOSI) - 推挽输出
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_11;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOF, &GPIO_InitStruct);

    // PC13(T_CS) - 推挽输出, 默认高电平(未选中)
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_13;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_UP;       // 上拉保证空闲高
    GPIO_Init(GPIOC, &GPIO_InitStruct);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);                 // T_CS = 1

    // PB1(T_PEN) - 输入(中断检测用), 下拉
    GPIO_InitStruct.GPIO_Pin  = GPIO_Pin_1;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_DOWN;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* ======================== 5. LCD 复位 & 背光 ======================== */
    // ⚠️ 以下两个引脚请根据实际原理图确认，此处为常见默认值
    // LCD_RST = PG15 (示例)
    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_15;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOG, &GPIO_InitStruct);
    GPIO_SetBits(GPIOG, GPIO_Pin_15);  // RST 默认高

    // LCD_BL = PB15 (示例)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_15;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIO_ResetBits(GPIOB, GPIO_Pin_15); // BL 默认关
		
		/* ======================== 6. FSMC Bank4 初始化 (关键！) ======================== */
    FSMC_NORSRAMTimingInitTypeDef  FSMC_ReadTimingInitStructure; 
  FSMC_NORSRAMTimingInitTypeDef  FSMC_WriteTimingInitStructure;
  FSMC_NORSRAMInitTypeDef        FSMC_NORSRAMInitStructure;

  // ★ 读时序 (收紧到实用值)
  FSMC_ReadTimingInitStructure.FSMC_AddressSetupTime      = 0x02;  // 2 HCLK ≈ 12ns
  FSMC_ReadTimingInitStructure.FSMC_AddressHoldTime       = 0x00;
  FSMC_ReadTimingInitStructure.FSMC_DataSetupTime         = 0x0a;  // 11 HCLK ≈ 66ns
  FSMC_ReadTimingInitStructure.FSMC_BusTurnAroundDuration = 0x00;
  FSMC_ReadTimingInitStructure.FSMC_CLKDivision           = 0x00;
  FSMC_ReadTimingInitStructure.FSMC_DataLatency           = 0x00;
  FSMC_ReadTimingInitStructure.FSMC_AccessMode            = FSMC_AccessMode_A;

  // ★ 写时序 (DMA M2M 安全值)
  FSMC_WriteTimingInitStructure.FSMC_AddressSetupTime      = 0x02;  // ← 从0x1F降至0x02
  FSMC_WriteTimingInitStructure.FSMC_AddressHoldTime       = 0x00;
  FSMC_WriteTimingInitStructure.FSMC_DataSetupTime         = 0x0A;  // ← 从0x0F改为0x0A (安全)
  FSMC_WriteTimingInitStructure.FSMC_BusTurnAroundDuration = 0x00;
  FSMC_WriteTimingInitStructure.FSMC_CLKDivision           = 0x00;
  FSMC_WriteTimingInitStructure.FSMC_DataLatency           = 0x00;
  FSMC_WriteTimingInitStructure.FSMC_AccessMode            = FSMC_AccessMode_A;

// Bank4 基础配置 (保持不变，已正确)
  FSMC_NORSRAMInitStructure.FSMC_Bank                  = FSMC_Bank1_NORSRAM4;
  FSMC_NORSRAMInitStructure.FSMC_DataAddressMux        = FSMC_DataAddressMux_Disable;
  FSMC_NORSRAMInitStructure.FSMC_MemoryType            = FSMC_MemoryType_SRAM;
  FSMC_NORSRAMInitStructure.FSMC_MemoryDataWidth       = FSMC_MemoryDataWidth_16b;
  FSMC_NORSRAMInitStructure.FSMC_BurstAccessMode       = FSMC_BurstAccessMode_Disable;  // ✅ 必须关
  FSMC_NORSRAMInitStructure.FSMC_WaitSignalPolarity    = FSMC_WaitSignalPolarity_Low;
  FSMC_NORSRAMInitStructure.FSMC_AsynchronousWait      = FSMC_AsynchronousWait_Disable;
  FSMC_NORSRAMInitStructure.FSMC_WrapMode              = FSMC_WrapMode_Disable;
  FSMC_NORSRAMInitStructure.FSMC_WaitSignalActive      = FSMC_WaitSignalActive_BeforeWaitState;
  FSMC_NORSRAMInitStructure.FSMC_WriteOperation        = FSMC_WriteOperation_Enable;    // ✅ 写使能
  FSMC_NORSRAMInitStructure.FSMC_WaitSignal            = FSMC_WaitSignal_Disable;
  FSMC_NORSRAMInitStructure.FSMC_ExtendedMode          = FSMC_ExtendedMode_Enable;      // ✅ 读写分离
  FSMC_NORSRAMInitStructure.FSMC_WriteBurst            = FSMC_WriteBurst_Disable;       // ✅ 写突发关
  FSMC_NORSRAMInitStructure.FSMC_ReadWriteTimingStruct = &FSMC_ReadTimingInitStructure;
  FSMC_NORSRAMInitStructure.FSMC_WriteTimingStruct     = &FSMC_WriteTimingInitStructure;

  FSMC_NORSRAMInit(&FSMC_NORSRAMInitStructure);
  FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM4, ENABLE);

		
}



/**
 * @brief  ILI9488 初始化序列
 */
void ILI9488_Init(void)
{
    // 1. 初始化硬件接口
    ILI9488_Pin_Init();
	 
	  FSMC_DMA_Init();

    // 2. 硬件复位 LCD_DATA_ADDR
    LCD_RST_CLR();
	  Delay_ms(20);
    LCD_RST_SET();
    Delay_ms(120);
    

		
    // 3. 发送初始化命令序列 (参考 ILI9488 数据手册)
    // 以下为通用 320x480 初始化代码，可能需要根据具体屏幕模组微调
    
    ILI9488_WriteCmd(0xE0); // Positive Gamma Control
    ILI9488_WriteData(0x00);
    ILI9488_WriteData(0x03);
    ILI9488_WriteData(0x09);
    ILI9488_WriteData(0x08);
    ILI9488_WriteData(0x16);
    ILI9488_WriteData(0x0A);
    ILI9488_WriteData(0x3F);
    ILI9488_WriteData(0x78);
    ILI9488_WriteData(0x4C);
    ILI9488_WriteData(0x09);
    ILI9488_WriteData(0x0A);
    ILI9488_WriteData(0x08);
    ILI9488_WriteData(0x16);
    ILI9488_WriteData(0x1A);
    ILI9488_WriteData(0x0F);

    ILI9488_WriteCmd(0xE1); // Negative Gamma Control
    ILI9488_WriteData(0x00);
    ILI9488_WriteData(0x16);
    ILI9488_WriteData(0x19);
    ILI9488_WriteData(0x03);
    ILI9488_WriteData(0x0F);
    ILI9488_WriteData(0x05);
    ILI9488_WriteData(0x32);
    ILI9488_WriteData(0x45);
    ILI9488_WriteData(0x46);
    ILI9488_WriteData(0x04);
    ILI9488_WriteData(0x0E);
    ILI9488_WriteData(0x0D);
    ILI9488_WriteData(0x35);
    ILI9488_WriteData(0x37);
    ILI9488_WriteData(0x0F);

    ILI9488_WriteCmd(0xC0); // Power Control 1
    ILI9488_WriteData(0x17);
    ILI9488_WriteData(0x15);

    ILI9488_WriteCmd(0xC1); // Power Control 2
    ILI9488_WriteData(0x41);

    ILI9488_WriteCmd(0xC5); // VCOM Control
    ILI9488_WriteData(0x00);
    ILI9488_WriteData(0x12);
    ILI9488_WriteData(0x80);

    ILI9488_WriteCmd(0x36); // Memory Access Control (旋转/扫描方向)
    // 0x48: MY=0, MX=1, MV=0, ML=0, BGR=1 (根据实际屏幕方向调整)
    // 常用值: 0x28 (竖屏), 0x48 (横屏), 0x88 (竖屏翻转), 0xE8 (横屏翻转)
    // 这里设为 0x48 (320x480 横屏模式，BGR顺序)
    ILI9488_WriteData(0x48); 

    ILI9488_WriteCmd(0x3A); // Interface Pixel Format
    ILI9488_WriteData(0x55); // 16-bit/pixel (RGB565) via 16-bit bus

    ILI9488_WriteCmd(0xB0); // Interface Mode Control
    ILI9488_WriteData(0x00);

    ILI9488_WriteCmd(0xB1); // Frame Rate Control
    ILI9488_WriteData(0xA0);

    ILI9488_WriteCmd(0xB4); // Display Inversion Control
    ILI9488_WriteData(0x02);

    ILI9488_WriteCmd(0xB6); // Display Function Control
    ILI9488_WriteData(0x02);
    ILI9488_WriteData(0x02); // Source scan direction
    ILI9488_WriteData(0x3B);

    ILI9488_WriteCmd(0xB7); // Entry Mode Set
    ILI9488_WriteData(0xC6);

    ILI9488_WriteCmd(0xF7); // Adjust Control 3
    ILI9488_WriteData(0xA9);
    ILI9488_WriteData(0x51);
    ILI9488_WriteData(0x2C);
    ILI9488_WriteData(0x82);

    ILI9488_WriteCmd(0x11); // Sleep Out
    Delay_ms(120);          // 必须等 >=120ms，不能用不准的忙等循环

    ILI9488_WriteCmd(0x29); // Display ON
    Delay_ms(50);

    // 4. 清屏
    ILI9488_Fill(BLUE);

    // 5. 打开背光
    LCD_BL_ON();
}

/**
 * @brief  设置显示窗口 (GRAM 写入范围)
 * @param  xStart, yStart: 起始坐标
 * @param  xEnd, yEnd: 结束坐标
 */
void ILI9488_SetWindow(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd)
{
    // Column Address Set
    ILI9488_WriteCmd(0x2A);
    ILI9488_WriteData(xStart >> 8);
    ILI9488_WriteData(xStart & 0xFF);
    ILI9488_WriteData(xEnd >> 8);
    ILI9488_WriteData(xEnd & 0xFF);

    // Page Address Set
    ILI9488_WriteCmd(0x2B);
    ILI9488_WriteData(yStart >> 8);
    ILI9488_WriteData(yStart & 0xFF);
    ILI9488_WriteData(yEnd >> 8);
    ILI9488_WriteData(yEnd & 0xFF);

    // Memory Write
    ILI9488_WriteCmd(0x2C);
}

/**
 * @brief  全屏填充颜色
 * @param  color: RGB565 颜色值
 */
void ILI9488_Fill(uint16_t color)
{
    uint32_t i;
    uint32_t total_pixels = (uint32_t)LCD_WIDTH * LCD_HEIGHT;

    ILI9488_SetWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

   
    
    for(i = 0; i < total_pixels; i++)
    {
        LCD_DATA_ADDR = color;
    }
}







/**
 * @brief  画点
 */
void ILI9488_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    ILI9488_SetWindow(x, y, x, y);
    LCD_DATA_ADDR = color;
}

/**
 * @brief  背光控制
 */
void ILI9488_Backlight_On(void)
{
    LCD_BL_ON();
}

void ILI9488_Backlight_Off(void)
{
    LCD_BL_OFF();
}

#include "stm32f4xx.h"



/* NDTR 是 16 位寄存器：一次最多搬 65535 个半字 */
#define LCD_DMA_MAX_NDTR    65535UL

#define LCD_DMA_ALL_FLAGS   (DMA_FLAG_TCIF0 | DMA_FLAG_HTIF0 | DMA_FLAG_TEIF0 | \
                             DMA_FLAG_DMEIF0 | DMA_FLAG_FEIF0)

void FSMC_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

    /* MEM2MEM 位只能在 EN=0 时修改，所以先关流再配 */
    DMA_Cmd(DMA2_Stream0, DISABLE);
    while (DMA_GetCmdStatus(DMA2_Stream0) != DISABLE) {}
    DMA_DeInit(DMA2_Stream0);
    DMA_ClearFlag(DMA2_Stream0, LCD_DMA_ALL_FLAGS);

    DMA_StructInit(&DMA_InitStructure);

    DMA_InitStructure.DMA_Channel            = DMA_Channel_0;   /* M2M 下无意义 */

    /* ★ 关键：必须是 MemoryToMemory（宏值 0x80 = MEM2MEM 位置位 + DIR=10） */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_MemoryToMemory;
    DMA_InitStructure.DMA_BufferSize         = 1;               /* 每次传输前重填 */

    /* ★ M2M 角色互换：PAR = 源（帧缓冲，递增），M0AR = 目标（LCD，固定） */
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)0;                 /* 传输前填源 */
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)LCD_DATA_ADDR_VAL; /* 固定目标 */

    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Enable;    /* 源地址递增 */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Disable;       /* 目标地址固定 */

    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;

    /* ⚠️ 这里刻意【不】开 FIFO/Burst：INC4 突发要求地址 8 字节对齐且长度是 4 的倍数，
     *    而帧缓冲按块切分(30720B)、最后一块还可能不足；一旦不满足会直接置 TEIF。
     *    先跑通 Single + 直连模式，要再提速单独调。 */
    DMA_InitStructure.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;

    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;  /* 低于 SDIO 的 VeryHigh */

    DMA_Init(DMA2_Stream0, &DMA_InitStructure);
    DMA_ClearFlag(DMA2_Stream0, LCD_DMA_ALL_FLAGS);
}


void LCD_DMA_Transfer(uint32_t buffer_addr, uint32_t total_pixels)
{
    uint32_t src = buffer_addr;

    while (total_pixels > 0) {
        /* 单次最大传输长度受限于 NDTR (16-bit) */
        uint32_t chunk = (total_pixels > LCD_DMA_MAX_NDTR) 
                         ? LCD_DMA_MAX_NDTR 
                         : total_pixels;

        /* 1. 关流并等待真正关闭 */
        DMA_Cmd(DMA2_Stream0, DISABLE);
        while (DMA_GetCmdStatus(DMA2_Stream0) != DISABLE) {}

        /* 2. 清标志 + 配置本次传输 */
        DMA_ClearFlag(DMA2_Stream0, LCD_DMA_ALL_FLAGS);
        DMA2_Stream0->PAR = src;
        DMA_SetCurrDataCounter(DMA2_Stream0, (uint16_t)chunk);

        /* 3. 确保之前的 FSMC 写命令(0x2C)已到达总线 */
        __DSB();
        
        /* 4. 启动并阻塞等待完成 */
        DMA_Cmd(DMA2_Stream0, ENABLE);
        while (DMA_GetFlagStatus(DMA2_Stream0, DMA_FLAG_TCIF0) == RESET) {}

        /* 5. 更新指针与计数 */
        src          += chunk * 2;  // RGB565 = 2 Bytes/Pixel
        total_pixels -= chunk;
    }
    
    /* 传输结束后清理标志，为下一帧做准备 */
    DMA_ClearFlag(DMA2_Stream0, LCD_DMA_ALL_FLAGS);
}
