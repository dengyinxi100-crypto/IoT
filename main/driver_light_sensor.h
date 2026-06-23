/**
 * @file driver_light_sensor.h
 * @brief 光敏传感器驱动 (ADC + 数字量)
 */

#ifndef DRIVER_LIGHT_SENSOR_H
#define DRIVER_LIGHT_SENSOR_H

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化光敏传感器 ADC
 * @return ESP_OK 成功
 */
esp_err_t light_sensor_init(void);

/**
 * @brief 读取光敏模拟值
 * @param[out] adc_value ADC 原始值 (0-4095)
 * @return ESP_OK 成功
 */
esp_err_t light_sensor_read_adc(int *adc_value);

/**
 * @brief 读取光敏数字输出
 * @param[out] digital_val 数字值 (0=亮, 1=暗)
 * @return ESP_OK 成功
 */
esp_err_t light_sensor_read_digital(int *digital_val);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_LIGHT_SENSOR_H */
