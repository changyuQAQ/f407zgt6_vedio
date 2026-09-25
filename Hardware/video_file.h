#ifndef __VIDEO_FILE_H
#define __VIDEO_FILE_H

#include "stm32f4xx.h"

/* ============================================================================
 *  .vid 文件格式（PC 端「转码_3.py」生成，MCU 端按头播放）
 *
 *  布局（★ 音视频帧【交错】存储）：
 *      [ 40 字节文件头 ]
 *      [ 视频帧 1 ][ 音频帧 1 ]
 *      [ 视频帧 2 ][ 音频帧 2 ]
 *      ...
 *      [ 视频帧 N ][ 音频帧 N ]
 *
 *  设计目标：一份固件能播任意尺寸的视频。
 *  分辨率、每帧块数、每块字节数、采样率全部由文件头携带，
 *  固件里只留一个"块缓冲上限" CHUNK_MAX_BYTES。
 *
 *  ★ 所有多字节字段都是【小端】，PC 端用 struct.pack('<...') 打包，
 *    STM32 也是小端，所以按字节读进来 memcpy 到结构体即可。
 *
 *  ★ 音频字段语义（v3 交错格式）：
 *    audio_offset 已废弃，固定为 0（音频紧跟每帧视频数据之后）；
 *    audio_bytes  = 【每帧音频字节数】= audio_rate*audio_ch*2 / fps，
 *                   不再表示音频总字节数；0 = 该文件无音频。
 * ========================================================================== */

#define VIDEO_MAGIC        0x31504D56UL     /* "VMP1" 的小端写法 */

/* 文件头 40 字节，字段都自然对齐，不需要 pack */
typedef struct {
    uint32_t magic;         /*  0  VIDEO_MAGIC                          */
    uint16_t width;         /*  4  视频宽（像素）                        */
    uint16_t height;        /*  6  视频高                                */
    uint16_t fps;           /*  8  目标播放帧率（音视频同步用）           */
    uint16_t blocks;        /* 10  每帧拆成几块                          */
    uint32_t chunk_bytes;   /* 12  每块字节数 = frame_bytes / blocks      */
    uint32_t frame_bytes;   /* 16  每帧字节数 = width * height * 2        */
    uint32_t total_frames;  /* 20  总帧数                                */
    uint32_t audio_offset;  /* 24  废弃（v3 交错格式恒为 0）              */
    uint32_t audio_bytes;   /* 28  ★每帧音频字节数（交错格式），0 = 无音频 */
    uint32_t audio_rate;    /* 32  音频采样率（如 44100）                 */
    uint8_t  audio_ch;      /* 36  声道数（如 2）                         */
    uint8_t  audio_bits;    /* 37  位深（如 16）                          */
    uint16_t reserved;      /* 38  保留，PC 端填 0                        */
} VideoHeader;

#define VIDEO_HEADER_SIZE   ((uint32_t)sizeof(VideoHeader))   /* 应为 40 */

/* 音频帧缓冲上限（字节）。read_card 的 pcm_frame 就开这么大，
 * 文件头里的每帧音频字节数超过它会被拒播。25fps@44.1k/立体声 = 7056B */
#define VIDEO_MAX_AUDIO_FRAME_BYTES  8192

/* 校验一个头是否合法。返回 0 = 可以播；非 0 = 拒绝，并给出原因 */
static __inline uint8_t VideoHeader_Check(const VideoHeader *h, uint32_t chunk_max)
{
    if(h->magic != VIDEO_MAGIC)                     return 1;   /* 不是 .vid */
    if(h->width == 0 || h->height == 0)             return 2;
    if(h->fps == 0 || h->blocks == 0)               return 3;
    if(h->chunk_bytes == 0 ||
       h->chunk_bytes > chunk_max)                  return 4;   /* 块太大，缓冲装不下 */
    if(h->chunk_bytes % 512 != 0)                   return 5;   /* 不是 512 整数倍，FatFs 会走慢路径 */
    if(h->frame_bytes != (uint32_t)h->width * h->height * 2) return 6;
    if(h->frame_bytes != h->chunk_bytes * h->blocks)         return 7;  /* 不能整除 */
    if(h->audio_bytes != 0 &&
       (h->audio_bytes > VIDEO_MAX_AUDIO_FRAME_BYTES ||
        h->audio_bytes % 4 != 0))                   return 8;   /* 每帧音频字节数越界或未 4 字节对齐 */
    return 0;
}

#endif /* __VIDEO_FILE_H */
