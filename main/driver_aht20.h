/**
 * @file driver_aht20.h
 * @brief AHT20 高精度温湿度传感器驱动 (I2C)
 */

#ifndef DRIVER_AHT20_H
#define DRIVER_AHT20_H

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 AHT20 传感器
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t aht20_init(void);

/**
 * @brief 读取温湿度数据
 * @param[out] temperature 温度值 (℃)
 * @param[out] humidity    湿度值 (%)
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t aht20_read(float *temperature, float *humidity);

/**
 * @brief 软复位 AHT20
 * @return ESP_OK 成功
 */
esp_err_t aht20_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_AHT20_H */
