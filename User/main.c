#include "stm32f4xx.h"
#include "sys.h"
#include "uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "Delay.h"
#include "SD_Card.h"
#include "ff.h"
#include "tft.h"
#include "tft_init.h"
#include "video_file.h"
#include "audio.h"
#include "keys.h"
#include <string.h>
#include <stdio.h>

/*
 * 内存分配
 *    SRAM1 128KB
 *        block_buf        2×30720 = 61440  视频块缓冲区
 *        pcm_frame        7056 一帧音频
 *        audio_buf        16384  DMA 双缓冲
 *        pre_buf          fifo预缓冲数组
 *        FATFS + FIL      
 
 *        ≈ 93KB / 128KB
 *    CCM 64KB
 *        audio_fifo       32768  软件 FIFO
 *        FreeRTOS 堆      4kb
 *        其余 RW/ZI       
 *        ≈ 43KB / 64KB
 */

#define CHUNK_MAX_BYTES 30720       // 视频块最大字节
#define BUF_NUM         2           // 视频块缓冲数组大小


#define SRAM1_BYTES     (128u * 1024u)
#define CCM_BYTES       (64u  * 1024u)
#define SRAM1_NEEDED    (BUF_NUM * CHUNK_MAX_BYTES + \
                         AUDIO_BYTES_PER_CHUNK * 2 + \
                         AUDIO_FRAME_MAX_BYTES + \
                         AUDIO_CHUNK_HALFWORDS * 2 + 2048u)
#define CCM_NEEDED      (AUDIO_FIFO_BYTES + configTOTAL_HEAP_SIZE )
#if SRAM1_NEEDED > SRAM1_BYTES
#error "SRAM1 overflow: reduce CHUNK_MAX_BYTES or BUF_NUM"
#endif
#if CCM_NEEDED > CCM_BYTES
#error "CCM overflow: reduce AUDIO_FIFO_BYTES or configTOTAL_HEAP_SIZE"
#endif

static const char *playlist[] = { "1:/1.vid", "1:/2.vid", "1:/3.vid", "1:/4.vid", "1:/5.vid" };
#define PLAYLIST_NUM    (sizeof(playlist) / sizeof(playlist[0]))

/* ======================== 全局状态 ======================== */
typedef struct {
    uint16_t          data[CHUNK_MAX_BYTES / 2];
    volatile uint32_t len;
} Block;

#define DMA_BUF_SECTION __attribute__((section(".dma_buf"), aligned(4)))

static Block   block_buf[BUF_NUM]               DMA_BUF_SECTION;
static uint8_t pcm_frame[AUDIO_FRAME_MAX_BYTES] DMA_BUF_SECTION;
FATFS          fs                               DMA_BUF_SECTION; // FIL 内含扇区窗口，必须在 SRAM1
FIL            video_fp                         DMA_BUF_SECTION;

VideoHeader     vh;
volatile uint8_t video_idx    = 0;
volatile uint8_t video_opened = 0;
volatile uint8_t video_eof    = 0;

typedef enum {
    PSTATE_PLAY = 0,
    PSTATE_PAUSE,
    PSTATE_SWITCHING,
    PSTATE_STOP
} PlayState_t;

static volatile PlayState_t g_player_state = PSTATE_SWITCHING;
static volatile uint8_t     want_index     = 0;

// 按键标志 keys.c 引用
volatile uint8_t player_next         = 0;
volatile uint8_t player_replay       = 0;
volatile uint8_t player_pause_toggle = 0;

// 无符号减法
static volatile uint32_t q_head = 0;
static volatile uint32_t q_tail = 0;
#define Q_LEVEL()   (q_head - q_tail)
#define Q_EMPTY()   (Q_LEVEL() == 0)
#define Q_FULL()    (Q_LEVEL() >= BUF_NUM)

TaskHandle_t            read_cardTCB, displayTCB;
static SemaphoreHandle_t xVideoMutex = NULL;

static struct {
    uint32_t   chunk;
    uint32_t   frame;
    uint32_t   fps_cnt;
    TickType_t wake;
    TickType_t fps_tick;
} disp;

#define VIDEO_FRAME_MS()    (1000u / (vh.fps ? vh.fps : 25u))
//#define VIDEO_FRAME_MS()    (1000u / 60u)

/* ======================== 工具函数 ======================== */
static void FatalStop(const char *msg)
{
    printf("[FATAL] %s\r\n", msg);
    for (;;) {}
}

static FRESULT MountSD(void)
{
    for (uint8_t i = 0; i < 5; i++) {
        FRESULT res = f_mount(&fs, "1:", 1);
        if (res == FR_OK) return FR_OK;
        printf("SD mount fail(%d), retry %u/5\r\n", res, i + 1);
        Delay_ms(300);
    }
    return FR_NO_FILESYSTEM;
}

// 返回: 0=OK, 1=打开失败, 2=读头失败, 3=头校验失败
static uint8_t OpenVideo(uint8_t idx)
{
    uint8_t  raw[sizeof(VideoHeader)];
    uint32_t br = 0;
    FRESULT  res;

    if (video_opened) { f_close(&video_fp); video_opened = 0; }

    res = f_open(&video_fp, playlist[idx], FA_READ);
    if (res != FR_OK) { printf("Open %s fail:%d\r\n", playlist[idx], res); return 1; }
    video_opened = 1;

    // memcpy 避免结构体填充字节导致布局错位
    if (f_read(&video_fp, raw, sizeof(raw), &br) != FR_OK || br != sizeof(raw)) {
        printf("Read header fail\r\n"); f_close(&video_fp); video_opened = 0; return 2;
    }
    memcpy(&vh, raw, sizeof(vh));

    uint8_t rc = VideoHeader_Check(&vh, CHUNK_MAX_BYTES);
    if (rc) { printf("Bad header(%d)\r\n", rc); f_close(&video_fp); video_opened = 0; return 3; }

    
    if (vh.audio_bytes != 0) {
        uint32_t need = VIDEO_HEADER_SIZE + (uint32_t)vh.total_frames * (vh.frame_bytes + vh.audio_bytes);
        if (f_size(&video_fp) < need) { printf("[WARN] No audio data, mute\r\n"); vh.audio_bytes = 0; }
    }

    video_idx = idx;
    video_eof = 0;
    printf(".vid:%ux%u %ufps %ublk/f %luB %lufrm%s\r\n",
           vh.width, vh.height, vh.fps, vh.blocks,
           (unsigned long)vh.chunk_bytes, (unsigned long)vh.total_frames,
           vh.audio_bytes ? " +audio" : "");
    return 0;
}

static void ApplyAudioStream(void)
{
    AudioStream st = {
        .audio_rate = vh.audio_rate ? vh.audio_rate : AUDIO_SAMPLE_RATE,
        .audio_ch   = vh.audio_ch   ? vh.audio_ch   : AUDIO_CHANNELS,
        .audio_bits = vh.audio_bits ? vh.audio_bits : AUDIO_BITS,
        .audio_bytes_per_frame = (uint16_t)vh.audio_bytes,
        .audio_frames = (uint16_t)vh.total_frames,
        .has_audio = (vh.audio_bytes != 0)
    };
    Audio_Switch(&st);
}//根据头文件初始化音频


/* ==================== read_card 级别3 ==================== */
void read_card(void *pvParameters)
{
    uint8_t  open_fail = 0, io_fail = 0;
    uint16_t blk_in_frame = 0;
    (void)pvParameters;

    for (;;) {
        // 1. 处理切集请求
			  if (g_player_state == PSTATE_SWITCHING) {
            if (xSemaphoreTake(xVideoMutex, portMAX_DELAY) == pdTRUE) {
                if (OpenVideo(want_index) == 0) {
                    open_fail = io_fail = blk_in_frame = 0;
                    q_head = q_tail = 0;
                    ApplyAudioStream();
                    g_player_state = PSTATE_PLAY;
                    printf("Play: %s\r\n", playlist[video_idx]);
                } else {
                    want_index = (want_index + 1) % PLAYLIST_NUM;
                    if (++open_fail >= PLAYLIST_NUM) {
                        open_fail = 0;
                        g_player_state = PSTATE_PAUSE;
                        printf("[ERR] No valid video, paused\r\n");
                    }
                }
            }
            xSemaphoreGive(xVideoMutex);
        }

        // 2.避免死机
        if (g_player_state != PSTATE_PLAY || video_eof) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 3. 队列满则等待
        if (Q_FULL()) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }

        // 4. 读视频块
        {
            uint32_t slot = q_head % BUF_NUM;
            uint32_t br = 0;
            FRESULT res = f_read(&video_fp, block_buf[slot].data, vh.chunk_bytes, &br);

            if (res != FR_OK) {
                if (++io_fail >= 3) {//尝试3次
                    printf("[ERR] Read fail:%d, EOF\r\n", res);
                    video_eof = 1; io_fail = 0;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            io_fail = 0;

            if (br == 0) { video_eof = 1; continue; }
            if (br < vh.chunk_bytes) video_eof = 1; // 尾块数据不足

            block_buf[slot].len = br;
            q_head++;
        }

        // 5. 帧尾读交错音频
        if (++blk_in_frame >= vh.blocks) {
            blk_in_frame = 0;
            if (vh.audio_bytes != 0) {
                uint32_t abr = 0;
                if (f_read(&video_fp, pcm_frame, vh.audio_bytes, &abr) == FR_OK && abr > 0)
                    Audio_FeedFrame(pcm_frame, (uint16_t)abr);
                if (abr < vh.audio_bytes) video_eof = 1;
            }
        }
    }
}

/* ==================== display (prio 2) ==================== */
static void RequestSwitch(uint8_t idx)
{
    Audio_Pause();
	  TFT_Fill(0, 0, LCD_W, LCD_H, BLACK);
    want_index = idx;
    video_eof = 0;
    g_player_state = PSTATE_SWITCHING;
    memset(&disp, 0, sizeof(disp));
    disp.wake = disp.fps_tick = xTaskGetTickCount();
}

// 返回 1 表示本轮仅处理了按键，调用方应 continue
static uint8_t HandleKeyCommands(void)
{
    if (!player_next && !player_replay && !player_pause_toggle) return 0;

    if (xSemaphoreTake(xVideoMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (player_pause_toggle) {
            player_pause_toggle = 0;
            if (g_player_state == PSTATE_PLAY) {
                Audio_Pause();
                g_player_state = PSTATE_PAUSE;
                printf("Paused\r\n");
            } else if (g_player_state == PSTATE_PAUSE) {
                Audio_Resume();
                g_player_state = PSTATE_PLAY;
                disp.wake = xTaskGetTickCount(); // 拉平基准防补帧快进
                printf("Resumed\r\n");
            }
        }
        if (player_next) {
            player_next = 0;
            RequestSwitch((video_idx + 1) % PLAYLIST_NUM);
        } else if (player_replay) {
            player_replay = 0;
            RequestSwitch(video_idx);
        }
        xSemaphoreGive(xVideoMutex);
    }
    return 1;
}

static void PrintStatus(void)
{
    if ((xTaskGetTickCount() - disp.fps_tick) < pdMS_TO_TICKS(1000)) return;
    printf("fps=%u un=%lu stk(r=%u d=%u) st=%d\r\n",
           (unsigned)disp.fps_cnt, (unsigned long)Audio_Underruns(),
           (unsigned)uxTaskGetStackHighWaterMark(read_cardTCB),
           (unsigned)uxTaskGetStackHighWaterMark(displayTCB),
           (int)g_player_state);
    disp.fps_cnt = 0;
    disp.fps_tick = xTaskGetTickCount();
}

void display(void *pvParameters)
{
    (void)pvParameters;
    disp.wake = disp.fps_tick = xTaskGetTickCount();

    for (;;) {
        if (HandleKeyCommands()) continue;

        // 播完判定: EOF + 队列空
        if (video_eof && Q_EMPTY()) {
            printf("Done: %lu frames\r\n", (unsigned long)disp.frame);
            Audio_Stop();
            video_eof = 0;
            g_player_state = PSTATE_STOP;
            continue;
        }

        if (g_player_state != PSTATE_PLAY) {
            disp.wake = xTaskGetTickCount();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (Q_EMPTY()) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }

        // 帧首对齐帧率 + 设置显示窗口
        if (disp.chunk == 0) {
            if (disp.frame > 0)
                vTaskDelayUntil(&disp.wake, pdMS_TO_TICKS(VIDEO_FRAME_MS()));

            int x0 = ((int)LCD_W - (int)vh.width) / 2;
            int y0 = ((int)LCD_H - (int)vh.height) / 2;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            TFT_Address_Set(x0, y0, x0 + vh.width - 1, y0 + vh.height - 1);
        }

        // 送显一块
        uint32_t slot = q_tail % BUF_NUM;
        //TFT_PushPixels(block_buf[slot].data, block_buf[slot].len >> 1);
				if (((uint32_t)block_buf[slot].data & 0x3) != 0) {
            printf("Unaligned! addr=0x%08X\r\n", (uint32_t)block_buf[slot].data);
           }
				LCD_DMA_Transfer(block_buf[slot].data, block_buf[slot].len >> 1);
        q_tail++;

        if (++disp.chunk >= vh.blocks) {
            disp.chunk = 0;
            disp.frame++;
            disp.fps_cnt++;
        }
        PrintStatus();
    }
}

/* ==================== 栈溢出 ==================== */
extern void debug_puts(const char *s);
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    debug_puts("\r\n***STACK OVERFLOW*** ");
    debug_puts(pcTaskName ? pcTaskName : "(null)");
    debug_puts("\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

/* ==================== main ==================== */
int main(void)
{
    NVIC_Configuration();
    USART1_Config(115200);
    UART_TxLock_Init();     // printf 互斥量，遗漏会被链接器优化掉
    Keys_Init();
    TFT_Init();
    TFT_Fill(0, 0, LCD_W, LCD_H, RED);//验证屏幕初始化
    printf("Init... FW:17\r\n");//记录程序版本
	
    printf("SD mount: %d\r\n", MountSD());
   
    // WM8978 在I2S启动后初始化 
    printf("Audio init: %s\r\n", Audio_Init(50) ? "FAIL" : "OK");
    Delay_ms(500);

    xVideoMutex = xSemaphoreCreateMutex();
	
    if (!xVideoMutex) FatalStop("Mutex create fail: heap insufficient");
    xTaskCreate(keys_task, "keys", 256, NULL, 4, NULL);
    xTaskCreate(read_card, "readcard", 192, NULL, 3, &read_cardTCB);
    xTaskCreate(display, "display", 128, NULL, 2, &displayTCB);

    vTaskStartScheduler();
    FatalStop("Scheduler returned unexpectedly");
    return 0;
}
