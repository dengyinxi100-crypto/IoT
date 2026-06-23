/**
 * @file app_config.h
 * @brief IoT Gateway - 全局配置 (学号: 0207)
 *
 * 引脚说明（ESP32-S3）：
 *   AHT20  I2C:     SDA=GPIO41, SCL=GPIO42
 *   RGB LED PWM:    R=GPIO40,  G=GPIO39,  B=GPIO38
 *   光敏传感器:     AO=GPIO1(ADC1_CH0), DO=GPIO2
 *   MQ-2 气体传感器: AO=GPIO3(ADC1_CH2), DO=GPIO4
 *   蜂鸣器:         GPIO21
 *   呼救按钮:       GPIO0(BOOT键, strapping引脚)
 *   毫米波雷达:     IO=GPIO5, RX=GPIO6, TX=GPIO7(UART2)
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== WiFi 配置 (STA 模式) ==================== */
#define WIFI_SSID               "Xiaomi 15"
#define WIFI_PASSWORD           "user@123456"
#define WIFI_MAX_RETRY           10
#define WIFI_RETRY_INTERVAL_MS   5000

/* ==================== GPIO 引脚定义 ==================== */
/* AHT20 I2C */
#define AHT20_I2C_NUM            I2C_NUM_0
#define AHT20_I2C_SDA_IO         41
#define AHT20_I2C_SCL_IO         42
#define AHT20_I2C_FREQ_HZ        100000
#define AHT20_I2C_ADDR           0x38

/* RGB LED (PWM) — 共阳极模块，R/B 交叉 */
#define RGB_LED_R_IO             40
#define RGB_LED_G_IO             39
#define RGB_LED_B_IO             38
#define RGB_LED_FREQ_HZ          5000
#define RGB_LED_RESOLUTION       LEDC_TIMER_13_BIT
#define RGB_LED_COMMON_ANODE     1

/* 光敏传感器 */
#define LIGHT_SENSOR_AO_IO       1       /* ADC1_CH0 */
#define LIGHT_SENSOR_DO_IO       2
#define LIGHT_ADC_UNIT           ADC_UNIT_1
#define LIGHT_ADC_CHANNEL        ADC_CHANNEL_0
#define LIGHT_ADC_ATTEN          ADC_ATTEN_DB_12

/* MQ-2 烟雾/可燃气体传感器 */
#define MQ2_AO_IO                3       /* ADC1_CH2 */
#define MQ2_DO_IO                4
#define MQ2_ADC_UNIT             ADC_UNIT_1
#define MQ2_ADC_CHANNEL          ADC_CHANNEL_2
#define MQ2_ADC_ATTEN            ADC_ATTEN_DB_12

/* 蜂鸣器 */
#define BUZZER_IO                21

/* 呼救按钮 (BOOT键=GPIO0, strapping引脚, 不重新配置) */
#define SOS_BUTTON_IO            0

/* 毫米波雷达 (24GHz, 人体存在/移动检测) */
#define RADAR_IO_IO              5       /* 存在检测输出 (1=有人, 0=无人) */
#define RADAR_UART_NUM           UART_NUM_2
#define RADAR_UART_RX_IO         6       /* ESP32-S3 接收雷达数据 */
#define RADAR_UART_TX_IO         7       /* ESP32-S3 向雷达发送指令 */
#define RADAR_UART_BAUD          115200

/* ==================== 传感器采集周期 ==================== */
#define SENSOR_COLLECT_PERIOD_MS   200
#define WS_BROADCAST_PERIOD_MS     200

/* ==================== 智能照明 & 告警阈值 ==================== */
#define AUTO_LIGHT_THRESHOLD      2000  /* 光敏 ADC > 此值 = 暗，开白光 */
#define LIGHT_DELAY_OFF_MS        3000  /* 延时关灯 3s */

/* MQ-2 气体浓度等级 (12-bit ADC, 0-4095) */
#define GAS_LEVEL_MILD            800
#define GAS_LEVEL_MEDIUM          1500
#define GAS_LEVEL_SEVERE          2500

/* ==================== 全局变量声明 ==================== */
extern EventGroupHandle_t g_evt_group;
extern adc_oneshot_unit_handle_t g_adc1_handle;

/* ==================== 传感器数据结构 ==================== */
typedef struct {
    float temperature;
    float humidity;
    int   light_adc;
    int   light_digital;
    int   gas_adc;
    int   gas_digital;
    int   gas_level;
    int   radar_presence;     /* 毫米波雷达: 1=有人, 0=无人 */
} sensor_data_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */