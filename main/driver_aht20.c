/**
 * @file driver_aht20.c
 * @brief AHT20 高精度温湿度传感器驱动实现
 *
 * AHT20 使用 I2C 接口，默认地址 0x38
 * 初始化流程：发送 0xBE 命令 → 等待校准 → 读取状态确认
 * 测量流程：发送 0xAC 0x33 0x00 触发测量 → 等待 80ms → 读取 6 字节数据
 * 数据解析：
 *   Byte[0]: 状态字 (Bit3=1 表示数据就绪)
 *   Byte[1], Byte[2], Byte[3]高4位：湿度原始值 (20-bit)
 *   Byte[3]低4位, Byte[4], Byte[5]：温度原始值 (20-bit)
 *   湿度(%) = 湿度原始值 / 2^20 * 100
 *   温度(℃) = 温度原始值 / 2^20 * 200 - 50
 */

#include "driver_aht20.h"
#include "driver/i2c.h"

#define TAG "AHT20"

/* AHT20 命令 */
#define AHT20_CMD_INIT      0xBE  /* 初始化/校准命令 */
#define AHT20_CMD_TRIGGER   0xAC  /* 触发测量命令 */
#define AHT20_CMD_RESET     0xBA  /* 软复位命令 */
#define AHT20_STATUS_BUSY   0x80  /* 忙标志位 */

/* 等待时间 */
#define AHT20_INIT_WAIT_MS  40
#define AHT20_MEAS_WAIT_MS  80

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 检查 AHT20 状态，等待就绪
 */
static esp_err_t aht20_wait_ready(void)
{
    uint8_t status;
    esp_err_t ret;
    int retry = 10;

    while (retry--) {
        ret = i2c_master_read_from_device(AHT20_I2C_NUM, AHT20_I2C_ADDR,
                                          &status, 1, pdMS_TO_TICKS(20));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "读取状态失败");
            continue;
        }
        if (!(status & AHT20_STATUS_BUSY)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "AHT20 状态超时");
    return ESP_ERR_TIMEOUT;
}

/* ==================== 公共接口 ==================== */

esp_err_t aht20_init(void)
{
    esp_err_t ret;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AHT20_I2C_SDA_IO,
        .scl_io_num = AHT20_I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = AHT20_I2C_FREQ_HZ,
        .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,
    };

    ret = i2c_param_config(AHT20_I2C_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 参数配置失败");
        return ret;
    }

    ret = i2c_driver_install(AHT20_I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 驱动安装失败");
        return ret;
    }

    /* 软复位以保证传感器处于已知状态 */
    uint8_t reset_cmd = AHT20_CMD_RESET;
    i2c_master_write_to_device(AHT20_I2C_NUM, AHT20_I2C_ADDR,
                               &reset_cmd, 1, pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 发送初始化命令 */
    uint8_t init_data[2] = {AHT20_CMD_INIT, 0x08};
    ret = i2c_master_write_to_device(AHT20_I2C_NUM, AHT20_I2C_ADDR,
                                     init_data, sizeof(init_data),
                                     pdMS_TO_TICKS(50));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "发送初始化命令失败");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_INIT_WAIT_MS));

    /* 等待校准完成 */
    ret = aht20_wait_ready();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化校准等待失败");
        return ret;
    }

    ESP_LOGI(TAG, "AHT20 初始化成功");
    return ESP_OK;
}

esp_err_t aht20_read(float *temperature, float *humidity)
{
    if (temperature == NULL || humidity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 触发测量 */
    uint8_t trigger_data[3] = {AHT20_CMD_TRIGGER, 0x33, 0x00};
    esp_err_t ret = i2c_master_write_to_device(AHT20_I2C_NUM, AHT20_I2C_ADDR,
                                               trigger_data, sizeof(trigger_data),
                                               pdMS_TO_TICKS(50));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "触发测量失败");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_MEAS_WAIT_MS));

    /* 等待数据就绪 */
    ret = aht20_wait_ready();
    if (ret != ESP_OK) {
        return ret;
    }

    /* 读取 6 字节温湿度数据 */
    uint8_t data[6] = {0};
    ret = i2c_master_read_from_device(AHT20_I2C_NUM, AHT20_I2C_ADDR,
                                      data, sizeof(data), pdMS_TO_TICKS(50));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取数据失败");
        return ret;
    }

    /* 数据解析：20-bit 原始值 */
    uint32_t raw_humi = ((uint32_t)data[1] << 12) |
                        ((uint32_t)data[2] << 4)  |
                        ((uint32_t)data[3] >> 4);

    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16) |
                        ((uint32_t)data[4] << 8) |
                        (uint32_t)data[5];

    /* 转换为物理量 */
    *humidity    = (float)raw_humi * 100.0f / 1048576.0f;   /* / 2^20 * 100 */
    *temperature = (float)raw_temp * 200.0f / 1048576.0f - 50.0f;

    return ESP_OK;
}

esp_err_t aht20_reset(void)
{
    uint8_t reset_cmd = AHT20_CMD_RESET;
    esp_err_t ret = i2c_master_write_to_device(AHT20_I2C_NUM, AHT20_I2C_ADDR,
                                               &reset_cmd, 1, pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(20));
    return ret;
}
