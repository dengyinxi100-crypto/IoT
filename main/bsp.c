/**
 * @file bsp.c
 * @brief 板级支持包实现：蜂鸣器报警 + 呼救按钮
 *
 * 蜂鸣器：有源蜂鸣器，低电平触发 (I/O=Low 响, High 停)
 * 呼救按钮：常开按钮，按下为低电平 (上拉输入)
 */

#include "bsp.h"
#include "driver/gpio.h"

#define TAG "BSP"

/* 蜂鸣器报警定时器句柄 */
static TimerHandle_t s_buzzer_timer = NULL;

/* 蜂鸣器定时器回调 */
static void buzzer_timer_cb(TimerHandle_t timer)
{
    gpio_set_level(BUZZER_IO, 1);
    ESP_LOGI(TAG, "蜂鸣器报警结束");
}

esp_err_t bsp_init(void)
{
    /* ---- 蜂鸣器 GPIO ---- */
    gpio_config_t buzzer_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BUZZER_IO),
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&buzzer_conf);
    gpio_set_level(BUZZER_IO, 1);  /* 初始高电平，蜂鸣器静音 */

    /* ---- 呼救按钮 GPIO (strapping引脚, 不重新配置) ---- */
    /* GPIO0 已由 ROM bootloader 配置为上拉输入，直接读取即可 */

    /* 创建蜂鸣器定时器 */
    s_buzzer_timer = xTimerCreate("buzzer_timer",
                                  pdMS_TO_TICKS(100),
                                  pdFALSE,     /* one-shot */
                                  NULL,
                                  buzzer_timer_cb);

    ESP_LOGI(TAG, "BSP 初始化完成 (蜂鸣器=GPIO%d, 呼救按钮=GPIO%d, strapping引脚)",
             BUZZER_IO, SOS_BUTTON_IO);
    return ESP_OK;
}

esp_err_t buzzer_alarm(uint32_t duration_ms)
{
    if (duration_ms == 0) {
        /* 立即停止：输出高电平 */
        gpio_set_level(BUZZER_IO, 1);
        if (s_buzzer_timer) xTimerStop(s_buzzer_timer, 0);
        return ESP_OK;
    }

    gpio_set_level(BUZZER_IO, 0);  /* 低电平触发报警 */
    if (s_buzzer_timer) {
        xTimerChangePeriod(s_buzzer_timer,
                          pdMS_TO_TICKS(duration_ms), 0);
        xTimerStart(s_buzzer_timer, 0);
    }
    return ESP_OK;
}

int sos_button_pressed(void)
{
    /* 上拉输入，按下为低电平 */
    return (gpio_get_level(SOS_BUTTON_IO) == 0);
}
