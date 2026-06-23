/**
 * @file main.c
 * @brief IoT Gateway - 重构版 (学号: 0207)
 *
 * WiFi STA + WebSocket (port 81) + HTTP (port 80) + 传感器 + RGB LED
 * ESP32-S3 + FreeRTOS
 */

#include "app_config.h"
#include "driver_aht20.h"
#include "driver_light_sensor.h"
#include "driver_mq2.h"
#include "driver_rgb_led.h"
#include "driver_radar.h"
#include "bsp.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "driver/gpio.h"
#include <string.h>

#define TAG "MAIN"

/* 事件标志 */
#define EVT_WIFI_CONNECTED  BIT0
#define EVT_SOS_ACTIVE      BIT1

/* WebSocket 客户端 */
#define WS_MAX_CLIENTS  4
#define WS_PORT         81

/* 全局变量 */
EventGroupHandle_t       g_evt_group;
adc_oneshot_unit_handle_t g_adc1_handle;

static int               ws_clients[WS_MAX_CLIENTS];
static SemaphoreHandle_t ws_mutex;
static int               ws_seq = 0;
static int               g_manual_mode = 0;  /* 0=auto, 1=manual ON, 2=manual OFF */
static int               g_sos_active = 0;   /* 0=normal, 1=SOS emergency */
static TickType_t        g_manual_on_ticks = 0;  /* 手动开灯时刻 */
#define MANUAL_ON_TIMEOUT_MS   (5 * 60 * 1000)   /* 手动开灯后无人5分钟自动转AUTO */

static sensor_data_t     g_sensor_data;
static SemaphoreHandle_t g_sensor_mutex;

/* ==================== WiFi STA ==================== */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "WiFi 断开 (reason=%d), 重连...", ev->reason);
        xEventGroupClearBits(g_evt_group, EVT_WIFI_CONNECTED);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi 已连接! IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(g_evt_group, EVT_WIFI_CONNECTED);
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {0};
    strcpy((char *)wifi_cfg.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_cfg.sta.password, WIFI_PASSWORD);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA 初始化, SSID: %s", WIFI_SSID);
}

/* ==================== WebSocket 帧 ==================== */

static void ws_send_frame(int sock, const char *payload, int len)
{
    if (sock < 0) return;

    uint8_t header[10];
    int hlen;

    if (len < 126) {
        header[0] = 0x81; header[1] = (uint8_t)len; hlen = 2;
    } else if (len < 65536) {
        header[0] = 0x81; header[1] = 126;
        header[2] = (len >> 8) & 0xFF; header[3] = len & 0xFF; hlen = 4;
    } else {
        header[0] = 0x81; header[1] = 127;
        for (int i = 0; i < 8; i++) header[2 + i] = (len >> (56 - 8 * i)) & 0xFF;
        hlen = 10;
    }

    struct iovec iov[2];
    iov[0].iov_base = header; iov[0].iov_len = hlen;
    iov[1].iov_base = (void *)payload; iov[1].iov_len = len;

    struct msghdr msg = {0};
    msg.msg_iov = iov; msg.msg_iovlen = 2;
    sendmsg(sock, &msg, 0);
}

static void ws_broadcast(const char *payload, int len)
{
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_clients[i] >= 0) ws_send_frame(ws_clients[i], payload, len);
    }
    xSemaphoreGive(ws_mutex);
}

static void ws_add_client(int sock)
{
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) != pdTRUE) { close(sock); return; }
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_clients[i] < 0) {
            ws_clients[i] = sock;
            ESP_LOGI(TAG, "WS client #%d (fd=%d)", i, sock);
            xSemaphoreGive(ws_mutex);
            return;
        }
    }
    xSemaphoreGive(ws_mutex);
    ESP_LOGW(TAG, "WS 已满, 拒绝 fd=%d", sock);
    close(sock);
}

static int ws_handshake(int sock, const char *req, int req_len)
{
    char *key = strstr(req, "Sec-WebSocket-Key: ");
    if (!key) return -1;
    key += 19;
    char *end = strstr(key, "\r\n");
    if (!end) return -1;
    *end = '\0';

    /* sha1(key + GUID) */
    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t sha1[20];
    char concat[256];
    int klen = strlen(key), glen = strlen(guid);
    memcpy(concat, key, klen);
    memcpy(concat + klen, guid, glen);
    mbedtls_sha1((uint8_t *)concat, klen + glen, sha1);

    char accept[64];
    size_t olen;
    mbedtls_base64_encode((uint8_t *)accept, sizeof(accept), &olen, sha1, 20);
    accept[olen] = '\0';

    char resp[256];
    int rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);

    return (send(sock, resp, rlen, 0) == rlen) ? 0 : -1;
}

/* ==================== WebSocket TCP 服务器 ==================== */

static void ws_server_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "WS socket() 失败");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(WS_PORT);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "WS bind() 失败");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 4) < 0) {
        ESP_LOGE(TAG, "WS listen() 失败");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "WebSocket 服务器启动: ws://<IP>:%d/", WS_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        /* 设置 socket 超时，防止 recv 永久阻塞 */
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        /* 读取 HTTP 升级请求 */
        char buf[1024];
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0 || ws_handshake(client, buf, n) != 0) {
            close(client);
            continue;
        }

        ws_add_client(client);
    }

    close(listen_sock);
    vTaskDelete(NULL);
}

/* ==================== HTTP 服务器 ==================== */

static const char INDEX_HTML[] =
"<!DOCTYPE html><html lang='zh'><head>"
"<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>银发智慧网关</title>"
"<style>"
":root{--bg:#0a0e1a;--card:#141b2d;--border:#1e2a45;--text:#c8d6e5;--sub:#6b7d99;--dim:#3d4f6b;"
"--blue:#3b82f6;--green:#10b981;--red:#ef4444;--amber:#f59e0b;--purple:#8b5cf6;--cyan:#06b6d4}"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;overflow-x:hidden}"
"body::before{content:'';position:fixed;top:-50%;left:-50%;width:200%;height:200%;background:radial-gradient(circle at 20% 80%,rgba(59,130,246,.08) 0%,transparent 50%),radial-gradient(circle at 80% 20%,rgba(139,92,246,.06) 0%,transparent 50%);pointer-events:none;z-index:0}"
".wrap{position:relative;z-index:1;max-width:440px;margin:0 auto;padding:12px 14px 20px}"
".topbar{display:flex;align-items:center;justify-content:space-between;padding:4px 0 12px}"
".topbar .logo{font-size:17px;font-weight:700;background:linear-gradient(135deg,var(--blue),var(--cyan));-webkit-background-clip:text;-webkit-text-fill-color:transparent;letter-spacing:.5px}"
".topbar .clock{font-size:12px;color:var(--sub);font-variant-numeric:tabular-nums}"
".status-row{display:flex;gap:6px;margin-bottom:10px;flex-wrap:wrap}"
".status-pill{display:flex;align-items:center;gap:4px;padding:4px 10px;border-radius:20px;font-size:10px;font-weight:600;background:var(--card);border:1px solid var(--border)}"
".status-pill .dot{width:6px;height:6px;border-radius:50%}"
".dot-ok{background:var(--green);box-shadow:0 0 6px var(--green)}"
".dot-err{background:var(--red);box-shadow:0 0 6px var(--red)}"
".dot-auto{background:var(--purple);box-shadow:0 0 6px var(--purple)}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}"
".scard{position:relative;background:var(--card);border:1px solid var(--border);border-radius:16px;padding:14px 12px;overflow:hidden;transition:border-color .3s}"
".scard::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;border-radius:16px 16px 0 0}"
".scard.temp::before{background:linear-gradient(90deg,#3b82f6,#f59e0b)}"
".scard.humi::before{background:linear-gradient(90deg,#06b6d4,#3b82f6)}"
".scard.light::before{background:linear-gradient(90deg,#f59e0b,#fbbf24)}"
".scard.gas::before{background:linear-gradient(90deg,#10b981,#ef4444)}"
".scard .icon{font-size:20px;margin-bottom:4px}"
".scard .label{font-size:10px;color:var(--sub);text-transform:uppercase;letter-spacing:1.5px;margin-bottom:4px}"
".scard .val{font-size:32px;font-weight:700;line-height:1;margin-bottom:2px}"
".scard .unit{font-size:13px;font-weight:400;color:var(--sub);margin-left:2px}"
".scard .sub{font-size:10px;color:var(--dim);margin-top:2px}"
".scard .bar-wrap{height:3px;background:var(--border);border-radius:2px;margin-top:6px;overflow:hidden}"
".scard .bar-fill{height:100%;border-radius:2px;transition:width .5s,background .5s}"
".ctrl-card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:14px;margin-bottom:8px}"
".rcard{display:flex;align-items:center;gap:12px;background:var(--card);border:1px solid var(--border);border-radius:16px;padding:14px;margin-bottom:8px}"
".rcard .rdot{width:12px;height:12px;border-radius:50%;flex-shrink:0;transition:background .3s,box-shadow .3s}"
".rdot-on{background:var(--green);box-shadow:0 0 10px var(--green)}"
".rdot-off{background:var(--dim);box-shadow:none}"
".rcard .rlabel{font-size:10px;color:var(--sub);text-transform:uppercase;letter-spacing:1.5px}"
".rcard .rval{font-size:15px;font-weight:600;margin-top:2px}"
".ctrl-card .ctitle{font-size:10px;color:var(--sub);text-transform:uppercase;letter-spacing:1.5px;margin-bottom:10px;text-align:center}"
".ctrl-row{display:flex;gap:8px}"
".btn{flex:1;padding:12px 8px;border:none;border-radius:12px;font-size:13px;font-weight:600;cursor:pointer;color:#fff;transition:all .2s;position:relative;overflow:hidden}"
".btn::after{content:'';position:absolute;inset:0;background:rgba(255,255,255,0);transition:background .2s}"
".btn:active::after{background:rgba(255,255,255,.15)}"
".btn:active{transform:scale(.96)}"
".b-on{background:linear-gradient(135deg,#059669,#047857);box-shadow:0 2px 8px rgba(16,185,129,.25)}"
".b-off{background:linear-gradient(135deg,#64748b,#475569);box-shadow:0 2px 8px rgba(100,116,139,.2)}"
".b-auto{background:linear-gradient(135deg,#7c3aed,#6d28d9);box-shadow:0 2px 8px rgba(124,58,237,.25)}"
".b-sos{width:100%;background:linear-gradient(135deg,#dc2626,#991b1b);box-shadow:0 2px 8px rgba(239,68,68,.3);font-size:15px;padding:14px;letter-spacing:2px}"
".b-sos.sos-on{animation:sosBtnPulse .8s ease-in-out infinite}"
"@keyframes sosBtnPulse{0%,100%{box-shadow:0 0 12px rgba(239,68,68,.4)}50%{box-shadow:0 0 32px rgba(239,68,68,.8),0 0 64px rgba(239,68,68,.3)}}"
".btn.active{box-shadow:0 0 0 3px rgba(255,255,255,.35),0 4px 16px rgba(0,0,0,.4);transform:translateY(-1px)}"
".sos-banner{display:none;background:linear-gradient(135deg,#991b1b,#7f1d1d);border:1px solid #ef4444;border-radius:14px;padding:10px 14px;margin-bottom:8px;text-align:center;animation:sosPulse 1s ease-in-out infinite}"
".sos-banner .sos-title{font-size:14px;font-weight:700;color:#fca5a5}"
".sos-banner .sos-sub{font-size:10px;color:#f87171;margin-top:2px}"
"@keyframes sosPulse{0%,100%{box-shadow:0 0 12px rgba(239,68,68,.3)}50%{box-shadow:0 0 28px rgba(239,68,68,.6)}}"
".footer{text-align:center;padding:8px 0}"
".watermark{font-size:9px;color:var(--dim);letter-spacing:1px;opacity:.6}"
"</style></head><body><div class='wrap'>"
"<div class='topbar'>"
"<div><div class='logo'>银发智慧网关</div></div>"
"<div class='clock' id='clock'>--</div>"
"</div>"
"<div class='sos-banner' id='sos_banner'><div class='sos-title'>SOS 紧急求助！</div><div class='sos-sub'>已通知云端，请立即响应</div></div>"
"<div class='status-row'>"
"<div class='status-pill'><span class='dot dot-err' id='dot_wifi'></span><span id='wb'>WiFi --</span></div>"
"<div class='status-pill'><span class='dot dot-auto' id='dot_mode'></span><span id='mode'>AUTO</span></div>"
"<div class='status-pill' style='margin-left:auto'><span id='hp'>--</span></div>"
"</div>"
"<div class='grid'>"
"<div class='scard temp'><div class='icon'>🌡️</div><div class='label'>温度</div><div class='val' id='t'>--<span class='unit'>℃</span></div><div class='sub' id='t_sub'></div><div class='bar-wrap'><div class='bar-fill' id='tb' style='width:0;background:var(--blue)'></div></div></div>"
"<div class='scard humi'><div class='icon'>💧</div><div class='label'>湿度</div><div class='val' id='h'>--<span class='unit'>%</span></div><div class='sub' id='h_sub'></div></div>"
"<div class='scard light'><div class='icon'>☀️</div><div class='label'>光照</div><div class='val' id='lv'>--</div><div class='sub' id='lt'></div><div class='bar-wrap'><div class='bar-fill' id='lb' style='width:0;background:var(--amber)'></div></div></div>"
"<div class='scard gas'><div class='icon'>🔥</div><div class='label'>燃气浓度</div><div class='val' id='gv'>--</div><div class='sub' id='gt'></div><div class='bar-wrap'><div class='bar-fill' id='gb' style='width:0;background:var(--green)'></div></div></div>"
"</div>"
"<div class='rcard' id='r_card'>"
"<div class='rdot rdot-off' id='rdot'></div>"
"<div><div class='rlabel'>毫米波雷达 · 人体检测</div>"
"<div class='rval' id='rv'>暂无数据</div></div>"
"</div>"
"<div class='ctrl-card'>"
"<div class='ctitle'>灯光控制</div>"
"<div class='ctrl-row'>"
"<button class='btn b-on' id='btn_on' onclick='sendCmd(\"ON\")'>开灯</button>"
"<button class='btn b-off' id='btn_off' onclick='sendCmd(\"OFF\")'>关灯</button>"
"<button class='btn b-auto active' id='btn_auto' onclick='sendCmd(\"AUTO\")'>自动</button>"
"</div></div>"
"<div class='ctrl-card'>"
"<div class='ctitle'>紧急求助</div>"
"<button class='btn b-sos' id='btn_sos' onclick='sendCmd(\"SOS\")'>🆘 SOS 一键求助</button>"
"</div>"
"<div class='footer'><div class='watermark'>学号 0207 | IoT Gateway v2.0</div></div></div>"
"<script>"
"function pad(n){return n<10?'0'+n:n}"
"function tick(){var n=new Date();"
"document.getElementById('clock').textContent="
"n.getFullYear()+'-'+pad(n.getMonth()+1)+'-'+pad(n.getDate())+' '+pad(n.getHours())+':'+pad(n.getMinutes())+':'+pad(n.getSeconds());}"
"var ws=new WebSocket('ws://'+location.hostname+':81');"
"ws.onmessage=function(e){"
"var d=JSON.parse(e.data);"
"var t=d.temperature,h=d.humidity;"
"document.getElementById('t').innerHTML=t.toFixed(1)+'<span class=\"unit\">℃</span>';"
"document.getElementById('t_sub').textContent=t>60?'⚠ 高温':t>35?'偏热':t<10?'偏冷':'舒适';"
"var tb=document.getElementById('tb');var tp=Math.min(100,Math.max(0,t/60*100));"
"tb.style.width=tp+'%';"
"tb.style.background='linear-gradient(90deg,#3b82f6,#ef4444)';"
"document.getElementById('h').innerHTML=h.toFixed(1)+'<span class=\"unit\">%</span>';"
"document.getElementById('h_sub').textContent=h>75?'潮湿':h<40?'干燥':'适宜';"
"var rp=d.radar;var rd=document.getElementById('rdot'),rv=document.getElementById('rv');"
"if(rp){rd.className='rdot rdot-on';rv.textContent='🚶 有人';}"
"else{rd.className='rdot rdot-off';rv.textContent='无人';}"
"var la=d.light_adc;"
"document.getElementById('lv').textContent=la;"
"var lb=document.getElementById('lb');"
"if(la<1000){document.getElementById('lt').textContent='🌞 明亮';lb.style.width='25%';lb.style.background='linear-gradient(90deg,#fde047,#fbbf24)';}"
"else if(la<2000){document.getElementById('lt').textContent='🌤 正常';lb.style.width='55%';lb.style.background='linear-gradient(90deg,#fb923c,#f97316)';}"
"else{document.getElementById('lt').textContent='🌙 较暗';lb.style.width='90%';lb.style.background='linear-gradient(90deg,#a78bfa,#ef4444)';}"
"var gs=['🟢 正常','🟡 轻度','🟠 中度','🔴 重度','⛔ 危险'];"
"var gc=['var(--green)','#eab308','#f97316','#ef4444','#dc2626'];"
"document.getElementById('gv').innerHTML=gs[d.gas_level]||'--';"
"document.getElementById('gt').textContent='ADC: '+d.gas_adc;"
"var gb=document.getElementById('gb');"
"gb.style.width=Math.min(100,d.gas_adc/40.95)+'%';"
"gb.style.background=gc[d.gas_level]||gc[0];"
"var dw=document.getElementById('dot_wifi'),wb=document.getElementById('wb');"
"if(d.wifi){dw.className='dot dot-ok';wb.textContent='WiFi 已连接';}"
"else{dw.className='dot dot-err';wb.textContent='WiFi 断开';}"
"document.getElementById('hp').textContent='内存 '+(d.heap/1024).toFixed(0)+'KB';"
"var dm=document.getElementById('dot_mode'),m=document.getElementById('mode');"
"if(d.manual==1){dm.className='dot dot-ok';m.textContent='ON 手动开';}"
"else if(d.manual==2){dm.className='dot dot-err';m.textContent='OFF 手动关';}"
"else{dm.className='dot dot-auto';m.textContent='AUTO 自动';}"
"updateBtns(d.manual);"
"document.getElementById('sos_banner').style.display=d.sos?'block':'none';"
"var bs=document.getElementById('btn_sos');"
"if(d.sos){bs.className='btn b-sos sos-on';bs.textContent='🔴 SOS 求助中... 再次点击解除';}"
"else{bs.className='btn b-sos';bs.textContent='🆘 SOS 一键求助';}"
"};"
"ws.onclose=function(){setTimeout(function(){location.reload();},2000);};"
"function sendCmd(c){fetch('/cmd?c='+c).catch(function(){});}"
"function updateBtns(m){"
"document.getElementById('btn_on').className='btn b-on'+(m==1?' active':'');"
"document.getElementById('btn_off').className='btn b-off'+(m==2?' active':'');"
"document.getElementById('btn_auto').className='btn b-auto'+(m==0?' active':'');}"
"setInterval(tick,1000);tick();"
"</script></body></html>";

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, INDEX_HTML);
    return ESP_OK;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char q[64] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char *val = strstr(q, "c=");
        if (val) {
            val += 2;
            ESP_LOGI(TAG, "CMD: %s", val);
            if (strcmp(val, "ON") == 0) {
                g_manual_mode = 1;
                g_manual_on_ticks = xTaskGetTickCount();
                rgb_led_set_color(RGB_COLOR_WHITE);
            } else if (strcmp(val, "OFF") == 0) {
                g_manual_mode = 2;
                rgb_led_off();
            } else if (strcmp(val, "AUTO") == 0) {
                g_manual_mode = 0;
                buzzer_alarm(0);
            } else if (strcmp(val, "SOS") == 0) {
                if (g_sos_active) {
                    g_sos_active = 0;
                    xEventGroupClearBits(g_evt_group, EVT_SOS_ACTIVE);
                    rgb_led_off();
                    buzzer_alarm(0);
                    ESP_LOGI(TAG, "SOS 解除 (Web)");
                    char msg[64];
                    int len = snprintf(msg, sizeof(msg), "{\"sos\":0,\"event\":\"sos_clear\"}");
                    ws_broadcast(msg, len);
                } else {
                    g_sos_active = 1;
                    xEventGroupSetBits(g_evt_group, EVT_SOS_ACTIVE);
                    rgb_led_set_color(RGB_COLOR_RED);
                    buzzer_alarm(60000);
                    ESP_LOGW(TAG, "！！！SOS 紧急求助 (Web)！！！");
                    char msg[64];
                    int len = snprintf(msg, sizeof(msg), "{\"sos\":1,\"event\":\"sos_alert\"}");
                    ws_broadcast(msg, len);
                }
            }
        }
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static void start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t u = { .uri = "/",    .method = HTTP_GET, .handler = index_handler };
    httpd_register_uri_handler(server, &u);
    u.uri = "/cmd"; u.handler = cmd_handler;
    httpd_register_uri_handler(server, &u);

    ESP_LOGI(TAG, "HTTP 服务器启动: http://<IP>/");
}

/* ==================== 传感器 + LED 控制 ==================== */

static int get_gas_level(int adc)
{
    if (adc < GAS_LEVEL_MILD)   return 0;
    if (adc < GAS_LEVEL_MEDIUM) return 1;
    if (adc < GAS_LEVEL_SEVERE) return 2;
    return 3;
}

static void led_control(int gas_level, int light_adc, int radar_presence)
{
    /* SOS 最高优先级 */
    if (g_sos_active) {
        rgb_led_set_color(RGB_COLOR_RED);
        return;
    }

    /* 手动模式下跳过自动控制 */
    if (g_manual_mode != 0) return;

    /* 告警优先级: 重度/中度 > 亮灯 > 轻度 */
    if (gas_level >= 3)       rgb_led_set_color(RGB_COLOR_RED);
    else if (gas_level == 2)  rgb_led_set_color(RGB_COLOR_YELLOW);
    /* 自动开灯: 暗 AND 有人 才开 (优先级高于绿灯) */
    else if (light_adc > AUTO_LIGHT_THRESHOLD && radar_presence)
                              rgb_led_set_color(RGB_COLOR_WHITE);
    else if (gas_level == 1 && !radar_presence)  rgb_led_set_color(RGB_COLOR_GREEN);
    else                      rgb_led_off();
}

static void sensor_task(void *arg)
{
    int last_gas = 0xff, last_light = -1, last_radar = -1, last_raw = -1;
    int debug_cnt = 0;
    char json[512];

    while (1) {
        float t = 0, h = 0;
        int light_adc = 0, light_do = 0, gas_adc = 0, gas_do = 0;

        aht20_read(&t, &h);
        light_sensor_read_adc(&light_adc);
        light_sensor_read_digital(&light_do);
        mq2_read_adc(&gas_adc);
        mq2_read_digital(&gas_do);

        int gas_lev = get_gas_level(gas_adc);
        int radar_pre = radar_presence_read();
        int raw_level = gpio_get_level(5);

        /* 从UART读取雷达数据帧, 更新内部有人状态缓存 (被动监听, 不发送命令) */
        radar_read_uart_data();
        int radar_uart = radar_presence_uart_get();

        /* 双通道融合: IO引脚 或 UART数据帧 任一有效即判定有人 */
        int radar_combined = (radar_pre || radar_uart) ? 1 : 0;

        /* 调试: 每10秒打印一次雷达状态, GPIO原始电平变化时立即打印 */
        debug_cnt++;
        if (debug_cnt % 50 == 0 || raw_level != last_raw) {
            ESP_LOGI(TAG, "雷达 DEBUG: GPIO5原始=%d, IO解析=%d(%s), UART=%d(%s), 融合=%d(%s)",
                     raw_level, radar_pre, radar_pre ? "有人" : "无人",
                     radar_uart, radar_uart ? "有人" : "无人",
                     radar_combined, radar_combined ? "有人" : "无人");
            last_raw = raw_level;
        }

        if (xSemaphoreTake(g_sensor_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sensor_data_t d = {t, h, light_adc, light_do, gas_adc, gas_do, gas_lev, radar_combined};
            g_sensor_data = d;
            xSemaphoreGive(g_sensor_mutex);
        }

        /* LED 控制（仅在变化时更新，雷达状态变化也触发） */
        if (gas_lev != last_gas || light_adc != last_light || radar_combined != last_radar) {
            led_control(gas_lev, light_adc, radar_combined);
            last_gas = gas_lev;
            last_light = light_adc;
            last_radar = radar_combined;
        }

        /* 手动开灯后 5 分钟无人 → 自动转回 AUTO 关灯 */
        if (g_manual_mode == 1 && !radar_combined) {
            TickType_t elapsed = xTaskGetTickCount() - g_manual_on_ticks;
            if (elapsed >= pdMS_TO_TICKS(MANUAL_ON_TIMEOUT_MS)) {
                g_manual_mode = 0;
                rgb_led_off();
                ESP_LOGI(TAG, "手动开灯5分钟无人→自动关灯");
            }
        }

        /* 蜂鸣器：燃气中度以上 或 温度 > 60℃ */
        if (gas_lev >= 2 || t > 60.0f) {
            buzzer_alarm(3000);
        }

        /* WebSocket 广播 */
        int w = (xEventGroupGetBits(g_evt_group) & EVT_WIFI_CONNECTED) ? 1 : 0;
        int heap = (int)esp_get_free_heap_size();

        int jlen = snprintf(json, sizeof(json),
            "{\"id\":%d,\"temperature\":%.1f,\"humidity\":%.1f,"
            "\"light_adc\":%d,\"light_digital\":%d,"
            "\"gas_adc\":%d,\"gas_digital\":%d,\"gas_level\":%d,"
            "\"wifi\":%d,\"heap\":%d,\"manual\":%d,\"sos\":%d,\"radar\":%d}",
            ws_seq++, t, h, light_adc, light_do,
            gas_adc, gas_do, gas_lev, w, heap, g_manual_mode, g_sos_active, radar_combined);

        ws_broadcast(json, jlen);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_COLLECT_PERIOD_MS));
    }
}

/* ==================== SOS 按钮任务（消抖 + 切换） ==================== */

#define SOS_DEBOUNCE_MS  300

static void sos_task(void *arg)
{
    int last_state = 1;  /* 上拉，默认高电平=未按下 */
    int stable_state = 1;
    TickType_t debounce_start = 0;
    int debouncing = 0;

    while (1) {
        int raw = sos_button_pressed() ? 0 : 1;  /* 0=按下 */

        if (raw != last_state) {
            debounce_start = xTaskGetTickCount();
            debouncing = 1;
            last_state = raw;
        }

        if (debouncing && (xTaskGetTickCount() - debounce_start) >= pdMS_TO_TICKS(SOS_DEBOUNCE_MS)) {
            debouncing = 0;
            if (raw != stable_state) {
                stable_state = raw;
                if (stable_state == 0) {  /* 按下 */
                    if (g_sos_active) {
                        /* 再次按下 → 解除 SOS */
                        g_sos_active = 0;
                        xEventGroupClearBits(g_evt_group, EVT_SOS_ACTIVE);
                        rgb_led_off();
                        buzzer_alarm(0);
                        ESP_LOGI(TAG, "SOS 解除");
                        /* 广播 SOS 解除 */
                        char msg[64];
                        int len = snprintf(msg, sizeof(msg), "{\"sos\":0,\"event\":\"sos_clear\"}");
                        ws_broadcast(msg, len);
                    } else {
                        /* 首次按下 → 触发 SOS */
                        g_sos_active = 1;
                        xEventGroupSetBits(g_evt_group, EVT_SOS_ACTIVE);
                        rgb_led_set_color(RGB_COLOR_RED);
                        buzzer_alarm(60000);  /* 持续 60s */
                        ESP_LOGW(TAG, "！！！SOS 紧急求助！！！");
                        /* 广播 SOS 触发 */
                        char msg[64];
                        int len = snprintf(msg, sizeof(msg), "{\"sos\":1,\"event\":\"sos_alert\"}");
                        ws_broadcast(msg, len);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void hardware_init(void)
{
    ESP_ERROR_CHECK(rgb_led_init());
    rgb_led_off();

    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &g_adc1_handle));

    if (aht20_init() != ESP_OK)           ESP_LOGW(TAG, "AHT20 初始化失败");
    if (light_sensor_init() != ESP_OK)    ESP_LOGW(TAG, "光敏传感器初始化失败");
    if (mq2_init() != ESP_OK)             ESP_LOGW(TAG, "MQ-2 初始化失败");
    if (bsp_init() != ESP_OK)             ESP_LOGW(TAG, "BSP 初始化失败");
    if (radar_init() != ESP_OK)           ESP_LOGW(TAG, "毫米波雷达初始化失败");
    /* 雷达配置命令改用被动监听模式, 不再主动发送配置, 避免干扰雷达正常工作 */

    ESP_LOGI(TAG, "硬件初始化完成");
}

/* ==================== 主入口 ==================== */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  IoT Gateway v2.0  学号: 0207");
    ESP_LOGI(TAG, "  ESP32-S3 + WebSocket + STA");
    ESP_LOGI(TAG, "========================================");

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    g_evt_group = xEventGroupCreate();
    g_sensor_mutex = xSemaphoreCreateMutex();
    ws_mutex = xSemaphoreCreateMutex();
    assert(g_evt_group && g_sensor_mutex && ws_mutex);

    for (int i = 0; i < WS_MAX_CLIENTS; i++) ws_clients[i] = -1;

    hardware_init();
    wifi_init_sta();

    /* 等待 WiFi（15 秒超时） */
    ESP_LOGI(TAG, "等待 WiFi 连接 SSID=%s ...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(g_evt_group, EVT_WIFI_CONNECTED,
                                           pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if (bits & EVT_WIFI_CONNECTED) {
        ESP_LOGI(TAG, "WiFi 连接成功!");
    } else {
        ESP_LOGW(TAG, "WiFi 超时，后台继续重连");
    }

    start_http_server();
    xTaskCreate(ws_server_task, "ws_server", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(sensor_task,    "sensor",    4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(sos_task,       "sos_btn",   2048, NULL, tskIDLE_PRIORITY + 3, NULL);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  系统就绪!");
    ESP_LOGI(TAG, "  HTTP:  http://<ESP_IP>/");
    ESP_LOGI(TAG, "  WS:    ws://<ESP_IP>:81/");
    ESP_LOGI(TAG, "  SOS:   BOOT按键(GPIO%d) 一键求助", SOS_BUTTON_IO);
    ESP_LOGI(TAG, "========================================");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        bits = xEventGroupGetBits(g_evt_group);
        ESP_LOGI(TAG, "WiFi:%s  SOS:%s  Heap:%d",
                 (bits & EVT_WIFI_CONNECTED) ? "OK" : "NO",
                 (bits & EVT_SOS_ACTIVE) ? "ON" : "OFF",
                 (int)esp_get_free_heap_size());
    }
}