#include "keys.h"
#include "FreeRTOS.h"
#include "task.h"

#define KEY_SCAN_PERIOD_MS      10
#define KEY_DEBOUNCE_CNT        3

/* ★ 只引用标志位，绝不直接操作状态变量 */
extern volatile uint8_t player_next;
extern volatile uint8_t player_replay;
extern volatile uint8_t player_pause_toggle;  /* ✅ 新增 */

static uint8_t key_stable[3] = {0};
static uint8_t key_count[3]  = {0};
static uint8_t key_last[3]   = {0};

void Keys_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_AHB1PeriphClockCmd(KEY_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin  = KEY0_PIN | KEY1_PIN | KEY2_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);
}

static uint8_t key_read(uint8_t idx)
{
    switch(idx) {
        case 0:  return KEY0_DOWN() ? 1 : 0;
        case 1:  return KEY1_DOWN() ? 1 : 0;
        default: return KEY2_DOWN() ? 1 : 0;
    }
}

void keys_task(void *pvParameters)
{
    uint8_t i, raw;
    Keys_Init();

    for(i = 0; i < 3; i++) {
        key_stable[i] = key_read(i);
        key_last[i]   = key_stable[i];
    }

    while(1)
    {
        for(i = 0; i < 3; i++)
        {
            raw = key_read(i);

            if(raw == key_stable[i]) {
                key_count[i] = 0;
            } else {
                if(++key_count[i] >= KEY_DEBOUNCE_CNT)
                {
                    key_count[i]  = 0;
                    key_stable[i] = raw;

                    if(raw == 1 && key_last[i] == 0)
                    {
                        switch(i)
                        {
                            case 0:     /* ✅ KEY0：暂停 / 继续 */
                                player_pause_toggle = 1;
                                break;

                            case 1:     /* KEY1：重新播放 */
                                player_replay = 1;
                                break;

                            default:    /* KEY2：下一个视频 */
                                player_next = 1;
                                break;
                        }
                    }
                    key_last[i] = raw;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_PERIOD_MS));
    }
}
