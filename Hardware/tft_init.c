/* ============================================================================
 *  tft_init.c  ——  ST7735S(SPI)  →  ILI9488(FSMC 8080 16位并口) 适配层
 *
 *  设计原则：lcd_ili9488.c / lcd_ili9488.h 原封不动（那份已经调通了），
 *            只在这里把 tft.c 依赖的 6 个函数重新实现一遍。
 *
 *  替换方式：用本文件覆盖 Hardware/tft_init.c
 *            （同时用 tft_init.h 覆盖 Hardware/tft_init.h）
 *
 *  对应硬件：
 *    FSMC Bank1 NORSRAM4  CS = NE4 = PG12     基地址 0x6C000000
 *    RS/DC = FSMC_A6 = PF12                   数据口偏移 1<<(6+1) = 0x80
 *    数据总线 D0-D15 = PD14/PD15/PD0/PD1/PE7..PE15/PD8/PD9/PD10
 *    NOE = PD4, NWE = PD5
 *    RST = PG15, BLK = PB15
 * ========================================================================== */

#include "tft_init.h"
#include "lcd_ili9488.h"
#include "stm32f4xx_dma.h"

/* ============================================================================
 *  ★ 默认用 CPU 循环写，不走 DMA。理由：
 *    1) FSMC 在 F4 上没有 DMA 请求线，只能用 DMA2 的 MEM2MEM 模式软件触发；
 *    2) MEM2MEM 的吞吐和 CPU 循环【完全一样】，两者都被 FSMC 的写时序卡住
 *       （ADDSET=2 + DATAST=0x0A → 14 HCLK ≈ 83ns/像素 → 160x128 ≈ 1.7ms/帧）；
 *    3) MEM2MEM 会长时间霸占 AHB 总线，和 SDIO 的 DMA2_Stream3 抢带宽。
 *  结论：收益几乎为零、风险却多一层，所以默认走 CPU。
 *  想实验 DMA 就把下面这行注释掉（地址方向务必看清 TFT_DMA_SendBlock 的注释）。
 * ========================================================================== */
#define TFT_USE_CPU_WRITE

/* ============================================================================
 *  DMA 配置
 *
 *  ⚠️ FSMC 在 F4 上没有 DMA 请求线，所以只能用 DMA2 的
 *     "存储器到存储器(MEM2MEM)" 模式，由软件触发。
 *  ⚠️ MEM2MEM 只有 DMA2 支持（DMA1 不支持）。
 *  ⚠️ 必须避开 SDIO 占用的 DMA2_Stream3 → 这里用 DMA2_Stream0。
 * ========================================================================== */
#define TFT_DMA_STREAM      DMA2_Stream0

#define TFT_DMA_TCIF        DMA_FLAG_TCIF0
#define TFT_DMA_HTIF        DMA_FLAG_HTIF0
#define TFT_DMA_TEIF        DMA_FLAG_TEIF0
#define TFT_DMA_DMEIF       DMA_FLAG_DMEIF0
#define TFT_DMA_FEIF        DMA_FLAG_FEIF0
#define TFT_DMA_ALL_FLAGS   (TFT_DMA_TCIF | TFT_DMA_HTIF | TFT_DMA_TEIF | \
                             TFT_DMA_DMEIF | TFT_DMA_FEIF)

/* NDTR 是 16 位寄存器，单次最大 65535 个半字 */
#define TFT_DMA_MAX_NDTR    65535UL

/* -------------------------------------------------------------------------- */
static void TFT_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

    /* 先确保流已停止，再改配置 */
    DMA_Cmd(TFT_DMA_STREAM, DISABLE);
    while(DMA_GetCmdStatus(TFT_DMA_STREAM) != DISABLE);
    DMA_DeInit(TFT_DMA_STREAM);

    DMA_StructInit(&DMA_InitStructure);

    DMA_InitStructure.DMA_Channel            = DMA_Channel_0;  /* M2M 与通道无关 */
    /* ★★★ MEM2MEM 模式下：PAR = 源地址，M0AR = 目标地址 ★★★
     *   参考 ST 官方 DMA_FLASHToRAM 例程：
     *       DMA_PeripheralBaseAddr = (uint32_t)aSRC_Const_Buffer;   // 源
     *       DMA_Memory0BaseAddr    = (uint32_t)aDST_Buffer;         // 目标
     *   所以这里 PAR 每次传输前填帧缓冲、并且要递增；
     *        M0AR 固定为 LCD 数据口、不递增。
     *   （HAL 库的 DMA_SetConfig 也是这么分的：Direction != MEMORY_TO_PERIPH
     *     时 PAR=Src、M0AR=Dst）                                                     */
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)0;    /* 每次传输前填 = 源 */
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)LCD_DATA_ADDR_VAL; /* 固定 = 目标(LCD) */
    DMA_InitStructure.DMA_DIR                = DMA_DIR_MemoryToMemory;
    DMA_InitStructure.DMA_BufferSize         = 1;              /* 每次传输前再填 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Enable;  /* 源递增 */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Disable;     /* 目标固定 */
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High; /* 低于 SDIO 的 VeryHigh */
    DMA_InitStructure.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(TFT_DMA_STREAM, &DMA_InitStructure);

    DMA_ClearFlag(TFT_DMA_STREAM, TFT_DMA_ALL_FLAGS);
}

/* ----------------------------------------------------------------------------
 *  用 M2M DMA 把一段像素推到 FSMC 数据口
 *  pixels 必须 <= TFT_DMA_MAX_NDTR
 * -------------------------------------------------------------------------- */
#if !defined(TFT_USE_CPU_WRITE)
static void TFT_DMA_SendBlock(const uint16_t *src, uint32_t pixels)
{
    /* 等上一次传输真正结束（Normal 模式下 EN 会自动清零） */
    DMA_Cmd(TFT_DMA_STREAM, DISABLE);
    while(DMA_GetCmdStatus(TFT_DMA_STREAM) != DISABLE);

    DMA_ClearFlag(TFT_DMA_STREAM, TFT_DMA_ALL_FLAGS);

    TFT_DMA_STREAM->PAR  = (uint32_t)src;   /* ★ M2M: PAR = 源（帧缓冲），递增 */
    /* M0AR 已在 TFT_DMA_Init 里固定成 LCD 数据口，这里不用改 */
    DMA_SetCurrDataCounter(TFT_DMA_STREAM, (uint16_t)pixels);

    /* ★ 关键：0x2C(写 GRAM) 命令是通过 FSMC 的写缓冲发出去的，
     *   必须等它真正落到总线上，DMA 才能开始往数据口灌像素，
     *   否则第一个像素可能被当成命令吃掉。__DSB() 保证写完成。 */
    __DSB();

    DMA_Cmd(TFT_DMA_STREAM, ENABLE);

    while(DMA_GetFlagStatus(TFT_DMA_STREAM, TFT_DMA_TCIF) == RESET);

    DMA_ClearFlag(TFT_DMA_STREAM, TFT_DMA_ALL_FLAGS);
}
#endif  /* !TFT_USE_CPU_WRITE */

/* ============================================================================
 *  低层接口实现
 * ========================================================================== */

/* 写命令：RS = 0 */
void TFT_WR_REG(uint8_t reg)
{
    LCD_CMD_ADDR = (uint16_t)reg;
}

/* 写数据：RS = 1（16 位） */
void TFT_WR_DATA(uint16_t data)
{
    LCD_DATA_ADDR = data;
}

/* 写数据：RS = 1（8 位参数，值落在总线的 D7-D0 上） */
void TFT_WR_DATA8(uint8_t data)
{
    LCD_DATA_ADDR = (uint16_t)data;
}

/* 设置显示窗口：0x2A / 0x2B / 0x2C */
void TFT_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    ILI9488_SetWindow(x1, y1, x2, y2);
}

/* ----------------------------------------------------------------------------
 *  整帧推送（tft.c 的 TFT_DrawPicture_DMA 调用它）
 *  len = 字节数；像素数 = len / 2
 * -------------------------------------------------------------------------- */
void TFT_WR_DATA_DMA(uint8_t *buff, uint32_t len)
{
    const uint16_t *p = (const uint16_t *)buff;
    uint32_t pixels   = len >> 1;      /* 字节 → 像素 */

#if defined(TFT_USE_CPU_WRITE)
    /* ---- 方案 A：CPU 循环写（零风险，占 CPU） ---- */
    while(pixels--)
    {
        LCD_DATA_ADDR = *p++;
    }
#else
    /* ---- 方案 B：DMA2 MEM2MEM 分块推送 ---- */
    while(pixels > 0UL)
    {
        uint32_t n = (pixels > TFT_DMA_MAX_NDTR) ? TFT_DMA_MAX_NDTR : pixels;

        TFT_DMA_SendBlock(p, n);

        p      += n;
        pixels -= n;
    }
#endif
}

/* ----------------------------------------------------------------------------
 *  屏幕初始化
 * -------------------------------------------------------------------------- */
void TFT_Init(void)
{
    /* ILI9488_Init() 内部已经完成：
     *   ILI9488_Pin_Init()  → GPIO 复用 + FSMC Bank4 配置
     *   硬件复位（CLR 20ms → SET 120ms）
     *   初始化序列（0x3A = 0x55，16bit RGB565）
     *   ILI9488_Fill(BLUE) 全屏清蓝（约 12.8ms）
     *   LCD_BL_ON() 开背光
     */
    ILI9488_Init();

    /* FSMC 数据口已经可用，再配 DMA */
    TFT_DMA_Init();
}
