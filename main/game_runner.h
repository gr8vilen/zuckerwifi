/**
 * Dinosaur Runner Game Module for SSD1306
 */
#pragma once

#include <stdbool.h>
#include "ssd1306.h"

// Initialize game variables
void game_runner_init(void);

// Update game physics and logic (called rapidly in the main loop)
void game_runner_update(void);

// Handle UI button presses to trigger jumps
void game_runner_handle_input(bool up, bool down, bool sel);

// Draw the current frame to the OLED display
void game_runner_draw(ssd1306_handle_t oled);

// Ask the game loop if we should exit (user pressed Down or Back)
bool game_runner_should_exit(void);
