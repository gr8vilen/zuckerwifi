#include "game_runner.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <stdio.h>

#define GROUND_Y        50
#define GRAVITY         1.0f
#define JUMP_VELOCITY   -6.5f
#define SPEED_MULTIPLIER 1.0f

typedef enum {
    STATE_START,
    STATE_PLAYING,
    STATE_GAMEOVER
} game_state_t;

static game_state_t s_state = STATE_START;
static bool s_should_exit = false;

// Player vars
static float s_dino_y = GROUND_Y;
static float s_dino_vy = 0.0f;
static bool s_is_jumping = false;
static int s_score = 0;
static int s_high_score = 0;
static int64_t s_last_update_time = 0;

// Obstacle vars (simplified to just one cactus on screen at a time for 128px width)
static float s_cactus_x = 128.0f;
static float s_cactus_speed = 3.0f;

// Basic 8x8 dino sprite
static const uint8_t dino_sprite[8] = {
    0b00111100,
    0b00111111,
    0b00111011,
    0b11111111,
    0b11111110,
    0b00111100,
    0b00100100,
    0b00100100
};

// Basic 4x8 cactus sprite
static const uint8_t cactus_sprite[8] = {
    0b01000000,
    0b01010000,
    0b11110000,
    0b01010000,
    0b01000000,
    0b01000000,
    0b01000000,
    0b01000000
};

void game_runner_init(void) {
    s_state = STATE_START;
    s_dino_y = GROUND_Y;
    s_dino_vy = 0.0f;
    s_is_jumping = false;
    s_score = 0;
    s_cactus_x = 128.0f;
    s_cactus_speed = 3.0f;
    s_should_exit = false;
    s_last_update_time = esp_timer_get_time();
}

static void reset_game(void) {
    s_dino_y = GROUND_Y;
    s_dino_vy = 0.0f;
    s_is_jumping = false;
    s_score = 0;
    s_cactus_x = 128.0f;
    s_cactus_speed = 3.0f;
    s_state = STATE_PLAYING;
    s_last_update_time = esp_timer_get_time();
}

void game_runner_update(void) {
    int64_t now = esp_timer_get_time();
    // 30 FPS logic update (33ms)
    if (now - s_last_update_time < 33000) return;
    s_last_update_time = now;

    if (s_state != STATE_PLAYING) return;

    // Apply physics
    if (s_is_jumping) {
        s_dino_y += s_dino_vy;
        s_dino_vy += GRAVITY;

        if (s_dino_y >= GROUND_Y) {
            s_dino_y = GROUND_Y;
            s_dino_vy = 0.0f;
            s_is_jumping = false;
        }
    }

    // Move obstacle
    s_cactus_x -= s_cactus_speed;
    if (s_cactus_x < -8.0f) {
        s_cactus_x = 128.0f + (esp_random() % 30); // Randomize next appearance distance slightly
        s_score++;
        
        // Slightly increase speed
        s_cactus_speed += 0.1f;
        if (s_cactus_speed > 6.0f) s_cactus_speed = 6.0f;
    }

    // Simple AABB Collision Detection
    // Dino bounding box: x=10 to 18, y=s_dino_y to s_dino_y+8
    // Cactus bounding box: x=s_cactus_x to s_cactus_x+4, y=GROUND_Y to GROUND_Y+8
    
    int dx1 = 10;
    int dy1 = (int)s_dino_y;
    int dw1 = 8;
    int dh1 = 8;
    
    int cx1 = (int)s_cactus_x;
    int cy1 = GROUND_Y;
    int cw1 = 4;
    int ch1 = 8;

    if (dx1 < cx1 + cw1 &&
        dx1 + dw1 > cx1 &&
        dy1 < cy1 + ch1 &&
        dy1 + dh1 > cy1) {
        // Collision!
        s_state = STATE_GAMEOVER;
        if (s_score > s_high_score) {
            s_high_score = s_score;
        }
    }
}

void game_runner_handle_input(bool up, bool down, bool sel) {
    if (down) {
        // Exit game unconditionally if user presses 'down'
        s_should_exit = true;
        return;
    }

    if (up || sel) {
        if (s_state == STATE_START) {
            reset_game();
        } else if (s_state == STATE_GAMEOVER) {
            reset_game();
        } else if (s_state == STATE_PLAYING && !s_is_jumping) {
            s_is_jumping = true;
            s_dino_vy = JUMP_VELOCITY;
        }
    }
}

bool game_runner_should_exit(void) {
    return s_should_exit;
}

// Simple custom bitmap drawing function
static void draw_bitmap(ssd1306_handle_t oled, int x, int y, const uint8_t* bitmap, int w, int h) {
    for (int by = 0; by < h; by++) {
        for (int bx = 0; bx < w; bx++) {
            if (bitmap[by] & (1 << (7 - bx))) {
                ssd1306_fill_point(oled, x + bx, y + by, 1);
            }
        }
    }
}

void game_runner_draw(ssd1306_handle_t oled) {
    if (s_state == STATE_START) {
        ssd1306_draw_string(oled, 15, 20, (const uint8_t*)"DINO RUNNER", 16, 1);
        ssd1306_draw_string(oled, 20, 45, (const uint8_t*)"SEL to Start", 12, 1);
        return;
    }

    // Draw Ground
    ssd1306_draw_line(oled, 0, GROUND_Y + 8, 127, GROUND_Y + 8);

    // Draw Score
    char buf[16];
    snprintf(buf, sizeof(buf), "HI:%04d %04d", s_high_score, s_score);
    ssd1306_draw_string(oled, 40, 2, (const uint8_t*)buf, 12, 1);

    // Draw Dino
    draw_bitmap(oled, 10, (int)s_dino_y, dino_sprite, 8, 8);

    // Draw Cactus
    if (s_cactus_x >= 0 && s_cactus_x < 128) {
        draw_bitmap(oled, (int)s_cactus_x, GROUND_Y, cactus_sprite, 4, 8);
    }

    // Draw Game Over Overlay
    if (s_state == STATE_GAMEOVER) {
        ssd1306_draw_string(oled, 30, 25, (const uint8_t*)"GAME OVER", 16, 1);
        ssd1306_draw_string(oled, 15, 45, (const uint8_t*)"SEL to Restart", 12, 1);
    }
}
