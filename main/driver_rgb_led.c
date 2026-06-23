/**
 * @file driver_rgb_led.c
 * @brief RGB LED 模块驱动实现
 *
 * 使用 LEDC (LED PWM Controller) 实现三路 PWM 独立控制
 * 支持：预设颜色、自定义 RGB、亮度调节、自动/手动模式
 */

#include "driver_rgb_led.h"
#include "driver/ledc.h"

#define TAG "RGB_LED"

/* 当前状态 */
static rgb_mode_t s_mode = RGB_MODE_AUTO;
static uint8_t    s_brightness = 100;   /* 亮度 0-100% */
static rgb_color_t s_current_color = RGB_COLOR_OFF;

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 初始化单路 LEDC 通道
 */
static esp_err_t ledc_channel_init(gpio_num_t gpio, ledc_channel_t channel,
                                   ledc_timer_t timer)
{
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = channel,
        .timer_sel  = timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = gpio,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ledc_channel);
}

/**
 * @brief 根据亮度百分比计算 duty 值
 */
static uint32_t duty_percent(uint32_t max_duty, uint8_t percent)
{
    if (percent > 100) percent = 100;
    return (max_duty * percent) / 100;
}

/**
 * @brief 应用亮度到当前颜色
 */
static void apply_brightness(void)
{
    /* 重新设置上次的颜色（亮度会自动应用） */
    rgb_led_set_color(s_current_color);
}

/* ==================== 公共接口 ==================== */

esp_err_t rgb_led_init(void)
{
    /* 配置 LEDC 定时器 */
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = RGB_LED_RESOLUTION,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = RGB_LED_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    /* 初始化三路 PWM 通道 */
    esp_err_t ret;
    ret = ledc_channel_init(RGB_LED_R_IO, LEDC_CHANNEL_0, LEDC_TIMER_0);
    if (ret != ESP_OK) goto fail;
    ret = ledc_channel_init(RGB_LED_G_IO, LEDC_CHANNEL_1, LEDC_TIMER_0);
    if (ret != ESP_OK) goto fail;
    ret = ledc_channel_init(RGB_LED_B_IO, LEDC_CHANNEL_2, LEDC_TIMER_0);
    if (ret != ESP_OK) goto fail;

    rgb_led_off();
    ESP_LOGI(TAG, "RGB LED 初始化完成 (R=GPIO%d, G=GPIO%d, B=GPIO%d)",
             RGB_LED_R_IO, RGB_LED_G_IO, RGB_LED_B_IO);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "RGB LED 初始化失败: 0x%x", ret);
    return ret;
}

esp_err_t rgb_led_set_color(rgb_color_t color)
{
    s_current_color = color;
    uint32_t max_duty = (1 << RGB_LED_RESOLUTION) - 1;  /* 8191 for 13-bit */

    switch (color) {
    case RGB_COLOR_OFF:
        return rgb_led_set_rgb(0, 0, 0);

    case RGB_COLOR_RED:
        return rgb_led_set_rgb(max_duty, 0, 0);

    case RGB_COLOR_GREEN:
        return rgb_led_set_rgb(0, max_duty, 0);

    case RGB_COLOR_BLUE:
        return rgb_led_set_rgb(0, 0, max_duty);

    case RGB_COLOR_WHITE:
        return rgb_led_set_rgb(max_duty, max_duty, max_duty);

    case RGB_COLOR_YELLOW:
        return rgb_led_set_rgb(max_duty, max_duty, 0);

    case RGB_COLOR_CYAN:
        return rgb_led_set_rgb(0, max_duty, max_duty);

    case RGB_COLOR_MAGENTA:
        return rgb_led_set_rgb(max_duty, 0, max_duty);

    case RGB_COLOR_WARM:
        /* 暖白光：偏红橙 */
        return rgb_led_set_rgb(max_duty,
                               duty_percent(max_duty, 60),
                               duty_percent(max_duty, 10));

    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t rgb_led_set_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t max_duty = (1 << RGB_LED_RESOLUTION) - 1;

    /* 钳位到有效范围 */
    if (r > max_duty) r = max_duty;
    if (g > max_duty) g = max_duty;
    if (b > max_duty) b = max_duty;

    /* 应用亮度百分比 */
    r = duty_percent(r, s_brightness);
    g = duty_percent(g, s_brightness);
    b = duty_percent(b, s_brightness);

#if RGB_LED_COMMON_ANODE
    /* 共阳极：反相，max = 灭, 0 = 最亮 */
    r = max_duty - r;
    g = max_duty - g;
    b = max_duty - b;
#endif

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, r));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, g));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, b));

    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2));

    return ESP_OK;
}

esp_err_t rgb_led_off(void)
{
    s_current_color = RGB_COLOR_OFF;
    return rgb_led_set_rgb(0, 0, 0);
}

esp_err_t rgb_led_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_brightness = percent;
    apply_brightness();
    ESP_LOGI(TAG, "亮度设置为 %d%%", percent);
    return ESP_OK;
}

rgb_mode_t rgb_led_get_mode(void)
{
    return s_mode;
}

void rgb_led_set_mode(rgb_mode_t mode)
{
    s_mode = mode;
    ESP_LOGI(TAG, "LED 模式切换为: %s", mode == RGB_MODE_AUTO ? "自动" : "手动");
}

esp_err_t rgb_led_auto_control(int light_adc)
{
    if (s_mode != RGB_MODE_AUTO) {
        return ESP_OK;  /* 非自动模式，不处理 */
    }

    /* 跳过无效的光敏值 */
    if (light_adc < 0) {
        return ESP_OK;
    }

    static int auto_on = 0;
    static TickType_t last_trigger = 0;
    TickType_t now = xTaskGetTickCount();

    if (!auto_on && light_adc > AUTO_LIGHT_THRESHOLD) {
        /* 光线暗（ADC 高） → 自动开灯（暖白，适合起夜） */
        rgb_led_set_color(RGB_COLOR_WARM);
        last_trigger = now;
        auto_on = 1;
        ESP_LOGI(TAG, "自动模式：检测到光线暗 (ADC=%d)，开启照明", light_adc);
    } else if (auto_on && light_adc <= AUTO_LIGHT_THRESHOLD) {
        /* 光线恢复（ADC 低） → 延时关灯 */
        if ((now - last_trigger) > pdMS_TO_TICKS(LIGHT_DELAY_OFF_MS)) {
            rgb_led_off();
            auto_on = 0;
            ESP_LOGI(TAG, "自动模式：光线恢复 (ADC=%d)，延时关闭照明", light_adc);
        }
    }

    return ESP_OK;
}
