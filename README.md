# 银发智慧居家物联网网关

基于 **ESP32-S3 + ESP-IDF v5.1.6** 的智能家居网关系统，面向银发居家场景，提供环境监测、智能照明、燃气告警、SOS 紧急求助等功能。

## 硬件平台

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3 | - |
| 温湿度传感器 | AHT20 | I2C (SDA=GPIO41, SCL=GPIO42) |
| 光敏传感器 | 光敏电阻模块 | AO=GPIO1(ADC1_CH0), DO=GPIO2 |
| 气体传感器 | MQ-2 | AO=GPIO3(ADC1_CH2), DO=GPIO4 |
| 毫米波雷达 | 24GHz 人体存在检测 | IO=GPIO5, RX=GPIO6, TX=GPIO7(UART2) |
| RGB LED | 共阳极模块 | R=GPIO40, G=GPIO39, B=GPIO38 |
| 蜂鸣器 | 有源蜂鸣器 | GPIO21 |
| SOS 按钮 | BOOT 按键 | GPIO0 |

## 功能特性

### 感知层
- **AHT20** 温湿度采集（0.1℃/0.1%RH 精度）
- **光敏传感器** ADC 光照强度采集
- **MQ-2** 可燃气体/烟雾浓度检测（四级告警：正常/轻度/中度/重度）
- **毫米波雷达** 24GHz 人体存在与移动检测（IO电平 + UART数据帧双通道融合）
- **SOS 按钮** 硬件消抖，一键求助 / 再次按下解除

### 网络层
- **WiFi STA** 模式连接路由器，断线自动重连
- **HTTP Server** (端口 80) - Web 仪表盘 + RESTful 控制 API
- **WebSocket Server** (端口 81) - 实时传感器数据推送（JSON 格式）

### 应用层
- **Web 仪表盘** 自适应移动端 UI，深色主题，实时显示温湿度、光照、燃气浓度、雷达状态
- **智能照明** 自动模式（暗+有人 → 开灯）+ 手动开/关 + 手动5分钟无人自动恢复
- **燃气告警** 中度以上触发蜂鸣器 + RGB 红灯闪烁
- **SOS 紧急求助** 按钮 / Web 端均可触发，红色爆闪 + 蜂鸣 60s + 全端广播

### LED 状态指示
| 颜色 | 含义 |
|------|------|
| 🟢 绿色 | 燃气轻度 + 无人 |
| 🟡 黄色 | 燃气中度告警 |
| 🔴 红色 | 燃气重度 / SOS 紧急 |
| ⚪ 白色 | 自动开灯 / 手动开灯 |
| ⚫ 熄灭 | 正常待机 |

## 系统架构

```
┌─────────────────────────────────────────┐
│              FreeRTOS 任务               │
│                                          │
│  ws_server_task  (4096B, Prio+3)        │
│  sensor_task     (4096B, Prio+2)        │
│  sos_task        (2048B, Prio+3)        │
│                                          │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│  │ AHT20    │ │ 光敏     │ │ MQ-2     │ │
│  │ 温湿度   │ │ 传感器   │ │ 气体     │ │
│  └──────────┘ └──────────┘ └──────────┘ │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│  │ 毫米波   │ │ SOS      │ │ RGB LED  │ │
│  │ 雷达     │ │ 按钮     │ │ 蜂鸣器   │ │
│  └──────────┘ └──────────┘ └──────────┘ │
└─────────────────────────────────────────┘
```

## 快速开始

### 环境要求

- ESP-IDF **v5.1.6**
- ESP32-S3 开发板
- CMake ≥ 3.16
- Ninja

### 配置传感器

| 传感器 | MCU 引脚 | 供电 | 备注 |
|--------|---------|------|------|
| AHT20 | SDA→GPIO41, SCL→GPIO42 | 3.3V | I2C地址 0x38 |
| 光敏传感器 | AO→GPIO1, DO→GPIO2 | 3.3V | DO 数字输出(低电平亮) |
| MQ-2 | AO→GPIO3, DO→GPIO4 | 5V | 需预热 |
| 毫米波雷达 | IO→GPIO5, RX→GPIO6, TX→GPIO7 | 5V | 波特率 115200 |
| RGB LED | R→GPIO40, G→GPIO39, B→GPIO38 | 3.3V | 共阳极 |
| 蜂鸣器 | VCC→GPIO21 | 3.3V | 有源 |
| SOS 按钮 | BOOT→GPIO0 | - | 板载按键 |

### 编译与烧录

```bash
# 设置环境
. <IDF_PATH>/export.sh

# 配置 WiFi（修改 main/app_config.h 中的 WIFI_SSID / WIFI_PASSWORD）

# 编译
idf.py build

# 烧录（COM8 为例）
idf.py -p COM8 flash

# 查看日志
idf.py -p COM8 monitor
```

### 使用说明

1. 上电后 ESP32-S3 自动连接 WiFi
2. 连接成功后，浏览器访问 `http://<ESP_IP>/` 打开仪表盘
3. Web 界面支持：
   - 实时查看温湿度、光照、燃气、雷达状态
   - 灯光控制（开/关/自动）
   - SOS 一键求助
4. 按下 **BOOT 键** 或 Web 端点击 SOS 按钮均可触发紧急求助

## 阈值配置

在 `main/app_config.h` 中可调整：

```c
#define AUTO_LIGHT_THRESHOLD    2000   /* 光照 ADC > 此值触发开灯 */
#define GAS_LEVEL_MILD           800   /* 轻度 */
#define GAS_LEVEL_MEDIUM        1500   /* 中度 */
#define GAS_LEVEL_SEVERE        2500   /* 重度 */
#define SENSOR_COLLECT_PERIOD_MS 200   /* 传感器采集周期 */
#define MANUAL_ON_TIMEOUT_MS 300000   /* 手动开灯5分钟无人 → 自动关 */
```

## 项目结构

```
IotGateway/
├── CMakeLists.txt              # 顶层 CMake
├── main/
│   ├── CMakeLists.txt          # 组件依赖
│   ├── main.c                  # 主入口 (HTTP/WS/传感器/LED/SOS)
│   ├── app_config.h            # 全局配置 & 引脚定义
│   ├── bsp.c / bsp.h           # 蜂鸣器 & 按键
│   ├── driver_aht20.c/h        # AHT20 驱动
│   ├── driver_light_sensor.c/h # 光敏传感器驱动
│   ├── driver_mq2.c/h          # MQ-2 气体传感器驱动
│   ├── driver_rgb_led.c/h      # RGB LED 驱动
│   └── driver_radar.c/h        # 毫米波雷达驱动
├── partitions.csv              # 分区表
├── sdkconfig                   # ESP-IDF 配置
└── sdkconfig.defaults          # 默认配置片段
```

## 通信协议

### WebSocket 数据格式 (Server → Client)

```json
{
  "id": 123,
  "temperature": 25.3,
  "humidity": 58.2,
  "light_adc": 1500,
  "light_digital": 1,
  "gas_adc": 400,
  "gas_digital": 0,
  "gas_level": 0,
  "wifi": 1,
  "heap": 245000,
  "manual": 0,
  "sos": 0,
  "radar": 1
}
```

### HTTP API

| 方法 | 路径 | 参数 | 说明 |
|------|------|------|------|
| GET | `/` | - | Web 仪表盘 |
| GET | `/cmd` | `?c=ON/OFF/AUTO/SOS` | 灯光控制 / SOS |

## License

MIT
