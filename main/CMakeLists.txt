idf_component_register(
    SRCS "main.c" "wifi_ctl.c" "attack.c" "display_ui.c" "screensaver.c" "game_runner.c"
    PRIV_REQUIRES
        spi_flash
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_timer
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        espressif__ssd1306
    INCLUDE_DIRS "."
)

# Allow overriding the WiFi library's ieee80211_raw_frame_sanity_check
# This must be applied to the final executable via idf_build_set_property
idf_build_set_property(LINK_OPTIONS "-Wl,-zmuldefs" APPEND)
