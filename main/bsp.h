/**
 * @file bsp.h
 * @brief 板级支持包 (BSP) - 蜂鸣器、按键
 */

#ifndef BSP_H
#define BSP_H

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化板级外设 (蜂鸣器、按钮)
 * @return ESP_OK 成功
 */
esp_err_t bsp_init(void);

/**
 * @brief 蜂鸣器报警（非阻塞，指定持续时间）
 * @param[in] duration_ms 蜂鸣持续时间 (ms), 0=停止
 * @return ESP_OK 成功
 */
esp_err_t buzzer_alarm(uint32_t duration_ms);

/**
 * @brief 读取呼救按钮状态
 * @return 1=按下, 0=未按下
 */
int sos_button_pressed(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
