/**
 * ESP32-C3 WiFi Deauther - Configuration
 * Hardware: ESP32-C3 SuperMini + SSD1306 0.96" OLED + 3 Buttons
 */
#pragma once

// ===== VERSION ===== //
#define DEAUTHER_VERSION       "1.0.0-C3"
#define DEAUTHER_VERSION_MAJOR 1
#define DEAUTHER_VERSION_MINOR 0

// ===== HARDWARE PINS ===== //
#define BUTTON_UP_PIN          0   // GPIO 0
#define BUTTON_DOWN_PIN        1   // GPIO 1
#define BUTTON_SELECT_PIN      2   // GPIO 2

#define I2C_MASTER_SDA_IO      6   // SDA on GPIO 6
#define I2C_MASTER_SCL_IO      7   // SCL on GPIO 7
#define I2C_MASTER_FREQ_HZ     400000

// ===== NRF24L01+ SPI PINS (1x module supported, expandable to 2x) ===== //
// Wiring: NRF VCC→3.3V, GND→GND
#define NRF_CE_PIN             4   // GPIO 4  → NRF CE
#define NRF_CSN_PIN            5   // GPIO 5  → NRF CSN (CS)
#define NRF_SCK_PIN            8   // GPIO 8  → NRF SCK
#define NRF_MISO_PIN           9   // GPIO 9  → NRF MISO
#define NRF_MOSI_PIN           10  // GPIO 10 → NRF MOSI
#define NRF_SPI_HOST           SPI2_HOST
#define NRF_SPI_FREQ_HZ        4000000   // 4 MHz (very stable for nRF24L01+)

#define OLED_WIDTH             128
#define OLED_HEIGHT            64

// ===== BUTTON TIMING ===== //
#define BUTTON_DEBOUNCE_MS     50
#define BUTTON_REPEAT_MS       200
#define BUTTON_HOLD_MS         800

// ===== DISPLAY ===== //
#define DISPLAY_FPS            10
#define DISPLAY_DRAW_MS        (1000 / DISPLAY_FPS)
#define DISPLAY_INTRO_MS       2500
#define DISPLAY_TIMEOUT_S      600
#define DISPLAY_MAX_CHARS      21  // chars per line at 6px font

// ===== WIFI / SCAN ===== //
#define MAX_APS                64
#define MAX_STATIONS           128
#define MAX_SSIDS              64
#define SCAN_CHANNEL_TIME_MS   200
#define WIFI_CHANNEL_MAX       13

// ===== ATTACK DEFAULTS ===== //
#define DEAUTHS_PER_TARGET     25
#define DEAUTH_REASON_CODE     1
#define BEACON_INTERVAL_100MS  true
#define ATTACK_TIMEOUT_S       600
#define PROBE_FRAMES_PER_SSID  1

// ===== AP DEFAULTS ===== //
#define DEFAULT_AP_SSID        "C3-Deauther"
#define DEFAULT_AP_PASS        "deauther"
#define DEFAULT_AP_CHANNEL     1

// ===== NVS ===== //
#define NVS_NAMESPACE          "deauther"
