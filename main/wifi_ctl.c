/**
 * WiFi Control Module Implementation
 * Uses esp_wifi for scanning, promiscuous sniffing, and raw frame TX
 */
#include "wifi_ctl.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"

static const char* TAG = "wifi_ctl";

// ===== Bypass sanity check for raw frame injection ===== //
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}

// ===== Internal state ===== //
static ap_info_t      s_aps[MAX_APS];
static int            s_ap_count = 0;
static station_info_t s_stations[MAX_STATIONS];
static int            s_station_count = 0;
static ssid_entry_t   s_ssids[MAX_SSIDS];
static int            s_ssid_count = 0;
static uint8_t        s_channel = 1;
static bool           s_scanning = false;
static EventGroupHandle_t s_wifi_events;
#define SCAN_DONE_BIT BIT0

// ===== Event handler ===== //
static void wifi_event_handler(void* arg, esp_event_base_t base,
                               int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(s_wifi_events, SCAN_DONE_BIT);
    }
}

// ===== Promiscuous sniffer callback ===== //
static void sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* frame = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;

    uint8_t frame_type = frame[0];
    // We're interested in data frames and probe/assoc to find stations
    // Typical mgmt: 0x40=probe_req, 0x50=probe_resp, 0x80=beacon, 0xC0=deauth

    // Extract source MAC (bytes 10-15) and BSSID (bytes 16-21)
    const uint8_t* src_mac = &frame[10];
    const uint8_t* bssid   = &frame[16];

    // Skip broadcast/multicast source MACs
    if (src_mac[0] & 0x01) return;
    // Skip null MACs
    bool all_zero = true;
    for (int i = 0; i < 6; i++) { if (src_mac[i] != 0) { all_zero = false; break; } }
    if (all_zero) return;

    // Check if it's a known AP BSSID — if so, the source is a station
    bool bssid_is_ap = false;
    int ap_idx = -1;
    for (int i = 0; i < s_ap_count; i++) {
        if (memcmp(s_aps[i].bssid, bssid, 6) == 0) {
            bssid_is_ap = true;
            ap_idx = i;
            break;
        }
    }

    // If source is NOT a known AP bssid, it might be a station
    bool src_is_ap = false;
    for (int i = 0; i < s_ap_count; i++) {
        if (memcmp(s_aps[i].bssid, src_mac, 6) == 0) {
            src_is_ap = true;
            break;
        }
    }

    if (!src_is_ap && bssid_is_ap) {
        // src_mac is a station associated with bssid
        // Check if we already have it
        for (int i = 0; i < s_station_count; i++) {
            if (memcmp(s_stations[i].mac, src_mac, 6) == 0) {
                s_stations[i].rssi = pkt->rx_ctrl.rssi;
                s_stations[i].last_seen = xTaskGetTickCount() * portTICK_PERIOD_MS;
                memcpy(s_stations[i].ap_bssid, bssid, 6);
                s_stations[i].channel = pkt->rx_ctrl.channel;
                return;
            }
        }
        // New station
        if (s_station_count < MAX_STATIONS) {
            station_info_t* st = &s_stations[s_station_count];
            memcpy(st->mac, src_mac, 6);
            memcpy(st->ap_bssid, bssid, 6);
            st->rssi = pkt->rx_ctrl.rssi;
            st->channel = pkt->rx_ctrl.channel;
            st->last_seen = xTaskGetTickCount() * portTICK_PERIOD_MS;
            st->selected = false;
            s_station_count++;
        }
    }
}

// ===== Init ===== //
void wifi_ctl_init(void) {
    s_wifi_events = xEventGroupCreate();

    // Init NVS (needed by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE));

    // Max TX power
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84)); // 21 dBm

    ESP_LOGI(TAG, "WiFi initialized for ESP32-C3 deauther");
}

// ===== Scanning ===== //
void wifi_start_scan(void) {
    s_scanning = true;
    s_ap_count = 0;
    s_station_count = 0;

    // Stop sniffer if running
    esp_wifi_set_promiscuous(false);

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,  // all channels
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    xEventGroupClearBits(s_wifi_events, SCAN_DONE_BIT);
    esp_wifi_scan_start(&scan_cfg, false);

    // Wait for scan to complete (max 15s)
    xEventGroupWaitBits(s_wifi_events, SCAN_DONE_BIT, true, true, pdMS_TO_TICKS(15000));

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > MAX_APS) ap_num = MAX_APS;

    wifi_ap_record_t* records = malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (records) {
        esp_wifi_scan_get_ap_records(&ap_num, records);
        for (int i = 0; i < ap_num; i++) {
            memcpy(s_aps[i].bssid, records[i].bssid, 6);
            strncpy(s_aps[i].ssid, (char*)records[i].ssid, 32);
            s_aps[i].ssid[32] = '\0';
            s_aps[i].rssi = records[i].rssi;
            s_aps[i].channel = records[i].primary;
            s_aps[i].auth_mode = (records[i].authmode != WIFI_AUTH_OPEN) ? 1 : 0;
            s_aps[i].selected = false;
        }
        s_ap_count = ap_num;
        free(records);
    }

    s_scanning = false;
    ESP_LOGI(TAG, "Scan complete: %d APs found", s_ap_count);
}

bool wifi_is_scanning(void) { return s_scanning; }
int  wifi_get_ap_count(void) { return s_ap_count; }
ap_info_t* wifi_get_ap(int index) {
    if (index < 0 || index >= s_ap_count) return NULL;
    return &s_aps[index];
}

// ===== Sniffer ===== //
void wifi_start_sniffer(void) {
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(sniffer_cb);
    esp_wifi_set_promiscuous(true);
    ESP_LOGI(TAG, "Sniffer started");
}

void wifi_stop_sniffer(void) {
    esp_wifi_set_promiscuous(false);
    ESP_LOGI(TAG, "Sniffer stopped");
}

int wifi_get_station_count(void) { return s_station_count; }
station_info_t* wifi_get_station(int index) {
    if (index < 0 || index >= s_station_count) return NULL;
    return &s_stations[index];
}

// ===== Channel ===== //
void wifi_set_channel(uint8_t ch) {
    if (ch < 1 || ch > WIFI_CHANNEL_MAX) return;
    s_channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

uint8_t wifi_get_channel(void) { return s_channel; }

// ===== Packet TX ===== //
bool wifi_send_raw(const uint8_t* packet, uint16_t len) {
    esp_err_t ret = esp_wifi_80211_tx(WIFI_IF_STA, packet, len, false);
    return ret == ESP_OK;
}

// ===== Selection helpers ===== //
void wifi_select_all_aps(void) {
    for (int i = 0; i < s_ap_count; i++) s_aps[i].selected = true;
}
void wifi_deselect_all_aps(void) {
    for (int i = 0; i < s_ap_count; i++) s_aps[i].selected = false;
}
void wifi_toggle_ap(int index) {
    if (index >= 0 && index < s_ap_count) s_aps[index].selected = !s_aps[index].selected;
}
void wifi_select_all_stations(void) {
    for (int i = 0; i < s_station_count; i++) s_stations[i].selected = true;
}
void wifi_deselect_all_stations(void) {
    for (int i = 0; i < s_station_count; i++) s_stations[i].selected = false;
}
void wifi_toggle_station(int index) {
    if (index >= 0 && index < s_station_count) s_stations[index].selected = !s_stations[index].selected;
}
int wifi_count_selected_aps(void) {
    int c = 0;
    for (int i = 0; i < s_ap_count; i++) if (s_aps[i].selected) c++;
    return c;
}
int wifi_count_selected_stations(void) {
    int c = 0;
    for (int i = 0; i < s_station_count; i++) if (s_stations[i].selected) c++;
    return c;
}

// ===== SSID list ===== //
void ssid_list_clear(void) { s_ssid_count = 0; }
bool ssid_list_add(const char* ssid, bool wpa2) {
    if (s_ssid_count >= MAX_SSIDS) return false;
    strncpy(s_ssids[s_ssid_count].ssid, ssid, 32);
    s_ssids[s_ssid_count].ssid[32] = '\0';
    s_ssids[s_ssid_count].wpa2 = wpa2;
    s_ssid_count++;
    return true;
}
int ssid_list_count(void) { return s_ssid_count; }
ssid_entry_t* ssid_list_get(int index) {
    if (index < 0 || index >= s_ssid_count) return NULL;
    return &s_ssids[index];
}
void ssid_list_remove(int index) {
    if (index < 0 || index >= s_ssid_count) return;
    for (int i = index; i < s_ssid_count - 1; i++) {
        s_ssids[i] = s_ssids[i + 1];
    }
    s_ssid_count--;
}

// ===== MAC helpers ===== //
void mac_to_str(const uint8_t* mac, char* buf, int buf_size) {
    snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void random_mac(uint8_t* mac) {
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    mac[0] = (r1 >> 0)  & 0xFE;  // clear multicast bit
    mac[1] = (r1 >> 8)  & 0xFF;
    mac[2] = (r1 >> 16) & 0xFF;
    mac[3] = (r2 >> 0)  & 0xFF;
    mac[4] = (r2 >> 8)  & 0xFF;
    mac[5] = (r2 >> 16) & 0xFF;
}
