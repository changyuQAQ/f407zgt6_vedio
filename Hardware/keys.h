#ifndef __KEYS_H
#define __KEYS_H

#include "stm32f4xx.h"

/* ============================================================================
 *  三个按键 —— 暂停 / 重新播放 / 打开下一个视频
 *
 *  引脚（沿用配套代码的接法，全部非阻塞扫描）：
 *      KEY0 = PE4  上拉输入，低有效  → 暂停 / 继续
 *      KEY1 = PE3  上拉输入，低有效  → 重新播放（当前视频从头开始）
 *      KEY2 = PE2  上拉输入，低有效  → 打开下一个视频
 *
 *  ★ 引脚冲突已核对：PE2/PE3/PE4 在 STM32F407 上也是 FSMC_A23/A19/A20，
 *    但本工程的 LCD 用的是 FSMC NE4 + A6(PF12)，数据线只占 PE7~PE15，
 *    PE2/PE3/PE4 完全没被 lcd_ili9488.c 碰过，所以可以安全当普通 IO 用。
 * ========================================================================== */

#define KEY_PORT            GPIOE
#define KEY_GPIO_CLK        RCC_AHB1Periph_GPIOE

#define KEY0_PIN            GPIO_Pin_4      /* 暂停 / 继续     */
#define KEY1_PIN            GPIO_Pin_3      /* 重新播放        */
#define KEY2_PIN            GPIO_Pin_2      /* 下一个视频      */

/* 上拉输入 → 按下时读到 0 */
#define KEY0_DOWN()         (GPIO_ReadInputDataBit(KEY_PORT, KEY0_PIN) == Bit_RESET)
#define KEY1_DOWN()         (GPIO_ReadInputDataBit(KEY_PORT, KEY1_PIN) == Bit_RESET)
#define KEY2_DOWN()         (GPIO_ReadInputDataBit(KEY_PORT, KEY2_PIN) == Bit_RESET)

/* ============================================================================
 *  播放命令。按键任务只负责置位，谁真正干活由业务决定：
 *    暂停/继续  → display 和 audio 任务各自看 player_paused
 *    重新播放   → read_card 任务回到文件头，display 帧号归零
 *    下一个视频 → 三个任务一起切换到下一个文件
 *  用独立的标志位而不是队列：这三个动作都是"一次性的、幂等的"，
 *  丢一次也不会出问题，反而比队列简单。
 * ========================================================================== */
typedef enum {
    PLAYER_STOP = 0,
    PLAYER_PLAY,
    PLAYER_PAUSE
} PlayerState;

extern volatile PlayerState player_state;    /* 暂停 / 播放 */
extern volatile uint8_t     player_replay;   /* 1 = 请从头重新播放 */
extern volatile uint8_t     player_next;     /* 1 = 请切到下一个视频 */

void Keys_Init(void);
void keys_task(void *pvParameters);          /* FreeRTOS 任务入口 */

#endif /* __KEYS_H */
