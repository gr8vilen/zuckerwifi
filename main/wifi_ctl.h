/**
 * WiFi Control Module - Scanning, Promiscuous Mode, Packet TX
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

// ===== AP info from scan ===== //
typedef struct {
    uint8_t  bssid[6];
    char     ssid[33];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  auth_mode;  // 0=open, else secured
    bool     selected;
} ap_info_t;

// ===== Station info from sniffer ===== //
typedef struct {
    uint8_t  mac[6];
    uint8_t  ap_bssid[6]; // associated AP
    int8_t   rssi;
    uint8_t  channel;
    uint32_t last_seen;   // millis timestamp
    bool     selected;
} station_info_t;

// ===== SSID list for beacon/probe ===== //
typedef struct {
    char     ssid[33];
    bool     wpa2;
} ssid_entry_t;

// ===== Init / Deinit ===== //
void wifi_ctl_init(void);

// ===== Scanning ===== //
void wifi_start_scan(void);
bool wifi_is_scanning(void);
int  wifi_get_ap_count(void);
ap_info_t* wifi_get_ap(int index);

// ===== Sniffer (promiscuous) ===== //
void wifi_start_sniffer(void);
void wifi_stop_sniffer(void);
int  wifi_get_station_count(void);
station_info_t* wifi_get_station(int index);

// ===== Channel ===== //
void wifi_set_channel(uint8_t ch);
uint8_t wifi_get_channel(void);

// ===== Packet TX ===== //
bool wifi_send_raw(const uint8_t* packet, uint16_t len);

// ===== Selection helpers ===== //
void wifi_select_all_aps(void);
void wifi_deselect_all_aps(void);
void wifi_toggle_ap(int index);
void wifi_select_all_stations(void);
void wifi_deselect_all_stations(void);
void wifi_toggle_station(int index);
int  wifi_count_selected_aps(void);
int  wifi_count_selected_stations(void);

// ===== SSID list ===== //
void     ssid_list_clear(void);
bool     ssid_list_add(const char* ssid, bool wpa2);
int      ssid_list_count(void);
ssid_entry_t* ssid_list_get(int index);
void     ssid_list_remove(int index);

// ===== MAC helpers ===== //
void mac_to_str(const uint8_t* mac, char* buf, int buf_size);
void random_mac(uint8_t* mac);
