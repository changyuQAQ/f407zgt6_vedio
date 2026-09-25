#include "wm8978.h"
#include "Delay.h"
#include <string.h>

/* ============================================================================
 *  本地寄存器镜像
 *  很多配置是"读出来改几位再写回去"（音量、输入增益等），WM8978 又不能
 *  可靠地回读，所以这里自己记一份。所有写入都必须经过 WM8978_WriteReg()。
 * ========================================================================== */
static uint16_t wm8978_regval[128];

/* ============================================================================
 *  I2C1 配置（硬件 I2C，PB8 = SCL，PB9 = SDA）
 * ========================================================================== */
static void WM8978_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef  I2C_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(WM8978_I2C_CLK, ENABLE);

    GPIO_PinAFConfig(WM8978_SCL_PORT, WM8978_SCL_SRC, GPIO_AF_I2C1);
    GPIO_PinAFConfig(WM8978_SDA_PORT, WM8978_SDA_SRC, GPIO_AF_I2C1);

    /* I2C 必须【开漏】。外部要有 4.7k 上拉到 3.3V；
     * 板子上没焊的话这里开内部上拉顶一下，但内部上拉偏弱，长线可能不够 */
    GPIO_InitStructure.GPIO_Pin   = WM8978_SCL_PIN | WM8978_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Fast_Speed;
    GPIO_Init(WM8978_SCL_PORT, &GPIO_InitStructure);

    I2C_DeInit(WM8978_I2C);
    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle           = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1         = 0x00;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed          = 400000;
    I2C_Init(WM8978_I2C, &I2C_InitStructure);
    I2C_Cmd(WM8978_I2C, ENABLE);
}

/* ============================================================================
 *  写寄存器 —— 注意字节格式，见 wm8978.h 顶部说明
 *  返回 0 成功，非 0 表示失败在哪一步
 * ========================================================================== */
uint8_t WM8978_WriteReg(uint8_t reg, uint16_t val)
{
    uint32_t timeout;

    timeout = 0xFFFF;
    while(I2C_GetFlagStatus(WM8978_I2C, I2C_FLAG_BUSY) && timeout--) {}
    if(timeout == 0) return 1;

    I2C_GenerateSTART(WM8978_I2C, ENABLE);
    timeout = 0xFFFF;
    while(!I2C_CheckEvent(WM8978_I2C, I2C_EVENT_MASTER_MODE_SELECT) && timeout--) {}
    if(timeout == 0) { I2C_GenerateSTOP(WM8978_I2C, ENABLE); return 2; }

    I2C_Send7bitAddress(WM8978_I2C, WM8978_I2C_ADDR, I2C_Direction_Transmitter);
    timeout = 0xFFFF;
    while(!I2C_CheckEvent(WM8978_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) && timeout--) {}
    if(timeout == 0) { I2C_GenerateSTOP(WM8978_I2C, ENABLE); return 3; }

    /* ★ 第 1 字节：寄存器地址(bit7:1) + 数据的最高位(bit0) */
    I2C_SendData(WM8978_I2C, (uint8_t)((reg << 1) | ((val >> 8) & 0x01)));
    timeout = 0xFFFF;
    while(!I2C_CheckEvent(WM8978_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTING) && timeout--) {}
    if(timeout == 0) { I2C_GenerateSTOP(WM8978_I2C, ENABLE); return 4; }

    /* ★ 第 2 字节：数据低 8 位 */
    I2C_SendData(WM8978_I2C, (uint8_t)(val & 0xFF));
    timeout = 0xFFFF;
    while(!I2C_CheckEvent(WM8978_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED) && timeout--) {}

    I2C_GenerateSTOP(WM8978_I2C, ENABLE);
    if(timeout == 0) return 5;

    if(reg < 128) wm8978_regval[reg] = val;
    return 0;
}

/* WM8978 不能可靠回读，返回本地镜像 */
uint16_t WM8978_ReadReg(uint8_t reg)
{
    return (reg < 128) ? wm8978_regval[reg] : 0;
}

/* ============================================================================
 *  初始化 —— 数值取自配套 MP3 参考代码，已按"外接耳机"调整
 * ========================================================================== */
uint8_t WM8978_Init(void)
{
    memset(wm8978_regval, 0, sizeof(wm8978_regval));
    WM8978_I2C_Init();

    if(WM8978_WriteReg(0, 0x000)) return 1;     /* 软复位 */
    Delay_ms(2);

    /* R1  电源管理1：MICEN=1 BIASEN=1 VMIDSEL=11(5k) */
    if(WM8978_WriteReg(1, 0x1B)) return 2;

    /* R2  电源管理2：LOUT1EN/ROUT1EN=1 —— ★ 耳机就走这一路
     *                 BOOSTENL/BOOSTENR=1 */
    if(WM8978_WriteReg(2, 0x1B0)) return 3;

    /* R3  电源管理3：LMIXEN/RMIXEN + SPKBST2 打开混音器；
     *                 保留 LOUT2EN/ROUT2EN(bit8/7)=0 —— 外接耳机不用喇叭 */
    if(WM8978_WriteReg(3, 0x06C)) return 4;

    /* R6  时钟：MS=0 从机（BCLK/LRCK 由 STM32 提供），MCLKDIV=1，CLKSEL=MCLK */
    if(WM8978_WriteReg(6, 0x000)) return 5;

    /* R43 [4]INVROUT2 反向（驱动喇叭用，耳机不受影响） */
    if(WM8978_WriteReg(43, 1 << 4)) return 6;

    /* R47/R48 PGABOOST 左/右 MIC 20dB（放音用不到，保持参考值） */
    if(WM8978_WriteReg(47, 1 << 8)) return 7;
    if(WM8978_WriteReg(48, 1 << 8)) return 8;

    /* R49 [1]TSDEN 过热保护 */
    if(WM8978_WriteReg(49, 1 << 1)) return 9;

    /* R10/R14 128x 过采样，最佳 SNR */
    if(WM8978_WriteReg(10, 1 << 3)) return 10;
    if(WM8978_WriteReg(14, 1 << 3)) return 11;

    return 0;
}

/* ============================================================================
 *  I2S 工作模式
 *    fmt: 0=右对齐  1=左对齐  2=飞利浦标准I2S  3=PCM/DSP
 *    len: 0=16bit   1=20bit   2=24bit           3=32bit
 *  I2S2 配的是 I2S_Standard_Phillips + 16bit → 必须 WM8978_I2S_Cfg(2, 0)
 *  这两边不一致的话，声音会变成噪声或者干脆没声。
 * ========================================================================== */
void WM8978_I2S_Cfg(uint8_t fmt, uint8_t len)
{
    WM8978_WriteReg(4, (uint16_t)(((fmt & 0x03) << 3) | ((len & 0x03) << 5)));
}

/* dacen=1 开 DAC，adcen=1 开 ADC */
void WM8978_ADDA_Cfg(uint8_t dacen, uint8_t adcen)
{
    uint16_t regval;

    regval = WM8978_ReadReg(3);
    if(dacen) regval |= (3 << 0); else regval &= (uint16_t)~(3 << 0);
    WM8978_WriteReg(3, regval);

    regval = WM8978_ReadReg(2);
    if(adcen) regval |= (3 << 0); else regval &= (uint16_t)~(3 << 0);
    WM8978_WriteReg(2, regval);
}

/* dacen=1 把 DAC 接到输出混音器；bpsen=1 输入直通输出（录音监听） */
void WM8978_Output_Cfg(uint8_t dacen, uint8_t bpsen)
{
    uint16_t regval = 0;

    if(dacen) regval |= (1 << 0);
    if(bpsen)
    {
        regval |= (1 << 1);
        regval |= (5 << 2);
    }
    WM8978_WriteReg(50, regval);
    WM8978_WriteReg(51, regval);
}

/* 耳机音量 0~63，数值越大越轻；0 直接静音 */
void WM8978_HPvol_Set(uint8_t voll, uint8_t volr)
{
    voll &= 0x3F;
    volr &= 0x3F;
    if(voll == 0) voll |= (1 << 6);
    if(volr == 0) volr |= (1 << 6);
    WM8978_WriteReg(52, voll);
    WM8978_WriteReg(53, (uint16_t)(volr | (1 << 8)));   /* bit8 = HPVU 同步更新 */
}

/* 喇叭音量 0~63（外接耳机时用不到） */
void WM8978_Spkvol_Set(uint8_t volx)
{
    volx &= 0x3F;
    if(volx == 0) volx |= (1 << 6);
    WM8978_WriteReg(54, volx);
    WM8978_WriteReg(55, (uint16_t)(volx | (1 << 8)));
}

/* 软静音。暂停/恢复时用它避免爆音，比直接停 DMA 干净 */
void WM8978_Mute(uint8_t mute)
{
    uint16_t regval = WM8978_ReadReg(10);
    if(mute) regval |= (1 << 5);        /* R10 [5] DACMU */
    else     regval &= (uint16_t)~(1 << 5);
    WM8978_WriteReg(10, regval);
}

/* ============================================================================
 *  外接耳机的一次性配置
 *  顺序不能乱：基础寄存器 → I2S 格式 → 开 DAC 和输出 → 音量
 * ========================================================================== */
uint8_t WM8978_HeadphoneInit(uint8_t volume)
{
    if(WM8978_Init() != 0) return 1;

    WM8978_I2S_Cfg(2, 0);           /* 飞利浦 I2S / 16bit —— 必须和 I2S2 一致 */
    WM8978_ADDA_Cfg(1, 0);          /* 开 DAC，关 ADC */
    WM8978_Output_Cfg(1, 0);        /* DAC → 输出混音器 */
    WM8978_HPvol_Set(volume, volume);
    WM8978_Mute(0);

    return 0;
}
