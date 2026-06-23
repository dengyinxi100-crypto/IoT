/**
 * @file driver_mq2.h
 * @brief MQ-2 烟雾/可燃气体传感器驱动
 *
 * MQ-2 可检测液化气、丙烷、氢气、烟雾等
 * 引脚: VCC(5V), GND, AO(模拟量输出), DO(数字阈值输出)
 * 工作原理: 传感器电阻随气体浓度变化 → 输出电压变化 → ADC 采集
 */

#ifndef DRIVER_MQ2_H
#define DRIVER_MQ2_H

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 MQ-2 传感器 (ADC + GPIO)
 * @return ESP_OK 成功
 */
esp_err_t mq2_init(void);

/**
 * @brief 读取 MQ-2 模拟量 (气体浓度原始值)
 * @param[out] adc_value ADC 原始值 (0-4095)
 * @return ESP_OK 成功
 */
esp_err_t mq2_read_adc(int *adc_value);

/**
 * @brief 读取 MQ-2 数字阈值输出
 * @param[out] digital_val 0=低于阈值(正常), 1=高于阈值(报警)
 * @return ESP_OK 成功
 */
esp_err_t mq2_read_digital(int *digital_val);

/**
 * @brief 将 ADC 值转换为气体浓度等级 (0-4)
 * @param[in] adc_value ADC 原始值
 * @return 等级: 0=正常, 1=轻度, 2=中度, 3=重度, 4=危险
 */
int mq2_adc_to_level(int adc_value);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_MQ2_H */
