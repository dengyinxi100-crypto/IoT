/**
 * @file driver_radar.h
 * @brief 毫米波雷达驱动 (24GHz LD2410 兼容, 人体存在/移动检测)
 *
 * 引脚: IO=GPIO5 (存在输出, 低电平=有人), RX=GPIO6, TX=GPIO7 (UART2)
 */

#ifndef DRIVER_RADAR_H
#define DRIVER_RADAR_H

#include "app_config.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化毫米波雷达 (GPIO + UART)
 * @return ESP_OK 成功
 */
esp_err_t radar_init(void);

/**
 * @brief 通过UART发送配置命令唤醒雷达 (LD2410进入+退出配置模式)
 * @return ESP_OK 成功
 */
esp_err_t radar_send_config(void);

/**
 * @brief 读取雷达存在检测状态 (IO引脚)
 * @return 1=有人, 0=无人
 */
int radar_presence_read(void);

/**
 * @brief 从UART读取并解析雷达数据帧, 更新内部有人状态缓存
 * @return 读取字节数, <0 表示无数据
 */
int radar_read_uart_data(void);

/**
 * @brief 获取UART通道解析到的有人状态 (缓存值, 非阻塞)
 * @return 1=有人, 0=无人
 */
int radar_presence_uart_get(void);

/**
 * @brief 通过UART向雷达发送指令
 * @param[in] cmd 指令字符串
 * @return ESP_OK 成功
 */
esp_err_t radar_send_cmd(const char *cmd);

/**
 * @brief 通过UART读取雷达返回数据 (非阻塞)
 * @param[out] buf 接收缓冲区
 * @param[in] max_len 最大读取长度
 * @return 实际读取字节数, <0 表示无数据
 */
int radar_read_data(uint8_t *buf, int max_len);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_RADAR_H */
