#include "web_gamepad.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_battery.h"
#include "board_clock.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "gb_emu.h"
#include "gbc_emu.h"
#include "nvs_flash.h"

static const char *TAG = "web_gamepad";

#define WEB_GAMEPAD_AP_SSID        "ESP32-GB"
#define WEB_GAMEPAD_AP_PASSWORD    "12345678"
#define WEB_GAMEPAD_AP_CHANNEL     6
#define WEB_GAMEPAD_AP_MAX_CONN    2

static httpd_handle_t s_server = NULL;
static bool s_started = false;
static volatile uint8_t s_joypad_state = 0xFF;
static volatile uint8_t s_volume = 75;

static const char s_index_html[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,minimum-scale=1,user-scalable=no,viewport-fit=cover'>"
    "<title>ESP32 GB Gamepad</title>"
    "<style>"
    "*{box-sizing:border-box;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;touch-action:none}"
    "html,body{margin:0;width:100%;height:100%;overflow:hidden;position:fixed;inset:0;background:#111;color:#eee;font-family:Arial,sans-serif;overscroll-behavior:none}"
    "body{padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left)}"
    "header{position:absolute;left:12px;right:12px;top:10px;display:grid;grid-template-columns:1fr auto 1fr;align-items:center;gap:10px;font-size:14px;color:#aaa;z-index:5}"
    ".headl{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
    ".headr{display:flex;align-items:center;justify-content:flex-end;gap:12px;white-space:nowrap}"
    "#bat{font-size:12px;color:#aaa;letter-spacing:.3px}"
    "#bat.low{color:#ff9b70}"
    "#status.ok{color:#70e070}#status.bad{color:#ff7070}"
    "#edit{position:absolute;right:12px;bottom:10px;width:84px;height:38px;border-radius:19px;font-size:13px;z-index:6}"
    "#edit.on{background:#eee;color:#111;border-color:#eee}"
    "#volbox{display:flex;align-items:center;justify-content:center;gap:8px;font-size:13px;color:#aaa;min-width:190px}"
    "#vol{width:118px;accent-color:#eee;touch-action:auto}"
    "#volv{display:inline-block;min-width:28px;text-align:right;color:#eee}"
    ".hint{position:absolute;left:12px;bottom:18px;font-size:12px;color:#777;z-index:1}"
    ".ctrl{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);z-index:3}"
    ".btn{border:2px solid #555;background:#222;color:#eee;font-weight:700;box-shadow:inset 0 0 0 1px #000}"
    ".btn.down{background:#eee;color:#111;border-color:#eee}"
    ".ab{width:94px;height:94px;border-radius:50%;font-size:28px}"
    ".sys{width:112px;height:48px;border-radius:24px;font-size:15px}"
    ".stick{width:178px;height:178px;border-radius:50%;border:3px solid #555;background:#181818;box-shadow:inset 0 0 0 1px #000}"
    ".stick.down{border-color:#eee;background:#222}"
    ".knob{position:absolute;left:50%;top:50%;width:74px;height:74px;border-radius:50%;transform:translate(-50%,-50%);background:#333;border:2px solid #777;box-shadow:inset 0 0 0 1px #000}"
    ".stick.down .knob{background:#eee;border-color:#eee}"
    ".editmark{outline:2px dashed #888;outline-offset:4px}"
    "</style></head><body>"
    "<header><div class='headl'>ESP32 GB Gamepad</div><div id='volbox'><span>VOL</span><input id='vol' type='range' min='0' max='100' value='75'><span id='volv'>75</span></div><div class='headr'><div id='bat'>BAT --%</div><div id='status' class='bad'>connecting</div></div></header>"
    "<button id='edit'>EDIT</button><div class='hint'>EDIT: drag buttons, tap EDIT to play</div>"
    "<div class='ctrl stick' data-id='stick'><div class='knob'></div></div>"
    "<button class='ctrl btn ab' data-id='b' data-bit='1'>B</button>"
    "<button class='ctrl btn ab' data-id='a' data-bit='0'>A</button>"
    "<button class='ctrl btn sys' data-id='select' data-bit='2'>SELECT</button>"
    "<button class='ctrl btn sys' data-id='start' data-bit='3'>START</button>"
    "<script>"
    "let ws,state=255,lastSent=-1,lastSendAt=0,sendTimer=0,volTimer=0,pressed=new Map(),edit=false,drag=null,stickId=null;"
    "const st=document.getElementById('status'),bat=document.getElementById('bat'),eb=document.getElementById('edit'),vol=document.getElementById('vol'),volv=document.getElementById('volv'),btns=[...document.querySelectorAll('.btn')],ctrls=[...document.querySelectorAll('.ctrl')],stick=document.querySelector('.stick'),knob=document.querySelector('.knob');"
    "const def={stick:[23,55],b:[73,55],a:[87,42],select:[43,88],start:[58,88]};"
    "function clamp(v,a,b){return Math.max(a,Math.min(b,v))}"
    "function posKey(id){return'gbpad.pos.'+id}"
    "function setPos(el,x,y){el.style.left=x+'%';el.style.top=y+'%'}"
    "function loadPos(){ctrls.forEach(b=>{let p=localStorage.getItem(posKey(b.dataset.id));p=p?JSON.parse(p):def[b.dataset.id];setPos(b,p[0],p[1])})}"
    "function savePos(el){localStorage.setItem(posKey(el.dataset.id),JSON.stringify([parseFloat(el.style.left),parseFloat(el.style.top)]))}"
    "function flushSend(){if(state===lastSent)return;if(ws&&ws.readyState===1){ws.send(new Uint8Array([state]));lastSent=state;lastSendAt=performance.now()}}"
    "function send(force=false){if(force){clearTimeout(sendTimer);sendTimer=0;lastSent=-1;flushSend();return}if(state===lastSent)return;let now=performance.now(),wait=40-(now-lastSendAt);if(wait<=0){clearTimeout(sendTimer);sendTimer=0;flushSend()}else if(!sendTimer){sendTimer=setTimeout(()=>{sendTimer=0;flushSend()},wait)}}"
    "function sendVol(force=false){let v=clamp(parseInt(vol.value||'75'),0,100);volv.textContent=v;localStorage.setItem('gbpad.volume',String(v));if(!ws||ws.readyState!==1)return;if(force){ws.send(new Uint8Array([240,v]));return}clearTimeout(volTimer);volTimer=setTimeout(()=>{volTimer=0;if(ws&&ws.readyState===1)ws.send(new Uint8Array([240,v]))},80)}"
    "function sendTime(){if(!ws||ws.readyState!==1)return;let ms=Date.now(),tz=new Date().getTimezoneOffset();let b=new Uint8Array(11),v=new DataView(b.buffer);b[0]=241;v.setUint32(1,ms%4294967296,true);v.setUint32(5,Math.floor(ms/4294967296),true);v.setInt16(9,tz,true);ws.send(b)}"
    "function rebuild(){state=255;pressed.forEach(b=>state&=~(1<<b));send()}"
    "function down(id,bit,el){pressed.set(id,Number(bit));el.classList.add('down');rebuild()}"
    "function up(id){let el=document.querySelector(`[data-id=\"${id}\"]`);pressed.delete(id);if(el)el.classList.remove('down');rebuild()}"
    "function resetStick(){stickId=null;stick.classList.remove('down');knob.style.left='50%';knob.style.top='50%';['joy-l','joy-r','joy-u','joy-d'].forEach(k=>pressed.delete(k))}"
    "function releaseAll(){pressed.clear();btns.forEach(b=>b.classList.remove('down'));resetStick();rebuild()}"
    "function stickBits(e){let r=stick.getBoundingClientRect(),cx=r.left+r.width/2,cy=r.top+r.height/2,rad=r.width/2;let dx=(e.clientX-cx)/rad,dy=(e.clientY-cy)/rad;let len=Math.hypot(dx,dy);if(len>1){dx/=len;dy/=len;len=1}knob.style.left=(50+dx*32)+'%';knob.style.top=(50+dy*32)+'%';['joy-l','joy-r','joy-u','joy-d'].forEach(k=>pressed.delete(k));if(len>.28){if(dx>.35)pressed.set('joy-r',4);else if(dx<-.35)pressed.set('joy-l',5);if(dy>.35)pressed.set('joy-d',7);else if(dy<-.35)pressed.set('joy-u',6)}stick.classList.toggle('down',len>.28);rebuild()}"
    "function connect(){ws=new WebSocket('ws://'+location.host+'/ws');ws.binaryType='arraybuffer';"
    "ws.onopen=()=>{st.textContent='connected';st.className='ok';lastSent=-1;send(true);sendVol(true);sendTime()};"
    "ws.onclose=()=>{st.textContent='disconnected';st.className='bad';setTimeout(connect,800)};"
    "ws.onerror=()=>{try{ws.close()}catch(e){}}}"
    "function pollBattery(){fetch('/battery',{cache:'no-store'}).then(r=>r.json()).then(j=>{if(!j.ok){bat.textContent='BAT --%';return}bat.textContent='BAT '+j.percent+'%';bat.classList.toggle('low',j.percent<=20);bat.title=j.mv+' mV'}).catch(()=>{bat.textContent='BAT --%'})}"
    "ctrls.forEach(b=>{b.addEventListener('pointerdown',e=>{if(b===stick)return;e.preventDefault();b.setPointerCapture(e.pointerId);"
    "if(edit){drag={el:b,id:e.pointerId};b.classList.add('editmark');return}down(b.dataset.id,b.dataset.bit,b)});"
    "b.addEventListener('pointermove',e=>{if(!edit||!drag||drag.id!==e.pointerId)return;e.preventDefault();"
    "let x=clamp(e.clientX/window.innerWidth*100,5,95),y=clamp(e.clientY/window.innerHeight*100,8,92);setPos(drag.el,x,y)});"
    "b.addEventListener('pointerup',e=>{e.preventDefault();if(edit&&drag&&drag.id===e.pointerId){savePos(drag.el);drag.el.classList.remove('editmark');drag=null}else up(b.dataset.id)});"
    "});"
    "stick.addEventListener('pointerdown',e=>{e.preventDefault();stick.setPointerCapture(e.pointerId);if(edit){drag={el:stick,id:e.pointerId};stick.classList.add('editmark');return}stickId=e.pointerId;stickBits(e)});"
    "stick.addEventListener('pointermove',e=>{e.preventDefault();if(edit&&drag&&drag.id===e.pointerId){let x=clamp(e.clientX/window.innerWidth*100,8,92),y=clamp(e.clientY/window.innerHeight*100,12,88);setPos(stick,x,y);return}if(stickId===e.pointerId)stickBits(e)});"
    "stick.addEventListener('pointerup',e=>{e.preventDefault();if(edit&&drag&&drag.id===e.pointerId){savePos(stick);stick.classList.remove('editmark');drag=null}else if(stickId===e.pointerId){resetStick();rebuild()}});"
    "stick.addEventListener('pointercancel',e=>{if(stickId===e.pointerId){resetStick();rebuild()}});"
    "eb.addEventListener('pointerdown',e=>{e.preventDefault();edit=!edit;eb.classList.toggle('on',edit);eb.textContent=edit?'PLAY':'EDIT';releaseAll()});"
    "vol.value=localStorage.getItem('gbpad.volume')||vol.value;volv.textContent=vol.value;vol.addEventListener('input',()=>sendVol(false));"
    "['pointerdown','pointermove','pointerup','touchstart','touchmove'].forEach(n=>vol.addEventListener(n,e=>e.stopPropagation(),{passive:true}));"
    "['touchstart','touchmove','gesturestart','gesturechange','dblclick','contextmenu'].forEach(n=>document.addEventListener(n,e=>{if(e.target===vol)return;e.preventDefault()},{passive:false}));"
    "window.addEventListener('blur',releaseAll);loadPos();connect();pollBattery();setInterval(pollBattery,10000);setInterval(sendTime,300000);"
    "</script></body></html>";

static esp_err_t web_gamepad_index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t web_gamepad_battery_handler(httpd_req_t *req)
{
    board_battery_status_t status = {0};
    esp_err_t ret = board_battery_read(&status);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char response[80] = {0};
    if (ret != ESP_OK) {
        snprintf(response, sizeof(response), "{\"ok\":false}");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    snprintf(response,
             sizeof(response),
             "{\"ok\":true,\"mv\":%" PRIu32 ",\"percent\":%u}",
             status.voltage_mv,
             status.percent);
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t web_gamepad_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Web gamepad connected");
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
    };

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (frame.len == 0 || frame.len > 16) {
        return ESP_OK;
    }

    uint8_t buffer[16] = {0};
    frame.payload = buffer;

    ret = httpd_ws_recv_frame(req, &frame, sizeof(buffer));
    if (ret != ESP_OK) {
        return ret;
    }

    if (frame.type == HTTPD_WS_TYPE_BINARY && frame.len >= 2 && buffer[0] == 0xF0) {
        uint8_t volume = buffer[1];
        if (volume > 100) {
            volume = 100;
        }
        s_volume = volume;
        gb_emu_set_volume(volume);
        gbc_emu_set_volume(volume);
        return ESP_OK;
    }

    if (frame.type == HTTPD_WS_TYPE_BINARY && frame.len >= 11 && buffer[0] == 0xF1) {
        uint64_t unix_ms = 0;
        for (int i = 0; i < 8; i++) {
            unix_ms |= ((uint64_t)buffer[1 + i]) << (i * 8);
        }

        int16_t timezone_offset_minutes = (int16_t)((uint16_t)buffer[9] | ((uint16_t)buffer[10] << 8));
        esp_err_t sync_ret = board_clock_sync_unix_ms(unix_ms, timezone_offset_minutes);
        if (sync_ret != ESP_OK) {
            ESP_LOGW(TAG, "sync phone time failed: %s", esp_err_to_name(sync_ret));
        }
        return ESP_OK;
    }

    if (frame.type == HTTPD_WS_TYPE_BINARY && frame.len >= 1) {
        uint8_t next_state = buffer[0];
        if (next_state != s_joypad_state) {
            s_joypad_state = next_state;
            gb_emu_set_joypad(next_state);
            gbc_emu_set_joypad(next_state);
        }
    }

    return ESP_OK;
}

static esp_err_t web_gamepad_start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 4096;
    config.task_priority = 3;
    config.core_id = 0;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "start http server failed");

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = web_gamepad_index_handler,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &index_uri), TAG, "register index uri failed");

    const httpd_uri_t battery_uri = {
        .uri = "/battery",
        .method = HTTP_GET,
        .handler = web_gamepad_battery_handler,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &battery_uri), TAG, "register battery uri failed");

    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = web_gamepad_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &ws_uri), TAG, "register websocket uri failed");

    return ESP_OK;
}

static esp_err_t web_gamepad_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs failed");
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t web_gamepad_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(web_gamepad_init_nvs(), TAG, "init nvs failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "init netif failed");

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "create event loop failed");
    }

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    init_config.wifi_task_core_id = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "init wifi failed");

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WEB_GAMEPAD_AP_SSID,
            .password = WEB_GAMEPAD_AP_PASSWORD,
            .ssid_len = strlen(WEB_GAMEPAD_AP_SSID),
            .channel = WEB_GAMEPAD_AP_CHANNEL,
            .max_connection = WEB_GAMEPAD_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(WEB_GAMEPAD_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set ap config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start wifi failed");
    ESP_RETURN_ON_ERROR(board_battery_init(), TAG, "init battery monitor failed");
    ESP_RETURN_ON_ERROR(web_gamepad_start_http_server(), TAG, "start web gamepad server failed");

    s_joypad_state = 0xFF;
    gb_emu_set_joypad(0xFF);
    gbc_emu_set_joypad(0xFF);
    gb_emu_set_volume(s_volume);
    gbc_emu_set_volume(s_volume);
    s_started = true;

    ESP_LOGI(TAG, "Web gamepad ready: connect WiFi SSID '%s', password '%s', open http://192.168.4.1",
             WEB_GAMEPAD_AP_SSID,
             WEB_GAMEPAD_AP_PASSWORD);
    return ESP_OK;
}

uint8_t web_gamepad_get_joypad_state(void)
{
    return s_joypad_state;
}

uint8_t web_gamepad_get_volume(void)
{
    return s_volume;
}
