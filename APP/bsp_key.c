#include "bsp_key.h"
//#include "gpio_config.h"

Key_TypeDef KeyList[] = {
    {GPIOA, GPIO_Pin_0, 1},  // KEY_UP：下拉输入，高有效
    {GPIOE, GPIO_Pin_2, 0},  // KEY0：上拉输入，低有效
    {GPIOE, GPIO_Pin_3, 0},  // KEY1：上拉输入，低有效
    {GPIOE, GPIO_Pin_4, 0},  // KEY2：上拉输入，低有效
};

// 读取原始电平并转换为“按下=1”
uint8_t Key_ReadRaw(uint8_t index)
{
    uint8_t level = GPIO_ReadInputDataBit(KeyList[index].port, KeyList[index].pin);
    return (level == KeyList[index].activeLevel) ? 1 : 0;
}

// 简单阻塞延时消抖（示例，实际项目建议非阻塞）
uint8_t Key_Scan(void)
{
    static uint8_t stable[KEY_NUM] = {0};
    static uint8_t cnt[KEY_NUM] = {0};
    uint8_t raw, i;

    for (i = 0; i < KEY_NUM; i++) {
        raw = Key_ReadRaw(i);
        if (raw == stable[i]) {
            cnt[i] = 0;
        } else {
            cnt[i]++;
            if (cnt[i] >= 3) {          // 连续3次（60ms）确认
                stable[i] = raw;
                if (raw == 1)           // 按下（假设高电平有效）
                    return i + 1;
            }
        }
    }
    return 0;
}
