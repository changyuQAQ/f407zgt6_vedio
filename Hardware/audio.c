
#include "audio.h"
#include "wm8978.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

/* ============================================================================
 

 SD卡 -> Audio_FeedFrame -> CCM FIFO -> DMA中断取数 -> SRAM1双缓冲 -> I2S -> WM8978

 * - audio_stream_on: 1=DMA运行中, 0=预填充阶段(DMA停)
 * - audio_paused:    1=暂停(丢数据,DMA停), 0=正常/预填充

 * ========================================================================== */

#define AUDIO_BUF_SECTION   __attribute__((section(".dma_buf"), aligned(4)))
#define AUDIO_CCM_SECTION   __attribute__((section(".ccm_buf"), aligned(4)))

#define HALF_WORDS          AUDIO_CHUNK_HALFWORDS
#define BUF_WORDS           (AUDIO_CHUNK_HALFWORDS * 2)
#define FIFO_MASK           (AUDIO_FIFO_WORDS - 1)

/* ============================================================================
 * 1. 缓冲区与状态量
 * ========================================================================== */

static uint16_t audio_buf[BUF_WORDS]  AUDIO_BUF_SECTION; /* DMA目标: A/B双半缓冲 */
static uint16_t audio_fifo[AUDIO_FIFO_WORDS] AUDIO_CCM_SECTION; /* 软件FIFO: CPU读写 */

#if (AUDIO_FIFO_WORDS & (AUDIO_FIFO_WORDS - 1)) != 0
#error "AUDIO_FIFO_WORDS must be power of 2"
#endif

typedef struct {
    volatile uint32_t rd;      /* 读指针 (ISR更新) */
    volatile uint32_t wr;      /* 写指针 (Task更新) */
    volatile uint32_t count;   /* 有效数据字数 */
} fifo_ctrl_t;

static fifo_ctrl_t fifo;

/* 播放状态 */
static volatile uint8_t  audio_stream_on = 0;
static volatile uint8_t  audio_paused    = 0;
static volatile uint32_t audio_underruns = 0; /* 欠载计数 */

/* 预填充状态机 */
#define PRE_FILL_A   0
#define PRE_FILL_B   1
#define PRE_DONE     2
static volatile uint8_t pre_state   = PRE_FILL_A;
static uint16_t         pre_buf[HALF_WORDS] AUDIO_BUF_SECTION; /* 暂存半块数据 */
static uint32_t         pre_fill_wr = 0;

static AudioStream cur_stream;
static uint8_t     audio_configured = 0;

/* ============================================================================
 * 2. 软件 FIFO 基本操作
 * ========================================================================== */

static __inline uint32_t Audio_FifoLevel(void) { return fifo.count; }
static __inline uint32_t Audio_FifoSpace(void) { return AUDIO_FIFO_WORDS - fifo.count; }

/* 写入FIFO (需在临界区内调用) */
static void Audio_FifoWrite(const uint16_t *src, uint32_t n)
{
    uint32_t w    = fifo.wr;
    uint32_t tail = AUDIO_FIFO_WORDS - w;

    if (n <= tail) {
        memcpy(&audio_fifo[w], src, n * sizeof(uint16_t));
    } else {
        memcpy(&audio_fifo[w], src, tail * sizeof(uint16_t));
        memcpy(&audio_fifo[0], src + tail, (n - tail) * sizeof(uint16_t));
    }

    fifo.wr = (w + n) & FIFO_MASK;
    fifo.count += n;
}

/* 从FIFO读取 (仅在ISR中调用) */
static void Audio_FifoReadOut(uint16_t *dst, uint32_t n)
{
    uint32_t r    = fifo.rd;
    uint32_t tail = AUDIO_FIFO_WORDS - r;

    if (n <= tail) {
        memcpy(dst, &audio_fifo[r], n * sizeof(uint16_t));
    } else {
        memcpy(dst, &audio_fifo[r], tail * sizeof(uint16_t));
        memcpy(dst + tail, &audio_fifo[0], (n - tail) * sizeof(uint16_t));
    }

    fifo.rd = (r + n) & FIFO_MASK;
    fifo.count -= n;
}

/* ============================================================================
 * 3. DMA 底层与中断
 * ========================================================================== */

/* 手动清除 DMA1 Stream4 高地址组标志位 (HIFCR)
 * 注意: 标准库函数可能误操作低地址组(LIFCR)，此处直接寄存器操作确保可靠清除 */
static void Audio_DMA_ClearAllFlags(void)
{
    DMA1->HIFCR = AUDIO_DMA_TCIF | AUDIO_DMA_HTIF |
                  AUDIO_DMA_TEIF | AUDIO_DMA_DMEIF | AUDIO_DMA_FEIF;
}

/* 停止DMA并禁用中断，防止竞态 */
static void Audio_DmaStop(void)
{
    NVIC_DisableIRQ(AUDIO_DMA_IRQn);
    DMA_Cmd(AUDIO_DMA_STREAM, DISABLE);
    Audio_DMA_ClearAllFlags();
    NVIC_EnableIRQ(AUDIO_DMA_IRQn);
}

/* 启动DMA播放 (需重置NDTR以对齐相位) */
static void Audio_DmaStart(void)
{
    Audio_DMA_ClearAllFlags();
    AUDIO_DMA_STREAM->NDTR = BUF_WORDS;
    DMA_Cmd(AUDIO_DMA_STREAM, ENABLE);
    audio_stream_on = 1;
}

/* 填充半个DMA缓冲区
 * 若FIFO数据不足，使用最后采样值保持(hold)而非补零，避免爆音；记录欠载次数 */
static void Audio_FillHalf(uint8_t half)
{
    uint16_t *dst = &audio_buf[(uint32_t)half * HALF_WORDS];
    uint32_t  n   = Audio_FifoLevel();

    if (n > HALF_WORDS) n = HALF_WORDS;      
    Audio_FifoReadOut(dst, n);

    if (n < HALF_WORDS) {
        uint16_t hold = (n > 0) ? dst[n - 1] : 0;
        for (uint32_t i = n; i < HALF_WORDS; i++) dst[i] = hold;
        audio_underruns++;
    }
}

/* DMA1_Stream4 中断服务程序
 * HT: 前半缓冲传完 -> 回填A区
 * TC: 后半缓冲传完 -> 回填B区, DMA自动回卷到A
 * 优先级6 >= configMAX_SYSCALL_INTERRUPT_PRIORITY，可被临界区屏蔽 */
void Audio_DMA_IRQHandler(void)
{
    if (DMA1->HISR & AUDIO_DMA_HTIF) {
        DMA1->HIFCR = AUDIO_DMA_HTIF;
        Audio_FillHalf(0);
    }
    if (DMA1->HISR & AUDIO_DMA_TCIF) {
        DMA1->HIFCR = AUDIO_DMA_TCIF;
        Audio_FillHalf(1);
    }
}

/* ============================================================================
 * 4. 硬件初始化
 * ========================================================================== */

static void Audio_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC, ENABLE);

    /* PB12:WS, PB13:CK, PC3:SD, PC6:MCK */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource12, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource3,  GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource6,  GPIO_AF_SPI2);

    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_6;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

static void Audio_I2S_Init(void)
{
    I2S_InitTypeDef I2S_InitStructure;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    I2S_InitStructure.I2S_Mode       = I2S_Mode_MasterTx;
    I2S_InitStructure.I2S_Standard   = I2S_Standard_Phillips;
    I2S_InitStructure.I2S_DataFormat = I2S_DataFormat_16b;
    I2S_InitStructure.I2S_MCLKOutput = I2S_MCLKOutput_Enable;
    I2S_InitStructure.I2S_AudioFreq  = AUDIO_SAMPLE_RATE;
    I2S_InitStructure.I2S_CPOL       = I2S_CPOL_Low;
    
    I2S_Init(SPI2, &I2S_InitStructure);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    I2S_Cmd(SPI2, ENABLE);
}

static void Audio_DMA_Init(void)
{
    DMA_InitTypeDef  DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    DMA_DeInit(AUDIO_DMA_STREAM);

    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_Channel            = AUDIO_DMA_CHANNEL;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)audio_buf;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_BufferSize         = BUF_WORDS;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(AUDIO_DMA_STREAM, &DMA_InitStructure);

    DMA_ITConfig(AUDIO_DMA_STREAM, DMA_IT_TC | DMA_IT_HT, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = AUDIO_DMA_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    Audio_DMA_ClearAllFlags();
}

uint8_t Audio_Init(uint8_t n)
{
    Audio_GPIO_Init();
    Audio_I2S_Init();
    Audio_DMA_Init();

    /* WM8978依赖外部MCLK，需在I2S启动后初始化 */
    if (WM8978_HeadphoneInit(n) != 0) return 1;

    memset(audio_buf,  0, sizeof(audio_buf));
    memset(audio_fifo, 0, sizeof(audio_fifo));

    audio_stream_on = 0;
    audio_paused    = 0;
    audio_underruns = 0;
    fifo.rd = fifo.wr = 0;
    fifo.count  = 0;
    pre_state   = PRE_FILL_A;
    pre_fill_wr = 0;
    audio_configured = 0;
    memset(&cur_stream, 0, sizeof(cur_stream));

    DMA_Cmd(AUDIO_DMA_STREAM, DISABLE);
    return 0;
}

/* ============================================================================
 * 5. 预填充 / 喂数据
 * ========================================================================== */

/* 重置流水线状态 (不memset缓冲区，由后续覆盖或静音处理) */
static void Audio_ResetPipeline(void)
{
    audio_stream_on = 0;
    audio_paused    = 0;
    audio_underruns = 0;
    fifo.rd = fifo.wr = 0;
    fifo.count  = 0;
    pre_state   = PRE_FILL_A;
    pre_fill_wr = 0;
}

/* 预填充逻辑：先填满A/B双缓冲，再填FIFO，达到阈值后启动DMA */
static void Audio_PreFill(const uint16_t *src, uint32_t nwords)
{
    while (nwords > 0 && pre_state < PRE_DONE) {
        uint32_t take = HALF_WORDS - pre_fill_wr;
        if (take > nwords) take = nwords;

        memcpy(&pre_buf[pre_fill_wr], src, take * sizeof(uint16_t));
        src         += take;
        nwords      -= take;
        pre_fill_wr += take;

        if (pre_fill_wr >= HALF_WORDS) {
            memcpy(&audio_buf[(uint32_t)pre_state * HALF_WORDS], pre_buf,
                   HALF_WORDS * sizeof(uint16_t));
            pre_fill_wr = 0;
            pre_state++;
        }
    }

    /* 剩余数据进FIFO */
    if (nwords > 0 && Audio_FifoSpace() >= nwords) {
        Audio_FifoWrite(src, nwords);
    }

    /* 满足开播条件：双缓冲满 + FIFO有足够预滚数据 */
    if (pre_state == PRE_DONE && Audio_FifoLevel() >= AUDIO_PRE_ROLL_WORDS) {
        Audio_DmaStart();
    }
}

/* 外部接口：送入PCM帧 */
void Audio_FeedFrame(const uint8_t *pcm, uint16_t nbytes)
{
    uint32_t        nwords;
    const uint16_t *src;

    if (pcm == NULL || nbytes < 2) return;

    nwords = nbytes >> 1;
    src    = (const uint16_t *)pcm;

    taskENTER_CRITICAL(); /* 保护FIFO指针 */

    if (audio_paused) {
        /* 暂停时丢弃数据 */
    } else if (audio_stream_on) {
        /* 播放中：空间够则写入，不够则整帧丢弃 */
        if (Audio_FifoSpace() >= nwords) Audio_FifoWrite(src, nwords);
    } else {
        /* 启动/切换：执行预填充 */
        Audio_PreFill(src, nwords);
    }

    taskEXIT_CRITICAL();
}

/* ============================================================================
 * 6. 对外控制接口
 * ========================================================================== */

/* 暂停：软静音 + 停DMA。保留缓冲区和指针状态以便原地续播 */
void Audio_Pause(void)
{
    if (!audio_stream_on || audio_paused) return;

    WM8978_Mute(1); 

    taskENTER_CRITICAL();
    audio_paused = 1;
    Audio_DmaStop();
    taskEXIT_CRITICAL();
}

/* 继续：重启DMA，解除静音 */
void Audio_Resume(void)
{
    if (!audio_stream_on || !audio_paused) return;

    taskENTER_CRITICAL();
    Audio_DMA_ClearAllFlags();
    DMA_Cmd(AUDIO_DMA_STREAM, ENABLE);
    audio_paused = 0;
    taskEXIT_CRITICAL();

    WM8978_Mute(0);
}

/* 停止：静音 + 停DMA + 重置流水线 */
void Audio_Stop(void)
{
    WM8978_Mute(1);                      

    if (!audio_stream_on) return;

    taskENTER_CRITICAL();
    Audio_DmaStop();
    Audio_ResetPipeline();
    taskEXIT_CRITICAL();
}

/* 切换音频流：停旧流，清状态，设新参数 */
uint8_t Audio_Switch(const AudioStream *st)
{
    if (st == NULL) return 1;

    taskENTER_CRITICAL();
    if (audio_stream_on && !audio_paused) {
        Audio_DmaStop();
    }
    cur_stream       = *st;
    audio_configured = 1;
    Audio_ResetPipeline();
    taskEXIT_CRITICAL();

    /* 在非临界区清零大缓冲区，减少中断延迟 */
    memset(audio_buf,  0, sizeof(audio_buf));
    memset(audio_fifo, 0, sizeof(audio_fifo));

    WM8978_Mute(st->has_audio ? 0 : 1);
    return 0;
}

/* 重播当前流 */
void Audio_Replay(void)
{
    AudioStream tmp;
    if (!audio_configured) return;
    tmp = cur_stream;
    Audio_Switch(&tmp);
}

uint8_t Audio_IsPlaying(void)
{
    return (uint8_t)(audio_stream_on && !audio_paused);
}

uint32_t Audio_Underruns(void)
{
    return audio_underruns;
}
