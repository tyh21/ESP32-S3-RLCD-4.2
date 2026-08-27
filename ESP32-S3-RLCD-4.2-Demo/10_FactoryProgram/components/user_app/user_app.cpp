#include <stdio.h>
#include <freertos/freeRTOS.h>
#include <esp_log.h>
#include "button_bsp.h"
#include "user_app.h"
#include "gui_guider.h"
#include "i2c_equipment.h"
#include "i2c_bsp.h"
#include "sdcard_bsp.h"
#include "codec_bsp.h"
#include "adc_bsp.h"
#include "esp_wifi_bsp.h"
#include "ble_scan_bsp.h"
#include <time.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <cstring>
#include <algorithm>

#include "weather_client.h"
#include "cJSON.h"
#include "esp_lvgl_port.h"

#include "lvgl.h"

LV_FONT_DECLARE(font_alipuhui20);

static lv_ui init_ui;
I2cMasterBus I2cbus(14,13,0);
CustomSDPort *sdcardPort = NULL;
Shtc3Port *shtc3port = NULL;
EventGroupHandle_t CodecGroups;
CodecPort *codecport = NULL;
static uint8_t *audio_ptr = NULL;
static bool is_Music = true;
static volatile bool g_next_song = false;   // 新增：下一首请求标志

static int parse_wav_header(FILE *fp, uint32_t *sample_rate, uint16_t *channels,
                            uint16_t *bits_per_sample, uint32_t *data_offset, uint32_t *data_size);

// 判断文件是否为 .wav 或 .WAV 后缀
static bool is_wav_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".wav") == 0);
}

// 递归扫描目录（仅一层子目录），收集所有 .wav 文件的绝对路径
static std::vector<std::string> scan_wav_files(const char *base_path) {
    std::vector<std::string> file_list;
    DIR *dir = opendir(base_path);
    if (!dir) {
        ESP_LOGE("Scan", "Failed to open directory: %s", base_path);
        return file_list;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        std::string full_path = std::string(base_path) + "/" + entry->d_name;

        if (entry->d_type == DT_REG && is_wav_file(entry->d_name)) {
            // 根目录下的 .wav 文件
            file_list.push_back(full_path);
        } 
        else if (entry->d_type == DT_DIR) {
            // 一级子目录：扫描其中的 .wav 文件
            DIR *subdir = opendir(full_path.c_str());
            if (subdir) {
                struct dirent *sub_entry;
                while ((sub_entry = readdir(subdir)) != NULL) {
                    if (strcmp(sub_entry->d_name, ".") == 0 || strcmp(sub_entry->d_name, "..") == 0) continue;
                    if (sub_entry->d_type == DT_REG && is_wav_file(sub_entry->d_name)) {
                        std::string sub_full_path = full_path + "/" + sub_entry->d_name;
                        file_list.push_back(sub_full_path);
                    }
                }
                closedir(subdir);
            } else {
                ESP_LOGW("Scan", "Cannot open subdir: %s", full_path.c_str());
            }
        }
    }
    closedir(dir);
    return file_list;
}

// 播放单个 WAV 文件（返回 true 表示正常结束，false 表示被中断）
static bool play_single_wav(const char *filepath) {
    ESP_LOGI("WAV", "Playing: %s", filepath);

    // 显示文件名（不含路径）
    const char *filename = strrchr(filepath, '/');
    if (filename) filename++; else filename = filepath;
    lv_label_set_text(init_ui.screen_label_5, filename);
    lv_label_set_text(init_ui.screen_label_6, "Playing...");

    FILE *wav_file = fopen(filepath, "rb");
    if (!wav_file) {
        ESP_LOGE("WAV", "Failed to open %s", filepath);
        lv_label_set_text(init_ui.screen_label_5, "文件打开失败");
        return false;
    }

    // 解析 WAV 头
    uint32_t sample_rate = 0, data_offset = 0, data_size = 0;
    uint16_t channels = 0, bits_per_sample = 0;
    if (parse_wav_header(wav_file, &sample_rate, &channels, &bits_per_sample, &data_offset, &data_size) != 0) {
        ESP_LOGE("WAV", "Failed to parse header: %s", filepath);
        lv_label_set_text(init_ui.screen_label_5, "解析失败");
        fclose(wav_file);
        return false;
    }

    ESP_LOGI("WAV", "  -> %u Hz, %u ch, %u bits, data size %u", sample_rate, channels, bits_per_sample, data_size);

    // 强制关闭播放和录音，避免采样率冲突
    codecport->CodecPort_CloseSpeaker();
    codecport->CodecPort_CloseRecord();   // 需要已添加该方法

    // 配置 Codec 为 WAV 文件的参数（只配置播放）
    codecport->CodecPort_SetInfo("es8311", 1, sample_rate, channels, bits_per_sample);
    codecport->CodecPort_SetSpeakerVol(80);

    // 定位到音频数据
    fseek(wav_file, data_offset, SEEK_SET);

    // 分配缓冲区
    uint8_t *buffer = (uint8_t *)malloc(4096);
    if (!buffer) {
        ESP_LOGE("WAV", "malloc failed");
        fclose(wav_file);
        return false;
    }

    size_t bytes_read, total_bytes = 0;
    bool interrupted = false;
    while ((bytes_read = fread(buffer, 1, 4096, wav_file)) > 0 && total_bytes < data_size) {
        codecport->CodecPort_PlayWrite(buffer, bytes_read);
        total_bytes += bytes_read;

        if (!is_Music) {               // 用户按了停止或下一首
            interrupted = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    free(buffer);
    fclose(wav_file);
    codecport->CodecPort_CloseSpeaker();

    // 恢复默认配置
    codecport->CodecPort_SetInfo("es8311 & es7210", 1, 16000, 2, 16);
    codecport->CodecPort_SetSpeakerVol(100);

    // 关键修改：判断是“下一首”还是“停止”
    if (interrupted) {
        if (g_next_song) {
            g_next_song = false;      // 清除请求
            lv_label_set_text(init_ui.screen_label_5, "下一首");
            return true;              // 返回 true，让播放列表继续下一个文件
        } else {
            lv_label_set_text(init_ui.screen_label_5, "播放中断");
            lv_label_set_text(init_ui.screen_label_6, "Stopped");
            return false;             // 用户真正停止，退出整个列表
        }
    } else {
        lv_label_set_text(init_ui.screen_label_5, "播放完成");
        lv_label_set_text(init_ui.screen_label_6, "Done");
        return true;
    }
}

// 播放所有扫描到的 WAV 文件（扫描根目录及一级子目录）
static void play_all_wav(void) {
    ESP_LOGI("Playlist", "Scanning /sdcard for .wav files...");
    std::vector<std::string> files = scan_wav_files("/sdcard");
    if (files.empty()) {
        ESP_LOGW("Playlist", "No .wav files found");
        lv_label_set_text(init_ui.screen_label_5, "无音频文件");
        lv_label_set_text(init_ui.screen_label_6, "No WAV");
        return;
    }

    ESP_LOGI("Playlist", "Found %d files", (int)files.size());
    lv_label_set_text(init_ui.screen_label_5, "开始播放列表");
    lv_label_set_text(init_ui.screen_label_6, "Playlist...");

    // 顺序播放，不排序
    for (size_t i = 0; i < files.size(); i++) {
        // 每次播放前重置中断标志（允许本次播放）
        is_Music = true;
        bool finished = play_single_wav(files[i].c_str());
        if (!finished) {
            // 用户中断，停止整个播放列表
            ESP_LOGI("Playlist", "Playlist interrupted by user");
            lv_label_set_text(init_ui.screen_label_5, "列表已停止");
            lv_label_set_text(init_ui.screen_label_6, "Stopped");
            return;
        }
        // 正常播完一个文件，继续下一个
    }

    ESP_LOGI("Playlist", "All files played");
    lv_label_set_text(init_ui.screen_label_5, "所有文件播放完成");
    lv_label_set_text(init_ui.screen_label_6, "Playlist Done");
}

void Lvgl_Cont1Task(void *arg) {
    lv_obj_clear_flag(init_ui.screen_label_1,LV_OBJ_FLAG_HIDDEN); 
    lv_obj_add_flag(init_ui.screen_label_2, LV_OBJ_FLAG_HIDDEN);
    vTaskDelay(pdMS_TO_TICKS(1500));
    lv_obj_clear_flag(init_ui.screen_label_2,LV_OBJ_FLAG_HIDDEN); 
    lv_obj_add_flag(init_ui.screen_label_1, LV_OBJ_FLAG_HIDDEN);
    vTaskDelay(pdMS_TO_TICKS(1500));
    lv_obj_clear_flag(init_ui.screen_cont_2,LV_OBJ_FLAG_HIDDEN); 
    lv_obj_add_flag(init_ui.screen_cont_1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(init_ui.screen_cont_3, LV_OBJ_FLAG_HIDDEN);
    vTaskDelete(NULL); 
}

void Lvgl_UserTask(void *arg) {
    uint32_t times = 0;
    uint32_t adc_time = 0;
    uint32_t rtc_time = 0;
    uint32_t shtc3_time = 0;
    char lvgl_buffer[30] = {""};
    for(;;) {
        if(times - adc_time == 10) {
            adc_time = times;
            uint8_t level = Adc_GetBatteryLevel();
            snprintf(lvgl_buffer,30,"%d%%",level);
            lv_label_set_text(init_ui.screen_label_7, lvgl_buffer);
        }
        if(times - rtc_time >= 5) {
            rtc_time = times;
            rtcTimeStruct_t timerData;
            Rtc_GetTime(&timerData);
            snprintf(lvgl_buffer,30,"%02d",timerData.hour);
            lv_label_set_text(init_ui.screen_label_3, lvgl_buffer);
            snprintf(lvgl_buffer,30,"%02d",timerData.minute);
            lv_label_set_text(init_ui.screen_label_4, lvgl_buffer);
            // ✅ 新增：显示年月日（格式：2026-03-28）
            snprintf(lvgl_buffer, 30, "%04d-%02d-%02d", 
                     timerData.year, timerData.month, timerData.day);
            lv_label_set_text(init_ui.screen_label_10, lvgl_buffer);
        }
        if(times - shtc3_time == 25)
        {
            shtc3_time = times;
            float rh,temp;
            shtc3port->Shtc3_ReadTempHumi(&temp,&rh);
            snprintf(lvgl_buffer,30,"%d%%",(int)rh);
            lv_label_set_text(init_ui.screen_label_11, lvgl_buffer);
            snprintf(lvgl_buffer,30,"%d°",(int)temp);
            lv_label_set_text(init_ui.screen_label_12, lvgl_buffer);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        times++;
    }
}

void Lvgl_SDcardTask(void *arg) {
    const char *str_write = "waveshare.com";
    char str_read[20] = {""};
    if(0 == sdcardPort->SDPort_GetStatus()) {
        lv_label_set_text(init_ui.screen_label_6, "No Card");
    } else {
        sdcardPort->SDPort_WriteFile("/sdcard/sdcard.txt",str_write,strlen(str_write));
        sdcardPort->SDPort_ReadFile("/sdcard/sdcard.txt",(uint8_t *)str_read,NULL);
        if(!strcmp(str_write,str_read)) {
            lv_label_set_text(init_ui.screen_label_6, "passed");
        } else {
            lv_label_set_text(init_ui.screen_label_6, "failed");
        }
    }
    vTaskDelete(NULL);
}

void Lvgl_WfifBleScanTask(void *srg) {
    char send_lvgl[10] = {""};
    uint8_t ble_scan_count = 0;
    uint8_t ble_mac[6];
    EventBits_t even = xEventGroupWaitBits(wifi_even_,0x02,pdTRUE,pdTRUE,pdMS_TO_TICKS(30000)); 
    //espwifi_deinit(); //释放WIFI
    ble_scan_prepare();
    ble_stack_init();
    ble_scan_start();
    for(;xQueueReceive(ble_queue,ble_mac,3500) == pdTRUE;) {
        ble_scan_count++;
        if(ble_scan_count >= 20)
        break;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    if(get_bit_data(even,1)) {
         //snprintf(send_lvgl,9,"%d",user_esp_bsp.apNum);
         //lv_label_set_text(init_ui.screen_label_14, send_lvgl);
        } else {
         //lv_label_set_text(init_ui.screen_label_14, "P");
        }
    snprintf(send_lvgl,10,"%d",ble_scan_count);
    lv_label_set_text(init_ui.screen_label_13, send_lvgl);
    ble_stack_deinit();    //释放BLE
    vTaskDelete(NULL);
}

void BOOT_LoopTask(void *arg) {
    bool is_cont4en = 0;
    for(;;) {
        EventBits_t even = xEventGroupWaitBits(BootButtonGroups,(0x01 | 0x02 | 0x04),pdTRUE,pdFALSE,pdMS_TO_TICKS(2000));
        if(even & 0x04) {
            if(0 == is_cont4en) {
                is_cont4en = 1;
                lv_obj_clear_flag(init_ui.screen_cont_4,LV_OBJ_FLAG_HIDDEN); 
                lv_obj_add_flag(init_ui.screen_cont_1, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_2, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_3, LV_OBJ_FLAG_HIDDEN);
            } else {
                is_cont4en = 0;
                lv_obj_clear_flag(init_ui.screen_cont_2,LV_OBJ_FLAG_HIDDEN); 
                lv_obj_add_flag(init_ui.screen_cont_1, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_4, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_3, LV_OBJ_FLAG_HIDDEN);
            }
        } else if(even & 0x01) {
            xEventGroupSetBits(CodecGroups,0x02);
        } else if(even & 0x02) {
            xEventGroupSetBits(CodecGroups,0x01);
        }
    }
}

void KEY_LoopTask(void *arg) {
        bool is_cont3en = 0;
        for(;;) {
            EventBits_t even = xEventGroupWaitBits(GP18ButtonGroups,(0x01 | 0x02 | 0x04),pdTRUE,pdFALSE,pdMS_TO_TICKS(2000));
            if(even & 0x01) {
                // 下一首：请求跳过当前歌曲
                g_next_song = true;
                is_Music = false;      // 立即中断当前播放
            } else if(even & 0x02) {
                // 原 bit1 功能不变（重新从头播放列表）
                is_Music = true;
                xEventGroupSetBits(CodecGroups,0x04);
            } else if(even & 0x04) {
            if(0 == is_cont3en) {
                is_cont3en = 1;
                lv_obj_clear_flag(init_ui.screen_cont_3,LV_OBJ_FLAG_HIDDEN); 
                lv_obj_add_flag(init_ui.screen_cont_1, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_2, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_4, LV_OBJ_FLAG_HIDDEN);
            } else {
                is_cont3en = 0;
                lv_obj_clear_flag(init_ui.screen_cont_2,LV_OBJ_FLAG_HIDDEN); 
                lv_obj_add_flag(init_ui.screen_cont_1, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_3, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(init_ui.screen_cont_4, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static int parse_wav_header(FILE *fp, uint32_t *sample_rate, uint16_t *channels,
                            uint16_t *bits_per_sample, uint32_t *data_offset, uint32_t *data_size) {
    uint8_t buf[44];
    uint32_t chunk_size, subchunk1_size, audio_format, byte_rate, block_align;
    uint16_t temp_channels, temp_bits;
    uint32_t temp_sample_rate;

    rewind(fp); // 回到文件开头

    // 读取 RIFF 头
    if (fread(buf, 1, 12, fp) != 12) return -1;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return -1;

    // 遍历所有 chunk
    while (1) {
        if (fread(buf, 1, 8, fp) != 8) return -1;
        chunk_size = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);

        if (memcmp(buf, "fmt ", 4) == 0) {
            // fmt chunk
            if (chunk_size < 16) return -1;
            if (fread(buf, 1, 16, fp) != 16) return -1;
            audio_format = buf[0] | (buf[1] << 8);
            temp_channels = buf[2] | (buf[3] << 8);
            temp_sample_rate = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);
            byte_rate = buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24);
            block_align = buf[12] | (buf[13] << 8);
            temp_bits = buf[14] | (buf[15] << 8);

            if (audio_format != 1) { // 仅支持 PCM
                ESP_LOGE("WAV", "Non-PCM format (0x%04x) not supported", audio_format);
                return -1;
            }
            *channels = temp_channels;
            *sample_rate = temp_sample_rate;
            *bits_per_sample = temp_bits;
            // 跳过可能的额外数据
            if (chunk_size > 16) fseek(fp, chunk_size - 16, SEEK_CUR);
        } else if (memcmp(buf, "data", 4) == 0) {
            // data chunk
            *data_offset = ftell(fp);
            *data_size = chunk_size;
            return 0;
        } else {
            // 其他 chunk，跳过
            fseek(fp, chunk_size, SEEK_CUR);
        }
    }
    return -1;
}

void Codec_LoopTask(void *arg) {
    bool is_eco = 0;
    for(;;) {
        EventBits_t even = xEventGroupWaitBits(CodecGroups, (0x01 | 0x02 | 0x04), pdTRUE, pdFALSE, pdMS_TO_TICKS(8 * 1000));
        
        if(even & 0x01) {
            // 录音
            lv_label_set_text(init_ui.screen_label_5, "正在录音");
            lv_label_set_text(init_ui.screen_label_6, "Recording...");
            codecport->CodecPort_EchoRead(audio_ptr, 192 * 1000);
            lv_label_set_text(init_ui.screen_label_5, "录音完成");
            lv_label_set_text(init_ui.screen_label_6, "Rec Done");
            is_eco = 1;
        }
        else if(even & 0x02) {
            // 播放录音
            if(1 == is_eco) {
                is_eco = 0;
                lv_label_set_text(init_ui.screen_label_5, "正在播放");
                lv_label_set_text(init_ui.screen_label_6, "Playing...");
                codecport->CodecPort_PlayWrite(audio_ptr, 192 * 1000);
                lv_label_set_text(init_ui.screen_label_5, "播放完成");
                lv_label_set_text(init_ui.screen_label_6, "Play Done");
            }
        }
        else if(even & 0x04) {
            // 播放 SD 卡中所有 .wav 文件（根目录及一级子目录）
            play_all_wav();
        }
    }
}

// 天气更新任务（每30分钟一次）
// 天气更新任务（每30分钟一次，带重试机制）
static void Weather_Task(void *arg) {
    char *weather_text = NULL;
    // 等待 WiFi 真正连接（给网络对时留出时间）
    vTaskDelay(pdMS_TO_TICKS(20000));

    // 初始显示等待信息
    lvgl_port_lock(portMAX_DELAY);
    lv_label_set_text(init_ui.screen_label_9, "Waiting...");
    lvgl_port_unlock();

    while (1) {
        esp_err_t ret = ESP_FAIL;
        int retry_count = 0;
        const int max_retries = 3;   // 最多重试3次
        
        // ---------- 重试循环 ----------
        while (retry_count < max_retries) {
            // 重试前等待（第一次不等待，第2次等2秒，第3次等4秒）
            int wait_ms = retry_count * 2000;
            if (wait_ms > 0) {
                ESP_LOGW("Weather", "Retry %d/%d after %d ms...", retry_count, max_retries, wait_ms);
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            }
            
            // 调用短文本天气接口
            ret = weather_get_current("Baoding", "5913d1cf6c7274cdfd6f90e8b748fca4", &weather_text);
            if (ret == ESP_OK && weather_text != NULL) {
                break;  // 成功，跳出重试循环
            }
            retry_count++;
        }
        
        // ---------- 处理结果并更新UI ----------
        lvgl_port_lock(portMAX_DELAY);
        if (ret == ESP_OK && weather_text != NULL) {
            ESP_LOGI("Weather", "Current weather: %s", weather_text);
            lv_label_set_text(init_ui.screen_label_9, weather_text);
            free(weather_text);
        } else {
            ESP_LOGE("Weather", "Failed to get weather after %d retries", max_retries);
            lv_label_set_text(init_ui.screen_label_9, "Weather N/A");
        }
        lvgl_port_unlock();
        
        // 每 30 分钟更新一次
        vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000));
    }
}

void UserApp_AppInit() {
    audio_ptr = (uint8_t *)heap_caps_malloc(288 * 1000 * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    assert(audio_ptr);
    sdcardPort = new CustomSDPort("/sdcard");
    Adc_PortInit();
    Custom_ButtonInit();
    Rtc_Setup(&I2cbus,0x51);
    //Rtc_SetTime(2026,1,5,14,30,30);
    shtc3port = new Shtc3Port(I2cbus);
    espwifi_init();
    CodecGroups = xEventGroupCreate();
    codecport = new CodecPort(I2cbus,"S3_RLCD_4_2");
    codecport->CodecPort_SetInfo("es8311 & es7210",1,16000,2,16);
    codecport->CodecPort_SetSpeakerVol(100);
    codecport->CodecPort_SetMicGain(35);

    // 配置 esp_lvgl_port
    lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 1,      // LVGL 任务优先级
        .task_stack = 4096,      // 任务栈大小（字节）
        .task_affinity = 1,      // 运行核心（0 或 1）
        .timer_period_ms = 10,   // LVGL 时钟周期（毫秒）
    };
    esp_err_t err = lvgl_port_init(&lvgl_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("UserApp", "lvgl_port_init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI("UserApp", "esp_lvgl_port initialized");

    vTaskDelay(pdMS_TO_TICKS(15000));
    //Rtc_SetTime(2026,1,5,14,30,30);
    if (sync_time_from_network(15000)) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // 设置到 RTC 芯片
        Rtc_SetTime(timeinfo.tm_year + 1900,
                    timeinfo.tm_mon + 1,
                    timeinfo.tm_mday,
                    timeinfo.tm_hour,
                    timeinfo.tm_min,
                    timeinfo.tm_sec);
        
        ESP_LOGI("UserApp", "✅ RTC updated from network: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        // 网络对时失败，使用默认时间
        Rtc_SetTime(2026, 3, 28, 14, 30, 30);
        ESP_LOGW("UserApp", "⚠️ Network time sync failed, using default time");
    }
}

void UserApp_UiInit() {
    setup_ui(&init_ui);
    lv_obj_set_style_text_font(init_ui.screen_label_5, &font_alipuhui20, 0);
    lv_obj_set_style_text_font(init_ui.screen_label_9, &font_alipuhui20, 0);
    lv_obj_set_style_text_font(init_ui.screen_label_10, &font_alipuhui20, 0);
    lv_label_set_text(init_ui.screen_label_8, "ON");
    lv_label_set_text(init_ui.screen_label_5, "中文测试");
}

void UserApp_TaskInit() {
    xTaskCreatePinnedToCore(Lvgl_Cont1Task, "Lvgl_Cont1Task", 4 * 1024, NULL, 2, NULL,1);
    xTaskCreatePinnedToCore(Lvgl_UserTask, "Lvgl_UserTask", 5 * 1024, NULL, 2, NULL,1);
    xTaskCreatePinnedToCore(Lvgl_SDcardTask, "Lvgl_SDcardTask", 4 * 1024, NULL, 2, NULL,1);
    //xTaskCreatePinnedToCore(Lvgl_WfifBleScanTask, "Lvgl_WfifBleScanTask", 4 * 1024, NULL, 2, NULL,1);
    xTaskCreatePinnedToCore(BOOT_LoopTask, "BOOT_LoopTask", 4 * 1024, NULL, 2, NULL,1);
    xTaskCreatePinnedToCore(KEY_LoopTask, "KEY_LoopTask", 4 * 1024, NULL, 2, NULL,1);
    xTaskCreatePinnedToCore(Codec_LoopTask, "Codec_LoopTask", 4 * 1024, NULL, 4, NULL,1);
    // 添加天气任务
    xTaskCreatePinnedToCore(Weather_Task, "Weather_Task", 6 * 1024, NULL, 2, NULL, 1);
}
