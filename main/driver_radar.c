/**
 * @file driver_radar.c
 * @brief 毫米波雷达驱动实现 (24GHz, LD2410 兼容协议)
 *
 * IO引脚(GPIO5): 低电平=有人, 高电平=无人 (上拉输入, 雷达开漏拉低有效)
 * UART2: 雷达数据通信与参数配置 (默认115200bps, 8N1)
 */

#include "driver_radar.h"
#include "driver/gpio.h"
#include <string.h>

#define TAG "RADAR"

/* ---- LD2410 协议常量 ---- */
#define RADAR_FRAME_HEADER_0     0xFD
#define RADAR_FRAME_HEADER_1     0xFC
#define RADAR_FRAME_HEADER_2     0xFB
#define RADAR_FRAME_HEADER_3     0xFA
#define RADAR_FRAME_FOOTER_0     0x04
#define RADAR_FRAME_FOOTER_1     0x03
#define RADAR_FRAME_FOOTER_2     0x02
#define RADAR_FRAME_FOOTER_3     0x01

/* 数据帧内目标状态偏移量 */
#define RADAR_TARGET_STATE_OFFSET  8   /* 0=无人, 1=运动, 2=静止, 3=运动+静止 */

/* 上次从UART解析到的有人状态 (全局缓存) */
static int g_radar_uart_presence = 0;
static uint32_t g_radar_uart_last_ms = 0;

esp_err_t radar_init(void)
{
    /* ---- 存在检测 IO 引脚 (上拉输入, 雷达开漏拉低=有人) ---- */
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << RADAR_IO_IO),
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* ---- UART2 初始化 ---- */
    uart_config_t uart_conf = {
        .baud_rate  = RADAR_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(RADAR_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(RADAR_UART_NUM, &uart_conf));
    ESP_ERROR_CHECK(uart_set_pin(RADAR_UART_NUM, RADAR_UART_TX_IO,
                                 RADAR_UART_RX_IO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "毫米波雷达初始化完成 (IO=GPIO%d, UART2 RX=GPIO%d TX=GPIO%d)",
             RADAR_IO_IO, RADAR_UART_RX_IO, RADAR_UART_TX_IO);
    return ESP_OK;
}

/* ---- 发送配置命令唤醒/复位雷达 (LD2410 协议) ---- */
esp_err_t radar_send_config(void)
{
    /* 先清空UART缓冲区中的残留数据 */
    uint8_t dummy[64];
    while (uart_read_bytes(RADAR_UART_NUM, dummy, sizeof(dummy), 0) > 0) {}

    /*
     * 发送"进入配置模式"命令:
     * FD FC FB FA 04 00 FF 00 01 00 04 03 02 01
     */
    const uint8_t enter_cfg[] = {
        0xFD, 0xFC, 0xFB, 0xFA,   /* 帧头 */
        0x04, 0x00,               /* 数据长度 (小端) */
        0xFF, 0x00,               /* 命令: 进入配置 */
        0x01, 0x00,               /* 值 */
        0x04, 0x03, 0x02, 0x01    /* 帧尾 */
    };
    int ret = uart_write_bytes(RADAR_UART_NUM, enter_cfg, sizeof(enter_cfg));
    if (ret < 0) {
        ESP_LOGW(TAG, "雷达配置命令发送失败");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    /*
     * 发送"退出配置模式"命令:
     * FD FC FB FA 02 00 FE 00 04 03 02 01
     * 这会让雷达回到正常上报模式, 确保IO引脚正常工作
     */
    const uint8_t exit_cfg[] = {
        0xFD, 0xFC, 0xFB, 0xFA,
        0x02, 0x00,
        0xFE, 0x00,
        0x04, 0x03, 0x02, 0x01
    };
    ret = uart_write_bytes(RADAR_UART_NUM, exit_cfg, sizeof(exit_cfg));
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 再次清空缓冲区 */
    while (uart_read_bytes(RADAR_UART_NUM, dummy, sizeof(dummy), 0) > 0) {}

    if (ret >= 0) {
        ESP_LOGI(TAG, "雷达配置命令已发送 (进入+退出配置模式)");
    }
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

int radar_presence_read(void)
{
    /* IO 有效电平: 低电平=有人 (上拉输入, 雷达开漏拉低有效) */
    return (gpio_get_level(RADAR_IO_IO) == 0) ? 1 : 0;
}

/* ---- 从UART读取并解析雷达数据帧 (LD2410 协议) ---- */
int radar_read_uart_data(void)
{
    uint8_t buf[128];
    int len = uart_read_bytes(RADAR_UART_NUM, buf, sizeof(buf), 0);
    if (len <= 0) return -1;  /* 无数据 */

    /* 在缓冲区中搜索 LD2410 数据帧 */
    for (int i = 0; i + 13 <= len; i++) {
        /* 匹配帧头 FD FC FB FA */
        if (buf[i]   != RADAR_FRAME_HEADER_0) continue;
        if (buf[i+1] != RADAR_FRAME_HEADER_1) continue;
        if (buf[i+2] != RADAR_FRAME_HEADER_2) continue;
        if (buf[i+3] != RADAR_FRAME_HEADER_3) continue;

        int data_len = buf[i+4] | (buf[i+5] << 8);
        int frame_end = i + 6 + data_len;

        /* 检查帧尾是否在缓冲区内 */
        if (frame_end + 4 > len) break;

        /* 验证帧尾 04 03 02 01 */
        if (buf[frame_end]   != RADAR_FRAME_FOOTER_0) continue;
        if (buf[frame_end+1] != RADAR_FRAME_FOOTER_1) continue;
        if (buf[frame_end+2] != RADAR_FRAME_FOOTER_2) continue;
        if (buf[frame_end+3] != RADAR_FRAME_FOOTER_3) continue;

        /* 检查是否是上报数据帧 (类型 0x01=工程模式, 0x02=标准模式) */
        uint8_t report_type = buf[i+6];
        if (report_type != 0x01 && report_type != 0x02) continue;

        /* 读取目标状态: 0=无人, 1=运动, 2=静止, 3=运动+静止 */
        if (i + RADAR_TARGET_STATE_OFFSET < frame_end) {
            uint8_t target_state = buf[i + RADAR_TARGET_STATE_OFFSET];
            int prev = g_radar_uart_presence;
            g_radar_uart_presence = (target_state != 0x00) ? 1 : 0;
            g_radar_uart_last_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (g_radar_uart_presence != prev) {
                ESP_LOGI(TAG, "UART检测: 目标状态=%d → %s",
                         target_state, g_radar_uart_presence ? "有人" : "无人");
            }
        }
        return len;  /* 成功解析到帧 */
    }

    /* 收到数据但未找到有效帧 (波特率可能不匹配或非LD2410协议) */
    static int noise_log_cnt = 0;
    if (++noise_log_cnt % 100 == 0) {
        ESP_LOGW(TAG, "UART收到%d字节但未找到有效数据帧 (波特率=%d, 可能不匹配)",
                 len, RADAR_UART_BAUD);
    }
    return len;
}

/* ---- 获取UART通道的有人状态 (缓存值, 非阻塞) ---- */
int radar_presence_uart_get(void)
{
    return g_radar_uart_presence;
}

esp_err_t radar_send_cmd(const char *cmd)
{
    int len = strlen(cmd);
    int ret = uart_write_bytes(RADAR_UART_NUM, cmd, len);
    if (ret < 0) {
        ESP_LOGE(TAG, "UART发送失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}

int radar_read_data(uint8_t *buf, int max_len)
{
    int len = uart_read_bytes(RADAR_UART_NUM, buf, max_len, 0);
    return (len > 0) ? len : -1;
}
