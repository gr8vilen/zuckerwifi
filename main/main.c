/**
 * ESP32-C3 WiFi Deauther - Main Entry Point
 * 
 * Features:
 *   - WiFi AP scanning (active scan)
 *   - Station discovery (promiscuous sniffer)
 *   - Deauthentication attack
 *   - Beacon spam attack
 *   - Probe request flood
 *   - SSD1306 OLED menu UI
 *   - 3-button navigation (UP/DOWN/SELECT)
 *   - Serial CLI
 *
 * Hardware: ESP32-C3 SuperMini
 *   Buttons: GPIO 0/1/2
 *   I2C:     GPIO 6 (SDA), GPIO 7 (SCL)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "config.h"
#include "wifi_ctl.h"
#include "attack.h"
#include "display_ui.h"
static const char* TAG = "main";

// ===== Serial CLI (basic) ===== //
static void cli_task(void* arg) {
    char line[128];
    int pos = 0;

    printf("\n============================\n");
    printf("  ESP32-C3 WiFi Deauther\n");
    printf("  Version %s\n", DEAUTHER_VERSION);
    printf("============================\n");
    printf("Commands: scan, deauth, beacon, probe, stop, aps, sts, ssid add <name>, ssid clear, help\n\n");
    printf("> ");

    while (1) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (c == '\n' || c == '\r') {
            line[pos] = '\0';
            if (pos > 0) {
                printf("\n");

                // Parse command
                if (strcmp(line, "scan") == 0) {
                    printf("Scanning...\n");
                    wifi_start_scan();
                    printf("Found %d APs\n", wifi_get_ap_count());
                    // Auto-start sniffer for stations
                    wifi_start_sniffer();
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    wifi_stop_sniffer();
                    printf("Found %d stations\n", wifi_get_station_count());

                } else if (strcmp(line, "aps") == 0) {
                    int count = wifi_get_ap_count();
                    printf("APs (%d):\n", count);
                    for (int i = 0; i < count; i++) {
                        ap_info_t* ap = wifi_get_ap(i);
                        char mac[18];
                        mac_to_str(ap->bssid, mac, sizeof(mac));
                        printf("  %d) %c %-32s %s CH:%d RSSI:%d\n",
                               i, ap->selected ? '*' : ' ',
                               ap->ssid[0] ? ap->ssid : "(hidden)",
                               mac, ap->channel, ap->rssi);
                    }

                } else if (strcmp(line, "sts") == 0) {
                    int count = wifi_get_station_count();
                    printf("Stations (%d):\n", count);
                    for (int i = 0; i < count; i++) {
                        station_info_t* st = wifi_get_station(i);
                        char mac[18], ap_mac[18];
                        mac_to_str(st->mac, mac, sizeof(mac));
                        mac_to_str(st->ap_bssid, ap_mac, sizeof(ap_mac));
                        printf("  %d) %c %s -> %s CH:%d RSSI:%d\n",
                               i, st->selected ? '*' : ' ',
                               mac, ap_mac, st->channel, st->rssi);
                    }

                } else if (strncmp(line, "select ", 7) == 0) {
                    int idx = atoi(line + 7);
                    wifi_toggle_ap(idx);
                    printf("Toggled AP %d\n", idx);

                } else if (strcmp(line, "selall") == 0) {
                    wifi_select_all_aps();
                    wifi_select_all_stations();
                    printf("Selected all\n");

                } else if (strcmp(line, "deselall") == 0) {
                    wifi_deselect_all_aps();
                    wifi_deselect_all_stations();
                    printf("Deselected all\n");

                } else if (strcmp(line, "deauth") == 0) {
                    attack_start(ATTACK_DEAUTH);
                    printf("Deauth attack started\n");

                } else if (strcmp(line, "beacon") == 0) {
                    if (ssid_list_count() == 0) {
                        printf("No SSIDs! Use 'ssid add <name>' first\n");
                    } else {
                        attack_start(ATTACK_BEACON);
                        printf("Beacon attack started (%d SSIDs)\n", ssid_list_count());
                    }

                } else if (strcmp(line, "probe") == 0) {
                    if (ssid_list_count() == 0) {
                        printf("No SSIDs! Use 'ssid add <name>' first\n");
                    } else {
                        attack_start(ATTACK_PROBE);
                        printf("Probe attack started\n");
                    }

                } else if (strcmp(line, "stop") == 0) {
                    attack_stop();
                    printf("Attack stopped\n");

                } else if (strncmp(line, "ssid add ", 9) == 0) {
                    const char* name = line + 9;
                    if (ssid_list_add(name, true)) {
                        printf("Added SSID: %s (total: %d)\n", name, ssid_list_count());
                    } else {
                        printf("SSID list full\n");
                    }

                } else if (strcmp(line, "ssid clear") == 0) {
                    ssid_list_clear();
                    printf("SSIDs cleared\n");

                } else if (strcmp(line, "ssid list") == 0) {
                    int count = ssid_list_count();
                    printf("SSIDs (%d):\n", count);
                    for (int i = 0; i < count; i++) {
                        ssid_entry_t* e = ssid_list_get(i);
                        printf("  %d) %s [%s]\n", i, e->ssid, e->wpa2 ? "WPA2" : "OPEN");
                    }

                } else if (strcmp(line, "status") == 0) {
                    attack_status_t st = attack_get_status();
                    printf("Running: %s\n", st.running ? "YES" : "NO");
                    printf("PPS: %lu\n", (unsigned long)st.packets_per_sec);
                    printf("Deauth: %lu  Beacon: %lu  Probe: %lu\n",
                           (unsigned long)st.deauth_sent,
                           (unsigned long)st.beacon_sent,
                           (unsigned long)st.probe_sent);

                } else if (strcmp(line, "help") == 0) {
                    printf("Commands:\n");
                    printf("  scan          - Scan for APs and stations\n");
                    printf("  aps           - List access points\n");
                    printf("  sts           - List stations\n");
                    printf("  select <n>    - Toggle AP selection\n");
                    printf("  selall        - Select all\n");
                    printf("  deselall      - Deselect all\n");
                    printf("  deauth        - Start deauth attack\n");
                    printf("  beacon        - Start beacon spam\n");
                    printf("  probe         - Start probe flood\n");
                    printf("  stop          - Stop attacks\n");
                    printf("  ssid add <n>  - Add SSID\n");
                    printf("  ssid clear    - Clear SSIDs\n");
                    printf("  ssid list     - List SSIDs\n");
                    printf("  status        - Show attack status\n");

                } else {
                    printf("Unknown command: %s (type 'help')\n", line);
                }
            }
            pos = 0;
            printf("> ");
        } else if (pos < (int)(sizeof(line) - 1)) {
            line[pos++] = (char)c;
            printf("%c", c);
        }
    }
}

// ===== Main loop task ===== //
static void main_loop_task(void* arg) {
    while (1) {
        // Update display (handles button input + rendering)
        display_update();

        // Update attacks
        attack_update();

        // Yield
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ===== Entry point ===== //
void app_main(void) {
    ESP_LOGI(TAG, "ESP32-C3 WiFi Deauther %s starting...", DEAUTHER_VERSION);

    // Init modules
    wifi_ctl_init();
    attack_init();
    display_init();
    ESP_LOGI(TAG, "All modules initialized");

    // Start main loop task (display + attack updates)
    xTaskCreate(main_loop_task, "main_loop", 8192, NULL, 5, NULL);

    // Start CLI task
    xTaskCreate(cli_task, "cli", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Tasks started, system ready");
}
