/**
 * @file driver_mq2.c
 * @brief MQ-2 烟雾/可燃气体传感器驱动实现
 *
 * AO 引脚 → ADC 采集，读取气体浓度模拟量
 * DO 引脚 → GPIO 数字输入
 *
 * 浓度等级 (12-bit ADC, 0-4095):
 *   0-800:    正常 (0级)
 *   800-1500: 轻度 (1级)
 *   1500-2500: 中度 (2级)
 *   2500-3500: 重度 (3级)
 *   3500+:     危险 (4级)
 */

#include "driver_mq2.h"
#include "driver/gpio.h"

#define TAG "MQ2"

esp_err_t mq2_init(void)
{
    /* 配置 MQ-2 ADC 通道（ADC1 已在 main.c 初始化）*/
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = MQ2_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_oneshot_config_channel(g_adc1_handle, MQ2_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC 通道配置失败");
        return ret;
    }

    /* GPIO 初始化（数字量输入 DO）*/
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << MQ2_DO_IO),
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "MQ-2 传感器初始化完成 (AO=GPIO%d, DO=GPIO%d)",
             MQ2_AO_IO, MQ2_DO_IO);
    return ESP_OK;
}

esp_err_t mq2_read_adc(int *adc_value)
{
    if (adc_value == NULL) return ESP_ERR_INVALID_ARG;
    return adc_oneshot_read(g_adc1_handle, MQ2_ADC_CHANNEL, adc_value);
}

esp_err_t mq2_read_digital(int *digital_val)
{
    if (digital_val == NULL) return ESP_ERR_INVALID_ARG;
    *digital_val = gpio_get_level(MQ2_DO_IO);
    return ESP_OK;
}

int mq2_adc_to_level(int adc_value)
{
    if (adc_value < 800)       return 0;
    else if (adc_value < 1500) return 1;
    else if (adc_value < 2500) return 2;
    else if (adc_value < 3500) return 3;
    else                       return 4;
}
