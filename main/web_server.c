#include "web_server.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "watchdog_timer.h"
#include "nvs_storage.h"
#include "relay_controller.h"

static const char *TAG = "web_server";

static device_config_t *s_config = NULL;

/* ── Embedded HTML ──────────────────────────────────────────────────────── */

static const char INDEX_HTML[] =
"<!DOCTYPE html><html lang=\"en\"><head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>ESP32-C6 Watchdog</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#121212;color:#e0e0e0;font-family:Arial,sans-serif;padding:20px}"
"h1{color:#90caf9;margin-bottom:20px}"
"h2{color:#80cbc4;margin:20px 0 10px}"
".card{background:#1e1e1e;border-radius:8px;padding:20px;margin-bottom:20px}"
".countdown{font-size:3em;color:#a5d6a7;text-align:center;padding:20px 0}"
".countdown.warn{color:#ffcc02}"
".countdown.critical{color:#ef5350}"
".row{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:10px}"
".btn{padding:10px 20px;border:none;border-radius:5px;cursor:pointer;font-size:1em}"
".btn-primary{background:#1976d2;color:#fff}"
".btn-warning{background:#f57c00;color:#fff}"
".btn:hover{opacity:.85}"
"label{display:block;margin-bottom:4px;color:#b0bec5;font-size:.9em}"
"input,select{width:100%;padding:8px;background:#2d2d2d;color:#e0e0e0;"
"border:1px solid #444;border-radius:4px;margin-bottom:10px}"
".stat{display:flex;justify-content:space-between;padding:6px 0;"
"border-bottom:1px solid #333}"
".stat span:last-child{color:#a5d6a7}"
"</style></head><body>"
"<h1>&#x1F4E1; ESP32-C6 Watchdog Relay</h1>"

/* Status card */
"<div class=\"card\">"
"<h2>Status</h2>"
"<div class=\"countdown\" id=\"cd\">--:--</div>"
"<div id=\"stats\"></div>"
"</div>"

/* Controls */
"<div class=\"card\">"
"<h2>Controls</h2>"
"<div class=\"row\">"
"<button class=\"btn btn-primary\" onclick=\"resetTimer()\">&#x23F1; Reset Timer</button>"
"<button class=\"btn btn-warning\" onclick=\"resetAttempts()\">&#x21BA; Reset Attempts</button>"
"</div>"
"</div>"

/* Configuration */
"<div class=\"card\">"
"<h2>Configuration</h2>"
"<form id=\"cfgForm\" onsubmit=\"saveConfig(event)\">"
"<label>Watchdog Timeout (minutes)"
"<input type=\"number\" id=\"timeout\" min=\"1\" max=\"1440\"></label>"
"<label>Turn-off Period (seconds)"
"<input type=\"number\" id=\"off_period\" min=\"1\" max=\"300\"></label>"
"<label>Max Restart Attempts"
"<input type=\"number\" id=\"max_attempts\" min=\"1\" max=\"255\"></label>"
"<label>Relay GPIO Pin"
"<input type=\"number\" id=\"relay_pin\" min=\"0\" max=\"21\"></label>"
"<label>LED GPIO Pin"
"<input type=\"number\" id=\"led_pin\" min=\"0\" max=\"21\"></label>"
"<label>Button GPIO Pin"
"<input type=\"number\" id=\"btn_pin\" min=\"0\" max=\"21\"></label>"
"<label>Serial Mode"
"<select id=\"serial_mode\">"
"<option value=\"0\">TTL UART</option>"
"<option value=\"1\">USB CDC</option>"
"</select></label>"
"<label>WiFi SSID"
"<input type=\"text\" id=\"ssid\" maxlength=\"31\"></label>"
"<label>WiFi Password"
"<input type=\"password\" id=\"wifi_pass\" maxlength=\"63\"></label>"
"<label>Hide SSID"
"<select id=\"hidden\">"
"<option value=\"0\">No</option>"
"<option value=\"1\">Yes</option>"
"</select></label>"
"<button class=\"btn btn-primary\" type=\"submit\">&#x1F4BE; Save Configuration</button>"
"</form>"
"</div>"

"<script>"
"function fmt(ms){"
"  if(ms<=0)return'00:00';"
"  var s=Math.floor(ms/1000),m=Math.floor(s/60);"
"  return String(m).padStart(2,'0')+':'+String(s%60).padStart(2,'0');"
"}"
"function updateStatus(){"
"  fetch('/api/status').then(r=>r.json()).then(d=>{"
"    var cd=document.getElementById('cd');"
"    cd.textContent=fmt(d.remaining_ms);"
"    cd.className='countdown'+(d.remaining_ms<60000?' critical':d.remaining_ms<300000?' warn':'');"
"    var s='<div class=\"stat\"><span>Restart Attempts</span><span>'+d.restart_attempts+'/'+d.max_attempts+'</span></div>';"
"    var lc=d.last_comm>0?Math.round((d.timeout_ms-d.remaining_ms)/1000)+'s ago':'Never';"
"    s+='<div class=\"stat\"><span>Last Communication</span><span>'+lc+'</span></div>';"
"    document.getElementById('stats').innerHTML=s;"
"  }).catch(()=>{});"
"}"
"function loadConfig(){"
"  fetch('/api/config').then(r=>r.json()).then(d=>{"
"    document.getElementById('timeout').value=Math.round(d.watchdog_timeout_ms/60000);"
"    document.getElementById('off_period').value=Math.round(d.turn_off_period_ms/1000);"
"    document.getElementById('max_attempts').value=d.max_restart_attempts;"
"    document.getElementById('relay_pin').value=d.relay_pin;"
"    document.getElementById('led_pin').value=d.led_pin;"
"    document.getElementById('btn_pin').value=d.button_pin;"
"    document.getElementById('serial_mode').value=d.serial_mode;"
"    document.getElementById('ssid').value=d.wifi_ssid;"
"    document.getElementById('hidden').value=d.wifi_hidden?'1':'0';"
"  }).catch(()=>{});"
"}"
"function resetTimer(){"
"  fetch('/api/reset_timer',{method:'POST'}).then(()=>updateStatus());"
"}"
"function resetAttempts(){"
"  fetch('/api/reset_attempts',{method:'POST'}).then(()=>updateStatus());"
"}"
"function saveConfig(e){"
"  e.preventDefault();"
"  var b={"
"    watchdog_timeout_ms:parseInt(document.getElementById('timeout').value)*60000,"
"    turn_off_period_ms:parseInt(document.getElementById('off_period').value)*1000,"
"    max_restart_attempts:parseInt(document.getElementById('max_attempts').value),"
"    relay_pin:parseInt(document.getElementById('relay_pin').value),"
"    led_pin:parseInt(document.getElementById('led_pin').value),"
"    button_pin:parseInt(document.getElementById('btn_pin').value),"
"    serial_mode:parseInt(document.getElementById('serial_mode').value),"
"    wifi_ssid:document.getElementById('ssid').value,"
"    wifi_password:document.getElementById('wifi_pass').value,"
"    wifi_hidden:document.getElementById('hidden').value==='1'"
"  };"
"  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})"
"  .then(r=>r.json()).then(d=>alert(d.status||'Saved'));"
"}"
"loadConfig();"
"updateStatus();"
"setInterval(updateStatus,2000);"
"</script>"
"</body></html>";

/* ── Handlers ────────────────────────────────────────────────────────────── */

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_api_status(httpd_req_t *req)
{
    int64_t remaining  = watchdog_get_remaining_ms();
    int64_t last_comm  = watchdog_get_last_comm_ms();
    uint8_t attempts   = watchdog_get_restart_attempts();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "remaining_ms",      (double)remaining);
    cJSON_AddNumberToObject(root, "restart_attempts",  attempts);
    cJSON_AddNumberToObject(root, "max_attempts",      s_config->max_restart_attempts);
    cJSON_AddNumberToObject(root, "timeout_ms",        (double)s_config->watchdog_timeout_ms);
    cJSON_AddNumberToObject(root, "last_comm",         (double)last_comm);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t handler_api_get_config(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "watchdog_timeout_ms",  s_config->watchdog_timeout_ms);
    cJSON_AddNumberToObject(root, "turn_off_period_ms",   s_config->turn_off_period_ms);
    cJSON_AddNumberToObject(root, "max_restart_attempts", s_config->max_restart_attempts);
    cJSON_AddNumberToObject(root, "relay_pin",            s_config->relay_pin);
    cJSON_AddNumberToObject(root, "led_pin",              s_config->led_pin);
    cJSON_AddNumberToObject(root, "button_pin",           s_config->button_pin);
    cJSON_AddNumberToObject(root, "serial_mode",          s_config->serial_mode);
    cJSON_AddStringToObject(root, "wifi_ssid",            s_config->wifi_ssid);
    cJSON_AddBoolToObject(root,   "wifi_hidden",          s_config->wifi_hidden);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t handler_api_post_config(httpd_req_t *req)
{
    char buf[512] = { 0 };
    int received  = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *item;

#define CFG_GET_U32(field, key) \
    item = cJSON_GetObjectItem(root, key); \
    if (item && cJSON_IsNumber(item)) s_config->field = (uint32_t)item->valuedouble;

#define CFG_GET_U8(field, key) \
    item = cJSON_GetObjectItem(root, key); \
    if (item && cJSON_IsNumber(item)) s_config->field = (uint8_t)item->valuedouble;

#define CFG_GET_STR(field, key) \
    item = cJSON_GetObjectItem(root, key); \
    if (item && cJSON_IsString(item)) strlcpy(s_config->field, item->valuestring, sizeof(s_config->field));

    CFG_GET_U32(watchdog_timeout_ms,  "watchdog_timeout_ms")
    CFG_GET_U32(turn_off_period_ms,   "turn_off_period_ms")
    CFG_GET_U8 (max_restart_attempts, "max_restart_attempts")
    CFG_GET_U8 (relay_pin,            "relay_pin")
    CFG_GET_U8 (led_pin,              "led_pin")
    CFG_GET_U8 (button_pin,           "button_pin")

    item = cJSON_GetObjectItem(root, "serial_mode");
    if (item && cJSON_IsNumber(item)) {
        s_config->serial_mode = (serial_mode_t)(int)item->valuedouble;
    }

    CFG_GET_STR(wifi_ssid,     "wifi_ssid")
    CFG_GET_STR(wifi_password, "wifi_password")

    item = cJSON_GetObjectItem(root, "wifi_hidden");
    if (item) {
        s_config->wifi_hidden = cJSON_IsTrue(item);
    }

    cJSON_Delete(root);

    /* Apply watchdog settings immediately */
    watchdog_set_timeout(s_config->watchdog_timeout_ms);
    watchdog_set_max_attempts(s_config->max_restart_attempts);

    nvs_storage_save_config(s_config);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t handler_api_reset_timer(httpd_req_t *req)
{
    watchdog_communication_received();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t handler_api_reset_attempts(httpd_req_t *req)
{
    watchdog_reset_attempts();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ── Server start ────────────────────────────────────────────────────────── */

void web_server_start(device_config_t *config)
{
    s_config = config;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",                 .method = HTTP_GET,  .handler = handler_root             },
        { .uri = "/api/status",       .method = HTTP_GET,  .handler = handler_api_status       },
        { .uri = "/api/config",       .method = HTTP_GET,  .handler = handler_api_get_config   },
        { .uri = "/api/config",       .method = HTTP_POST, .handler = handler_api_post_config  },
        { .uri = "/api/reset_timer",  .method = HTTP_POST, .handler = handler_api_reset_timer  },
        { .uri = "/api/reset_attempts",.method= HTTP_POST, .handler = handler_api_reset_attempts},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP server started");
}
