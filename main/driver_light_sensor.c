/**
 * @file driver_light_sensor.c
 * @brief 光敏传感器驱动实现
 *
 * AO 引脚 → ADC 采集，读取环境光强度的模拟量
 * DO 引脚 → GPIO 数字输入
 */

#include "driver_light_sensor.h"
#include "driver/gpio.h"

#define TAG "LIGHT"

esp_err_t light_sensor_init(void)
{
    /* 配置光敏 ADC 通道（ADC1 已在 main.c 初始化）*/
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = LIGHT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_oneshot_config_channel(g_adc1_handle, LIGHT_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败");
        return ret;
    }

    /* GPIO 初始化（数字量输入 DO）*/
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << LIGHT_SENSOR_DO_IO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "光敏传感器初始化完成 (AO=GPIO%d, DO=GPIO%d)",
             LIGHT_SENSOR_AO_IO, LIGHT_SENSOR_DO_IO);
    return ESP_OK;
}

esp_err_t light_sensor_read_adc(int *adc_value)
{
    if (adc_value == NULL) return ESP_ERR_INVALID_ARG;
    return adc_oneshot_read(g_adc1_handle, LIGHT_ADC_CHANNEL, adc_value);
}

esp_err_t light_sensor_read_digital(int *digital_val)
{
    if (digital_val == NULL) return ESP_ERR_INVALID_ARG;
    *digital_val = gpio_get_level(LIGHT_SENSOR_DO_IO);
    return ESP_OK;
}
