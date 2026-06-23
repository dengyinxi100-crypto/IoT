/**
 * @file driver_rgb_led.h
 * @brief RGB LED 模块驱动 (三路 PWM 控制)
 *
 * 模块引脚: V(3.3V/5V), R(红灯), G(绿灯), B(蓝灯)
 * 通过 PWM 实现颜色控制、调光、闪烁等效果
 */

#ifndef DRIVER_RGB_LED_H
#define DRIVER_RGB_LED_H

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED 预设颜色 */
typedef enum {
    RGB_COLOR_OFF     = 0,   /* 全灭 */
    RGB_COLOR_RED     = 1,   /* 红色 */
    RGB_COLOR_GREEN   = 2,   /* 绿色 */
    RGB_COLOR_BLUE    = 3,   /* 蓝色 */
    RGB_COLOR_WHITE   = 4,   /* 白色 */
    RGB_COLOR_YELLOW  = 5,   /* 黄色 */
    RGB_COLOR_CYAN    = 6,   /* 青色 */
    RGB_COLOR_MAGENTA = 7,   /* 品红 */
    RGB_COLOR_WARM    = 8,   /* 暖白 (适合夜间) */
} rgb_color_t;

/* RGB LED 工作模式 */
typedef enum {
    RGB_MODE_MANUAL = 0,   /* 手动模式：由 MQTT 命令控制 */
    RGB_MODE_AUTO   = 1,   /* 自动模式：根据光敏传感器自动调光 */
} rgb_mode_t;

/**
 * @brief 初始化 RGB LED PWM 通道
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_init(void);

/**
 * @brief 设置 LED 亮度百分比
 * @param[in] percent 0-100 (%)
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_set_brightness(uint8_t percent);

/**
 * @brief 设置 LED 预设颜色
 * @param[in] color 预设颜色枚举值
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_set_color(rgb_color_t color);

/**
 * @brief 设置 LED 自定义颜色 (RGB 分量 0-8191)
 * @param[in] r 红色分量
 * @param[in] g 绿色分量
 * @param[in] b 蓝色分量
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_set_rgb(uint32_t r, uint32_t g, uint32_t b);

/**
 * @brief 关闭 LED（全灭）
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_off(void);

/**
 * @brief 获取当前 LED 模式
 * @return 当前模式
 */
rgb_mode_t rgb_led_get_mode(void);

/**
 * @brief 设置 LED 工作模式
 * @param[in] mode 工作模式
 */
void rgb_led_set_mode(rgb_mode_t mode);

/**
 * @brief 自适应调光：根据环境光传感器值自动决定开灯/关灯
 * @param[in] light_adc 光敏传感器 ADC 值
 * @return ESP_OK 成功
 */
esp_err_t rgb_led_auto_control(int light_adc);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_RGB_LED_H */
