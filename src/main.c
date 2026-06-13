#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_a2dp_api.h"
#include "esp_a2dp_legacy_api.h"
#include "esp_avrc_api.h"
#include "esp_gap_bt_api.h"

#include "nvs_flash.h"

static const char *TAG = "A2DP_SRC";

// ============================================================
// TWS Bluetooth Address
// ============================================================
static uint8_t TWS_BDADDR[6] = {0xCF, 0xD6, 0x75, 0xA4, 0x81, 0x00};

// ============================================================
// State
// ============================================================
static EventGroupHandle_t s_bt_event_group;
#define A2DP_CONNECTED_BIT  BIT0

// ============================================================
// AUDIO DATA CALLBACK
// The stack calls this when it needs PCM data for SBC encoding.
// We fill the buffer with a 1000Hz square wave, stereo 44100Hz.
// ============================================================
static int32_t audio_data_callback(uint8_t *data, int32_t len)
{
    int16_t *samples = (int16_t *)data;
    int frames = len / 4;  // stereo = 2 channels * 2 bytes = 4 bytes/frame

    const int samples_per_cycle = 44100 / 1000;  // 1000Hz
    const int half_cycle = samples_per_cycle / 2;
    static int counter = 0;

    for (int i = 0; i < frames; i++) {
        int16_t sample = (counter < half_cycle) ? 8000 : -8000;

        samples[2 * i]     = sample;  // Left
        samples[2 * i + 1] = sample;  // Right

        counter++;
        if (counter >= samples_per_cycle)
            counter = 0;
    }

    return len;
}

// ============================================================
// A2DP Source callback
// ============================================================
static void a2d_source_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT: {
            esp_a2d_connection_state_t state = param->conn_stat.state;

            if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "A2DP CONNECTED!");
                xEventGroupSetBits(s_bt_event_group, A2DP_CONNECTED_BIT);
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);

            } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGW(TAG, "Disconnected!");
                xEventGroupClearBits(s_bt_event_group, A2DP_CONNECTED_BIT);
            }
            break;
        }

        case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
            ESP_LOGI(TAG, "Media Ctrl ACK: status=%d cmd=%d",
                     param->media_ctrl_stat.status, param->media_ctrl_stat.cmd);
            break;
        }

        case ESP_A2D_AUDIO_STATE_EVT: {
            ESP_LOGI(TAG, "Audio State: %d",
                     param->audio_stat.state);
            break;
        }

        case ESP_A2D_AUDIO_CFG_EVT: {
            ESP_LOGI(TAG, "Audio CFG: type=%d",
                     param->audio_cfg.mcc.type);
            break;
        }

        case ESP_A2D_PROF_STATE_EVT: {
            ESP_LOGI(TAG, "A2DP Prof State: %d",
                     param->a2d_prof_stat.init_state);
            break;
        }

        default:
            ESP_LOGD(TAG, "A2DP event: %d", event);
            break;
    }
}

// ============================================================
// GAP callback — scan for devices
// ============================================================
static esp_bd_addr_t s_found_addr;
static bool s_device_found = false;

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Check device name
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR) {
                    uint8_t *eir = param->disc_res.prop[i].val;
                    uint8_t len;
                    uint8_t *name = esp_bt_gap_resolve_eir_data(
                            eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);

                    if (name) {
                        ESP_LOGI(TAG, "Found: [%.*s] %02x:%02x:%02x:%02x:%02x:%02x",
                                 len, name,
                                 param->disc_res.bda[0], param->disc_res.bda[1],
                                 param->disc_res.bda[2], param->disc_res.bda[3],
                                 param->disc_res.bda[4], param->disc_res.bda[5]);
                    } else {
                        ESP_LOGI(TAG, "Found: %02x:%02x:%02x:%02x:%02x:%02x",
                                 param->disc_res.bda[0], param->disc_res.bda[1],
                                 param->disc_res.bda[2], param->disc_res.bda[3],
                                 param->disc_res.bda[4], param->disc_res.bda[5]);
                    }
                }
            }
            break;
        }

        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            ESP_LOGI(TAG, "Discovery state: %d", param->disc_st_chg.state);
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                if (s_device_found) {
                    ESP_LOGI(TAG, "Connecting...");
                    esp_a2d_source_connect(s_found_addr);
                } else {
                    ESP_LOGW(TAG, "No device found, restarting scan...");
                    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
                }
            }
            break;
        }

        default:
            break;
    }
}

// ============================================================
// Main
// ============================================================
void app_main(void)
{
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "  ESP32 A2DP Source -> TWS");
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "TWS MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             TWS_BDADDR[0], TWS_BDADDR[1], TWS_BDADDR[2],
             TWS_BDADDR[3], TWS_BDADDR[4], TWS_BDADDR[5]);

    s_bt_event_group = xEventGroupCreate();

    // --- NVS ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- BT Controller ---
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { ESP_LOGE(TAG, "BT ctrl init fail"); return; }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret) { ESP_LOGE(TAG, "BT ctrl enable fail: %s", esp_err_to_name(ret)); return; }

    // --- Bluedroid ---
    ret = esp_bluedroid_init();
    if (ret) { ESP_LOGE(TAG, "Bluedroid init fail"); return; }

    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(TAG, "Bluedroid enable fail"); return; }

    ESP_LOGI(TAG, "Bluetooth initialized!");

    // --- GAP ---
    esp_bt_gap_set_device_name("ESP32-TWS-Player");
    esp_bt_gap_register_callback(bt_gap_cb);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // --- A2DP Source ---
    esp_a2d_register_callback(a2d_source_cb);
    esp_a2d_source_register_data_callback(audio_data_callback);
    esp_a2d_source_init();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Turn ON your TWS earbuds NOW!");
    ESP_LOGI(TAG, "  ESP32 will scan & connect...");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // --- Scan for devices ---
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);

    // --- Reconnect loop ---
    while (1) {
        if (!(xEventGroupGetBits(s_bt_event_group) & A2DP_CONNECTED_BIT)) {
            ESP_LOGI(TAG, "Reconnecting...");
            esp_a2d_source_connect(TWS_BDADDR);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
