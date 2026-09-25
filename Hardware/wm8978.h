#ifndef __WM8978_H
#define __WM8978_H

#include "stm32f4xx.h"

/* ============================================================================
 *  WM8978 立体声编解码器驱动（外接耳机播放）
 *
 *  两条通路：
 *    ① 控制口：I2C1 —— SCL = PB8, SDA = PB9（硬件 I2C，需 4.7k 上拉到 3.3V）
 *    ② 数据口：I2S2 —— 见 audio.h
 *
 *  ★ WM8978 的 I2C 写格式和一般器件不一样，只发 2 个字节：
 *        [器件地址+W] [ reg(bit7:1) | data的bit8(bit0) ] [ data(bit7:0) ]
 *    寄存器地址 7 位和数据最高位是【打包在同一个字节】里的，
 *    写成"先发 reg 再发高位"会写到错误的寄存器，而且不会报错。
 * ========================================================================== */

#define WM8978_I2C              I2C1
#define WM8978_I2C_CLK          RCC_APB1Periph_I2C1

#define WM8978_SCL_PORT         GPIOB
#define WM8978_SCL_PIN          GPIO_Pin_8
#define WM8978_SCL_SRC          GPIO_PinSource8

#define WM8978_SDA_PORT         GPIOB
#define WM8978_SDA_PIN          GPIO_Pin_9
#define WM8978_SDA_SRC          GPIO_PinSource9

#define WM8978_I2C_ADDR         0x34    /* 7bit 0x1A << 1 */

/* 参数含义见 wm8978.c 里的函数注释 */
uint8_t WM8978_Init(void);                                  /* 0 = 成功 */
uint8_t WM8978_WriteReg(uint8_t reg, uint16_t val);
uint16_t WM8978_ReadReg(uint8_t reg);                       /* 读本地缓存 */

void WM8978_I2S_Cfg(uint8_t fmt, uint8_t len);              /* fmt:2=I2S  len:0=16bit */
void WM8978_ADDA_Cfg(uint8_t dacen, uint8_t adcen);
void WM8978_Output_Cfg(uint8_t dacen, uint8_t bpsen);
void WM8978_HPvol_Set(uint8_t voll, uint8_t volr);          /* 耳机音量 0~63，0 最响 */
void WM8978_Spkvol_Set(uint8_t volx);                       /* 喇叭音量（外接耳机时不用） */
void WM8978_Mute(uint8_t mute);                             /* 1 = 软静音 */

/* 外接耳机的一次性配置：初始化 + I2S 格式 + 开 DAC + 开耳机输出 + 音量 */
uint8_t WM8978_HeadphoneInit(uint8_t volume);

#endif /* __WM8978_H */
