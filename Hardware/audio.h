#ifndef __AUDIO_H
#define __AUDIO_H

#include "stm32f4xx.h"


#define AUDIO_SAMPLE_RATE       44100
#define AUDIO_BITS              16
#define AUDIO_CHANNELS          2       /* 立体声 */
//#define AUDIO_DEFAULT_VOLUME    0x19    /* 耳机音量 0~63，数值越大越轻 */

/* ---- DMA 双缓冲 ------------------------------------------------------。 */
#define AUDIO_CHUNK_SAMPLES     2048
#define AUDIO_CHUNK_HALFWORDS   (AUDIO_CHUNK_SAMPLES * AUDIO_CHANNELS)//双声道*2
#define AUDIO_BYTES_PER_CHUNK   (AUDIO_CHUNK_HALFWORDS * 2)

/* ---- 软件 FIFO -----------------------------------------------------------
 * 32KB = 16384 个 int16 ≈ 186ms 
 * 必须是 2 的幂（环形寻址用 & (N-1)）。 */
 
 #define AUDIO_FIFO_BYTES        32768    //16384 有一点杂音缓冲不足
#define AUDIO_FIFO_WORDS        (AUDIO_FIFO_BYTES / 2)

/* ---- 预滚阈值 ------------------------------------------------------------
 * 开 DMA 之前先把 A/B 两个半缓冲填满真数据，FIFO 再攒够这么多字才使能 DMA。
 * 否则开播头 46ms 是 memset 的静音，听起来"嘴型晚半拍"。 */
#define AUDIO_PRE_ROLL_WORDS    AUDIO_CHUNK_HALFWORDS

 
#define AUDIO_FRAME_MAX_BYTES  7056  //一帧字节

/* ---- DMA1_Stream4 / Channel0 = SPI2/I2S2_TX ---- */
#define AUDIO_DMA_STREAM        DMA1_Stream4
#define AUDIO_DMA_CHANNEL       DMA_Channel_0
#define AUDIO_DMA_IRQn          DMA1_Stream4_IRQn
#define AUDIO_DMA_TCIF          DMA_FLAG_TCIF4
#define AUDIO_DMA_HTIF          DMA_FLAG_HTIF4
#define AUDIO_DMA_TEIF          DMA_FLAG_TEIF4
#define AUDIO_DMA_DMEIF         DMA_FLAG_DMEIF4
#define AUDIO_DMA_FEIF          DMA_FLAG_FEIF4

/* ---------------------------------------------------------------------------
 *  音频流描述：read_card 打开 .vid 后按文件头填好，交给 Audio_Switch()
 * -------------------------------------------------------------------------- */
typedef struct {
    uint32_t audio_rate;            /* 采样率（44100）                        */
    uint8_t  audio_ch;              /* 声道数（2 = 立体声）                   */
    uint8_t  audio_bits;            /* 位深（16）                             */
    uint16_t audio_bytes_per_frame; /* 每帧音频字节数（交错格式），0 = 无音频 */
    uint16_t audio_frames;          /* 音频帧数（= 视频总帧数）               */
    uint8_t  has_audio;             /* 1 = 有音频，0 = 纯视频                 */
} AudioStream;

/* ---------------------------------------------------------------------------
 *  接口
 * -------------------------------------------------------------------------- */
uint8_t  Audio_Init(uint8_t n);          /* 0 = 成功。main() 里、开调度器之前调用  */

void     Audio_Pause(void);         /* 暂停：软静音 + 停 DMA，缓冲/指针全不动 */
void     Audio_Resume(void);        /* 继续：从暂停位置原地续播（一个采样不丢）*/
void     Audio_Stop(void);          /* 停止：静音 + 停 DMA + 流水线回到初始态 */
uint8_t  Audio_Switch(const AudioStream *st);  /* 切到新流：0 = 成功        */

uint8_t  Audio_IsPlaying(void);     /* 1 = DMA 正在播放            */
uint32_t Audio_Underruns(void);     /* FIFO不足  fps打印的un       */

/* read_card 任务帧尾调用：把一帧 PCM 写进FIFO */
void     Audio_FeedFrame(const uint8_t *pcm, uint16_t nbytes);

/* DMA1_Stream4 中断服务（User/stm32f4xx_it.c 里转发过来） */
void     Audio_DMA_IRQHandler(void);

#endif /* __AUDIO_H */
