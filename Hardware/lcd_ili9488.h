#ifndef __ILI9488_H
#define __ILI9488_H

#include "stm32f4xx.h"

// ================= 用户需根据实际硬件修改的部分 =================
#define LCD_RST_PORT    GPIOG
#define LCD_RST_PIN     GPIO_Pin_15
#define LCD_RST_CLK     RCC_AHB1Periph_GPIOG

#define LCD_BL_PORT     GPIOB
#define LCD_BL_PIN      GPIO_Pin_15
#define LCD_BL_CLK      RCC_AHB1Periph_GPIOB

#define LCD_BL_ON()     GPIO_SetBits(LCD_BL_PORT, LCD_BL_PIN)
#define LCD_BL_OFF()    GPIO_ResetBits(LCD_BL_PORT, LCD_BL_PIN)

#define LCD_RST_SET()   GPIO_SetBits(LCD_RST_PORT, LCD_RST_PIN)
#define LCD_RST_CLR()   GPIO_ResetBits(LCD_RST_PORT, LCD_RST_PIN)

// ================= FSMC 地址定义 (已修正) =================
// Bank1 NOR/SRAM4 基地址: 0x6C000000
// RS 接在 FSMC_A6 (PF12) 上
// 16位数据宽度下，外部 An 对应字节偏移 = 1 << (n+1)
// A6 → 字节偏移 = 1 << 7 = 0x80
#define LCD_BASE_ADDR       ((uint32_t)0x6C000000UL)
#define LCD_CMD_ADDR_VAL    (LCD_BASE_ADDR) 
#define LCD_DATA_ADDR_VAL   (LCD_BASE_ADDR + 0x80UL) 



#define LCD_CMD_ADDR        (*(volatile uint16_t *)(LCD_CMD_ADDR_VAL))            // A6=0 → RS=0 (命令)
#define LCD_DATA_ADDR       (*(volatile uint16_t *)(LCD_DATA_ADDR_VAL))   // A6=1 → RS=1 (数据)

// ================= 屏幕参数 =================
#define LCD_WIDTH       320
#define LCD_HEIGHT      480




// ================= 颜色定义 (RGB565) =================
#define WHITE           0xFFFF
#define BLACK           0x0000
#define BLUE            0x001F
#define RED             0xF800
#define GREEN           0x07E0
#define CYAN            0x07FF
#define MAGENTA         0xF81F
#define YELLOW          0xFFE0

// ================= 函数声明 =================
void ILI9488_Pin_Init(void);
void ILI9488_Init(void);
void BSP_SPI_SetSpeed(uint8_t prescaler);
void ILI9488_WriteCmd(uint8_t cmd);
void ILI9488_WriteData(uint16_t data);
void ILI9488_SetWindow(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd);
void ILI9488_Fill(uint16_t color);
void ILI9488_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void ILI9488_Backlight_On(void);
void ILI9488_Backlight_Off(void);
void ILI9488_ShowBMP(uint16_t x, uint16_t y, const char* filename);
void FSMC_DMA_Init(void);
void LCD_DMA_Transfer(uint32_t buffer_addr, uint32_t pixel_count);
#endif /* __ILI9488_H */

