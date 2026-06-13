/**
 * Display UI Module - SSD1306 OLED with 3-Button Menu Navigation
 */
#pragma once

#include <stdbool.h>

typedef enum {
    UI_SCREEN_INTRO,
    UI_SCREEN_HOME,
    UI_SCREEN_MAIN_MENU,
    UI_SCREEN_SCAN_MENU,
    UI_SCREEN_SCANNING,
    UI_SCREEN_AP_LIST,
    UI_SCREEN_AP_DETAIL,
    UI_SCREEN_STATION_LIST,
    UI_SCREEN_ATTACK_MENU,
    UI_SCREEN_ATTACK_RUNNING,
    UI_SCREEN_SSID_MENU,
    UI_SCREEN_INFO,
} ui_screen_t;

void display_init(void);
void display_update(void);
void display_set_screen(ui_screen_t screen);
ui_screen_t display_get_screen(void);
