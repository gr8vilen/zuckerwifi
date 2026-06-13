/**
 * Display UI Implementation
 * SSD1306 128x64 OLED with full menu system
 * 3-button navigation: UP, DOWN, SELECT
 * 
 * Uses the espressif/ssd1306 component (legacy I2C API)
 */
#include "display_ui.h"
#include "config.h"
#include "wifi_ctl.h"
#include "attack.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ssd1306.h"

static const char* TAG = "display";

// ===== Display state ===== //
static ssd1306_handle_t s_oled = NULL;
static ui_screen_t      s_screen = UI_SCREEN_INTRO;
static int              s_cursor = 0;       // menu cursor position
static int              s_scroll = 0;       // scroll offset for long lists
static int64_t          s_intro_time = 0;
static int64_t          s_last_draw = 0;
static int              s_detail_idx = 0;   // selected AP/station index for detail view

// Button state
static bool s_btn_up = false;
static bool s_btn_down = false;
static bool s_btn_sel = false;
static int64_t s_btn_up_time = 0;
static int64_t s_btn_down_time = 0;
static int64_t s_btn_sel_time = 0;

#define MAX_VISIBLE_LINES 4  // lines on screen for menu items (64-14 header)/12 = ~4
#define FONT_HEIGHT       12
#define HEADER_HEIGHT     14
#define FONT_SIZE         12  // use 12px font (the component supports 12 and 16)

// ===== Button reading ===== //
static bool read_button(gpio_num_t pin) {
    return gpio_get_level(pin) == 0;  // active low with pull-up
}

static bool button_pressed(gpio_num_t pin, bool* last_state, int64_t* last_time) {
    bool current = read_button(pin);
    int64_t now = esp_timer_get_time();

    if (current && !(*last_state) && (now - *last_time > BUTTON_DEBOUNCE_MS * 1000LL)) {
        *last_state = true;
        *last_time = now;
        return true;
    }
    if (!current) {
        *last_state = false;
    }
    // Auto-repeat
    if (current && *last_state && (now - *last_time > BUTTON_REPEAT_MS * 1000LL)) {
        *last_time = now;
        return true;
    }
    return false;
}

// ===== Drawing helpers ===== //
// Wrapper to cast const char* to const uint8_t* for the ssd1306 API
static void oled_draw_str(uint8_t x, uint8_t y, const char* text, uint8_t mode) {
    ssd1306_draw_string(s_oled, x, y, (const uint8_t*)text, FONT_SIZE, mode);
}

static void draw_header(const char* title) {
    oled_draw_str(0, 0, title, 1);
    ssd1306_draw_line(s_oled, 0, HEADER_HEIGHT - 1, OLED_WIDTH - 1, HEADER_HEIGHT - 1);
}

static void draw_menu_item(int row, const char* text, bool selected) {
    int y = HEADER_HEIGHT + row * FONT_HEIGHT;
    if (selected) {
        // Draw filled rectangle for highlight
        ssd1306_fill_rectangle(s_oled, 0, y, OLED_WIDTH - 1, y + FONT_HEIGHT - 1, 1);
        oled_draw_str(2, y + 1, text, 0); // inverted text
    } else {
        oled_draw_str(2, y + 1, text, 1);
    }
}

static void draw_status_bar(const char* text) {
    int y = OLED_HEIGHT - FONT_HEIGHT;
    ssd1306_draw_line(s_oled, 0, y - 1, OLED_WIDTH - 1, y - 1);
    oled_draw_str(2, y, text, 1);
}

// ===== Menu item definitions ===== //
static const char* main_menu_items[] = {
    "> Scan",
    "> SSIDs",
    "> Attack",
    "> Info",
};
#define MAIN_MENU_COUNT 4

static const char* scan_menu_items[] = {
    "> Scan APs",
    "> Scan APs+STs",
    "> AP List",
    "> Station List",
    "> Select All",
    "> Deselect All",
    "< Back",
};
#define SCAN_MENU_COUNT 7

// Track attack type selections
static bool s_atk_deauth = false;
static bool s_atk_beacon = false;
static bool s_atk_probe  = false;

static const char* ssid_menu_items[] = {
    "> Add Random",
    "> Add Clone APs",
    "> Clear All",
    "< Back",
};
#define SSID_MENU_COUNT 4

#define ATTACK_MENU_COUNT 5

// ===== Screen drawing functions ===== //

static void draw_intro(void) {
    ssd1306_draw_string(s_oled, 10, 5,  (const uint8_t*)"ESP32-C3", 16, 1);
    oled_draw_str(10, 25, "WiFi Deauther", 1);
    oled_draw_str(10, 40, DEAUTHER_VERSION, 1);
    oled_draw_str(10, 52, "Starting...", 1);
}

static void draw_main_menu(void) {
    draw_header("C3 Deauther");
    int count = MAIN_MENU_COUNT < MAX_VISIBLE_LINES ? MAIN_MENU_COUNT : MAX_VISIBLE_LINES;
    for (int i = 0; i < count; i++) {
        draw_menu_item(i, main_menu_items[i], i == s_cursor);
    }
}

static void draw_scan_menu(void) {
    draw_header("Scan");
    int visible = SCAN_MENU_COUNT < MAX_VISIBLE_LINES ? SCAN_MENU_COUNT : MAX_VISIBLE_LINES;
    for (int i = 0; i < visible; i++) {
        int idx = i + s_scroll;
        if (idx >= SCAN_MENU_COUNT) break;
        draw_menu_item(i, scan_menu_items[idx], idx == s_cursor);
    }
}

static void draw_scanning(void) {
    draw_header("Scanning...");
    char buf[32];
    snprintf(buf, sizeof(buf), "APs: %d", wifi_get_ap_count());
    oled_draw_str(10, 20, buf, 1);
    snprintf(buf, sizeof(buf), "Stations: %d", wifi_get_station_count());
    oled_draw_str(10, 34, buf, 1);
    oled_draw_str(10, 50, "Please wait...", 1);
}

static void draw_ap_list(void) {
    draw_header("AP List");
    int ap_count = wifi_get_ap_count();
    int total = ap_count + 1;  // +1 for "< Back" at index 0

    int visible = total < MAX_VISIBLE_LINES ? total : MAX_VISIBLE_LINES;
    for (int i = 0; i < visible; i++) {
        int idx = i + s_scroll;
        if (idx >= total) break;

        if (idx == 0) {
            draw_menu_item(i, "< Back", idx == s_cursor);
        } else {
            ap_info_t* ap = wifi_get_ap(idx - 1);
            if (!ap) continue;

            char line[24];
            snprintf(line, sizeof(line), "%c%.14s %d",
                     ap->selected ? '*' : ' ',
                     ap->ssid[0] ? ap->ssid : "(hidden)",
                     ap->rssi);
            draw_menu_item(i, line, idx == s_cursor);
        }
    }

    if (ap_count > 0) {
        char status[40];
        snprintf(status, sizeof(status), "%d/%d sel:%d", s_cursor > 0 ? s_cursor : 1, ap_count, wifi_count_selected_aps());
        draw_status_bar(status);
    }
}

static void draw_ap_detail(void) {
    ap_info_t* ap = wifi_get_ap(s_detail_idx);
    if (!ap) { s_screen = UI_SCREEN_AP_LIST; return; }

    draw_header("AP Detail");
    char buf[32];

    snprintf(buf, sizeof(buf), "%.18s", ap->ssid[0] ? ap->ssid : "(hidden)");
    oled_draw_str(0, HEADER_HEIGHT, buf, 1);

    char mac_str[18];
    mac_to_str(ap->bssid, mac_str, sizeof(mac_str));
    oled_draw_str(0, HEADER_HEIGHT + 12, mac_str, 1);

    snprintf(buf, sizeof(buf), "CH:%d RSSI:%d", ap->channel, ap->rssi);
    oled_draw_str(0, HEADER_HEIGHT + 24, buf, 1);

    snprintf(buf, sizeof(buf), "%s", ap->auth_mode ? "WPA/WPA2" : "OPEN");
    oled_draw_str(0, HEADER_HEIGHT + 36, buf, 1);
}

static void draw_station_list(void) {
    draw_header("Stations");
    int st_count = wifi_get_station_count();
    int total = st_count + 1;  // +1 for "< Back" at index 0

    int visible = total < MAX_VISIBLE_LINES ? total : MAX_VISIBLE_LINES;
    for (int i = 0; i < visible; i++) {
        int idx = i + s_scroll;
        if (idx >= total) break;

        if (idx == 0) {
            draw_menu_item(i, "< Back", idx == s_cursor);
        } else {
            station_info_t* st = wifi_get_station(idx - 1);
            if (!st) continue;

            char line[24];
            snprintf(line, sizeof(line), "%c%02X:%02X:%02X:%02X %d",
                     st->selected ? '*' : ' ',
                     st->mac[2], st->mac[3], st->mac[4], st->mac[5],
                     st->rssi);
            draw_menu_item(i, line, idx == s_cursor);
        }
    }

    if (st_count > 0) {
        char status[40];
        snprintf(status, sizeof(status), "%d/%d sel:%d", s_cursor > 0 ? s_cursor : 1, st_count, wifi_count_selected_stations());
        draw_status_bar(status);
    }
}

static void draw_attack_menu(void) {
    draw_header("Attack");

    char items[ATTACK_MENU_COUNT][24];
    snprintf(items[0], sizeof(items[0]), "[%c] Deauth", s_atk_deauth ? 'X' : ' ');
    snprintf(items[1], sizeof(items[1]), "[%c] Beacon", s_atk_beacon ? 'X' : ' ');
    snprintf(items[2], sizeof(items[2]), "[%c] Probe",  s_atk_probe  ? 'X' : ' ');
    snprintf(items[3], sizeof(items[3]), "> START");
    snprintf(items[4], sizeof(items[4]), "< Back");

    int visible = ATTACK_MENU_COUNT < MAX_VISIBLE_LINES ? ATTACK_MENU_COUNT : MAX_VISIBLE_LINES;
    for (int i = 0; i < visible; i++) {
        int idx = i + s_scroll;
        if (idx >= ATTACK_MENU_COUNT) break;
        draw_menu_item(i, items[idx], idx == s_cursor);
    }
}

static void draw_attack_running(void) {
    draw_header("ATTACKING!");
    attack_status_t st = attack_get_status();
    char buf[32];

    snprintf(buf, sizeof(buf), "PPS: %lu", (unsigned long)st.packets_per_sec);
    oled_draw_str(0, HEADER_HEIGHT, buf, 1);

    snprintf(buf, sizeof(buf), "Deauth: %lu", (unsigned long)st.deauth_sent);
    oled_draw_str(0, HEADER_HEIGHT + 12, buf, 1);

    snprintf(buf, sizeof(buf), "Beacon: %lu", (unsigned long)st.beacon_sent);
    oled_draw_str(0, HEADER_HEIGHT + 24, buf, 1);

    snprintf(buf, sizeof(buf), "Probe:  %lu", (unsigned long)st.probe_sent);
    oled_draw_str(0, HEADER_HEIGHT + 36, buf, 1);

    draw_status_bar("SEL=Stop");
}

static void draw_ssid_menu(void) {
    char header[24];
    snprintf(header, sizeof(header), "SSIDs (%d)", ssid_list_count());
    draw_header(header);

    for (int i = 0; i < SSID_MENU_COUNT && i < MAX_VISIBLE_LINES; i++) {
        draw_menu_item(i, ssid_menu_items[i], i == s_cursor);
    }
}

static void draw_info(void) {
    draw_header("Info");
    char buf[32];

    snprintf(buf, sizeof(buf), "Ver: %s", DEAUTHER_VERSION);
    oled_draw_str(0, HEADER_HEIGHT, buf, 1);

    snprintf(buf, sizeof(buf), "APs: %d  STs: %d", wifi_get_ap_count(), wifi_get_station_count());
    oled_draw_str(0, HEADER_HEIGHT + 12, buf, 1);

    snprintf(buf, sizeof(buf), "SSIDs: %d", ssid_list_count());
    oled_draw_str(0, HEADER_HEIGHT + 24, buf, 1);

    snprintf(buf, sizeof(buf), "CH: %d", wifi_get_channel());
    oled_draw_str(0, HEADER_HEIGHT + 36, buf, 1);

    draw_status_bar("SEL=Back");
}

// ===== Input handling per screen ===== //

static void handle_input_main(bool up, bool down, bool sel) {
    if (up && s_cursor > 0) s_cursor--;
    if (down && s_cursor < MAIN_MENU_COUNT - 1) s_cursor++;
    if (sel) {
        switch (s_cursor) {
            case 0: s_screen = UI_SCREEN_SCAN_MENU; s_cursor = 0; s_scroll = 0; break;
            case 1: s_screen = UI_SCREEN_SSID_MENU; s_cursor = 0; break;
            case 2: s_screen = UI_SCREEN_ATTACK_MENU; s_cursor = 0; s_scroll = 0; break;
            case 3: s_screen = UI_SCREEN_INFO; s_cursor = 0; break;
        }
    }
}

static void handle_input_scan(bool up, bool down, bool sel) {
    if (up && s_cursor > 0) s_cursor--;
    if (down && s_cursor < SCAN_MENU_COUNT - 1) s_cursor++;

    if (s_cursor < s_scroll) s_scroll = s_cursor;
    if (s_cursor >= s_scroll + MAX_VISIBLE_LINES) s_scroll = s_cursor - MAX_VISIBLE_LINES + 1;

    if (sel) {
        switch (s_cursor) {
            case 0: {  // Scan APs
                s_screen = UI_SCREEN_SCANNING;
                // Render scanning screen once before blocking
                ssd1306_clear_screen(s_oled, 0x00);
                draw_scanning();
                ssd1306_refresh_gram(s_oled);
                wifi_start_scan();
                s_screen = UI_SCREEN_AP_LIST;
                s_cursor = 0; s_scroll = 0;
                break;
            }
            case 1: {  // Scan APs + Stations
                s_screen = UI_SCREEN_SCANNING;
                ssd1306_clear_screen(s_oled, 0x00);
                draw_scanning();
                ssd1306_refresh_gram(s_oled);
                wifi_start_scan();
                wifi_start_sniffer();
                vTaskDelay(pdMS_TO_TICKS(5000));
                wifi_stop_sniffer();
                s_screen = UI_SCREEN_AP_LIST;
                s_cursor = 0; s_scroll = 0;
                break;
            }
            case 2:  s_screen = UI_SCREEN_AP_LIST; s_cursor = 0; s_scroll = 0; break;
            case 3:  s_screen = UI_SCREEN_STATION_LIST; s_cursor = 0; s_scroll = 0; break;
            case 4:  wifi_select_all_aps(); wifi_select_all_stations(); break;
            case 5:  wifi_deselect_all_aps(); wifi_deselect_all_stations(); break;
            case 6:  s_screen = UI_SCREEN_MAIN_MENU; s_cursor = 0; s_scroll = 0; break;
        }
    }
}

static void handle_input_ap_list(bool up, bool down, bool sel) {
    int ap_count = wifi_get_ap_count();
    int total = ap_count + 1;  // +1 for "< Back" at index 0

    if (up && s_cursor > 0) s_cursor--;
    if (down && s_cursor < total - 1) s_cursor++;

    if (s_cursor < s_scroll) s_scroll = s_cursor;
    if (s_cursor >= s_scroll + MAX_VISIBLE_LINES) s_scroll = s_cursor - MAX_VISIBLE_LINES + 1;

    if (sel) {
        if (s_cursor == 0) {
            // "< Back" selected
            s_screen = UI_SCREEN_SCAN_MENU;
            s_cursor = 2; s_scroll = 0;  // highlight "AP List" in scan menu
        } else {
            wifi_toggle_ap(s_cursor - 1);  // offset by 1 for the Back item
        }
    }
}

static void handle_input_ap_detail(bool up, bool down, bool sel) {
    (void)up; (void)down;
    if (sel) {
        s_screen = UI_SCREEN_AP_LIST;
    }
}

static void handle_input_station_list(bool up, bool down, bool sel) {
    int st_count = wifi_get_station_count();
    int total = st_count + 1;  // +1 for "< Back" at index 0

    if (up && s_cursor > 0) s_cursor--;
    if (down && s_cursor < total - 1) s_cursor++;

    if (s_cursor < s_scroll) s_scroll = s_cursor;
    if (s_cursor >= s_scroll + MAX_VISIBLE_LINES) s_scroll = s_cursor - MAX_VISIBLE_LINES + 1;

    if (sel) {
        if (s_cursor == 0) {
            // "< Back" selected
            s_screen = UI_SCREEN_SCAN_MENU;
            s_cursor = 3; s_scroll = 0;  // highlight "Station List" in scan menu
        } else {
            wifi_toggle_station(s_cursor - 1);  // offset by 1 for the Back item
        }
    }
}

static void handle_input_attack(bool up, bool down, bool sel) {
    if (up && s_cursor > 0) s_cursor--;
    if (down && s_cursor < ATTACK_MENU_COUNT - 1) s_cursor++;

    if (s_cursor < s_scroll) s_scroll = s_cursor;
    if (s_cursor >= s_scroll + MAX_VISIBLE_LINES) s_scroll = s_cursor - MAX_VISIBLE_LINES + 1;

    if (sel) {
        switch (s_cursor) {
            case 0: s_atk_deauth = !s_atk_deauth; break;
            case 1: s_atk_beacon = !s_atk_beacon; break;
            case 2: s_atk_probe  = !s_atk_probe;  break;
            case 3: {
                uint8_t types = 0;
                if (s_atk_deauth) types |= ATTACK_DEAUTH;
                if (s_atk_beacon) types |= ATTACK_BEACON;
                if (s_atk_probe)  types |= ATTACK_PROBE;
                if (types) {
                    attack_start(types);
                    s_screen = UI_SCREEN_ATTACK_RUNNING;
                }
                break;
            }
            case 4:
                s_screen = UI_SCREEN_MAIN_MENU;
                s_cursor = 2; s_scroll = 0;
                break;
        }
    }
}

static void handle_input_attack_running(bool up, bool down, bool sel) {
    (void)up; (void)down;
    if (sel) {
        attack_stop();
        s_screen = UI_SCREEN_ATTACK_MENU;
        s_cursor = 3; s_scroll = 0;
    }
    if (!attack_is_running()) {
        s_screen = UI_SCREEN_ATTACK_MENU;
        s_cursor = 3; s_scroll = 0;
    }
}

static void handle_input_ssid(bool up, bool down, bool sel) {
    if (up && s_cursor > 0) s_cursor--;
    if (down && s_cursor < SSID_MENU_COUNT - 1) s_cursor++;

    if (sel) {
        switch (s_cursor) {
            case 0: { // Add Random SSIDs
                const char* random_ssids[] = {
                    "Free WiFi", "FBI Van", "NSA_PRISM",
                    "Pretty Fly WiFi", "Get Off My LAN",
                    "Bill Wi Science Fi", "Drop It Hotspot",
                    "LAN of the Free", "The Promised LAN",
                    "Wu-Tang LAN",
                };
                for (int i = 0; i < 10; i++) {
                    ssid_list_add(random_ssids[i], true);
                }
                break;
            }
            case 1: { // Clone scanned APs
                int count = wifi_get_ap_count();
                for (int i = 0; i < count && ssid_list_count() < MAX_SSIDS; i++) {
                    ap_info_t* ap = wifi_get_ap(i);
                    if (ap && ap->ssid[0]) {
                        ssid_list_add(ap->ssid, ap->auth_mode != 0);
                    }
                }
                break;
            }
            case 2:
                ssid_list_clear();
                break;
            case 3:
                s_screen = UI_SCREEN_MAIN_MENU;
                s_cursor = 1; s_scroll = 0;
                break;
        }
    }
}

static void handle_input_info(bool up, bool down, bool sel) {
    (void)up; (void)down;
    if (sel) {
        s_screen = UI_SCREEN_MAIN_MENU;
        s_cursor = 3;
    }
}

// ===== Public API ===== //

void display_init(void) {
    // Init GPIO for buttons
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_UP_PIN) | (1ULL << BUTTON_DOWN_PIN) | (1ULL << BUTTON_SELECT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    // Init I2C (legacy driver used by ssd1306 component)
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_NUM_0, &i2c_cfg);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    // Create SSD1306 device
    s_oled = ssd1306_create(I2C_NUM_0, 0x3C);
    if (!s_oled) {
        ESP_LOGE(TAG, "Failed to create SSD1306 display");
        return;
    }
    ssd1306_init(s_oled);

    ssd1306_clear_screen(s_oled, 0x00);
    ssd1306_refresh_gram(s_oled);

    s_screen = UI_SCREEN_INTRO;
    s_intro_time = esp_timer_get_time();
    s_last_draw = 0;

    ESP_LOGI(TAG, "Display initialized");
}

void display_update(void) {
    if (!s_oled) return;

    int64_t now = esp_timer_get_time();

    // Limit framerate
    if (now - s_last_draw < DISPLAY_DRAW_MS * 1000LL) return;
    s_last_draw = now;

    // Auto-transition from intro
    if (s_screen == UI_SCREEN_INTRO) {
        if (now - s_intro_time > DISPLAY_INTRO_MS * 1000LL) {
            s_screen = UI_SCREEN_MAIN_MENU;
            s_cursor = 0;
        }
    }

    // Read buttons
    bool up   = button_pressed(BUTTON_UP_PIN, &s_btn_up, &s_btn_up_time);
    bool down = button_pressed(BUTTON_DOWN_PIN, &s_btn_down, &s_btn_down_time);
    bool sel  = button_pressed(BUTTON_SELECT_PIN, &s_btn_sel, &s_btn_sel_time);

    // Handle input per screen
    switch (s_screen) {
        case UI_SCREEN_MAIN_MENU:      handle_input_main(up, down, sel); break;
        case UI_SCREEN_SCAN_MENU:      handle_input_scan(up, down, sel); break;
        case UI_SCREEN_AP_LIST:        handle_input_ap_list(up, down, sel); break;
        case UI_SCREEN_AP_DETAIL:      handle_input_ap_detail(up, down, sel); break;
        case UI_SCREEN_STATION_LIST:   handle_input_station_list(up, down, sel); break;
        case UI_SCREEN_ATTACK_MENU:    handle_input_attack(up, down, sel); break;
        case UI_SCREEN_ATTACK_RUNNING: handle_input_attack_running(up, down, sel); break;
        case UI_SCREEN_SSID_MENU:      handle_input_ssid(up, down, sel); break;
        case UI_SCREEN_INFO:           handle_input_info(up, down, sel); break;
        default: break;
    }

    // Clear and draw
    ssd1306_clear_screen(s_oled, 0x00);

    switch (s_screen) {
        case UI_SCREEN_INTRO:          draw_intro(); break;
        case UI_SCREEN_MAIN_MENU:      draw_main_menu(); break;
        case UI_SCREEN_SCAN_MENU:      draw_scan_menu(); break;
        case UI_SCREEN_SCANNING:       draw_scanning(); break;
        case UI_SCREEN_AP_LIST:        draw_ap_list(); break;
        case UI_SCREEN_AP_DETAIL:      draw_ap_detail(); break;
        case UI_SCREEN_STATION_LIST:   draw_station_list(); break;
        case UI_SCREEN_ATTACK_MENU:    draw_attack_menu(); break;
        case UI_SCREEN_ATTACK_RUNNING: draw_attack_running(); break;
        case UI_SCREEN_SSID_MENU:      draw_ssid_menu(); break;
        case UI_SCREEN_INFO:           draw_info(); break;
    }

    ssd1306_refresh_gram(s_oled);
}

void display_set_screen(ui_screen_t screen) {
    s_screen = screen;
    s_cursor = 0;
    s_scroll = 0;
}

ui_screen_t display_get_screen(void) {
    return s_screen;
}
