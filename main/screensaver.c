#include "screensaver.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "ssd1306.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

// Clamp helpers — prevent negative ints wrapping to uint8_t 250+
// which causes ssd1306_fill_rectangle to loop forever
#define CLAMP_X(v) ((uint8_t)((v) < 0 ? 0 : ((v) > 127 ? 127 : (v))))
#define CLAMP_Y(v) ((uint8_t)((v) < 0 ? 0 : ((v) >  63 ?  63 : (v))))

static void safe_fill_rect(ssd1306_handle_t oled, int x1, int y1, int x2, int y2, uint8_t c) {
    int ax1 = x1 < x2 ? x1 : x2;  // ensure x1 <= x2
    int ax2 = x1 < x2 ? x2 : x1;
    int ay1 = y1 < y2 ? y1 : y2;  // ensure y1 <= y2
    int ay2 = y1 < y2 ? y2 : y1;
    // Skip if entirely off-screen
    if (ax2 < 0 || ax1 > 127 || ay2 < 0 || ay1 > 63) return;
    ssd1306_fill_rectangle(oled, CLAMP_X(ax1), CLAMP_Y(ay1), CLAMP_X(ax2), CLAMP_Y(ay2), c);
}

static void safe_fill_point(ssd1306_handle_t oled, int x, int y, uint8_t c) {
    if (x < 0 || x > 127 || y < 0 || y > 63) return;
    ssd1306_fill_point(oled, (uint8_t)x, (uint8_t)y, c);
}

static void safe_draw_line(ssd1306_handle_t oled, int x1, int y1, int x2, int y2) {
    // Coarse clip — skip lines that start AND end off-screen in the same direction
    if ((x1 < 0 && x2 < 0) || (x1 > 127 && x2 > 127)) return;
    if ((y1 < 0 && y2 < 0) || (y1 > 63  && y2 > 63))  return;
    ssd1306_draw_line(oled, CLAMP_X(x1), CLAMP_Y(y1), CLAMP_X(x2), CLAMP_Y(y2));
}

typedef enum {
    SKEL_STATE_DANCING,
    SKEL_STATE_WALKING,
    SKEL_STATE_SLEEPING,
    SKEL_STATE_WAVING,
    SKEL_STATE_COUNT
} skel_state_t;

// Animation state
static skel_state_t s_state = SKEL_STATE_DANCING;
static int64_t s_state_change_time = 0;

// Zzz particles structure for sleeping state
typedef struct {
    float x;
    float y;
    float age; // 0.0 to 1.0
} z_particle_t;

#define MAX_Z_PARTICLES 3
static z_particle_t s_z_particles[MAX_Z_PARTICLES];

void screensaver_init(void) {
    s_state = SKEL_STATE_DANCING;
    s_state_change_time = esp_timer_get_time();
    for (int i = 0; i < MAX_Z_PARTICLES; i++) {
        s_z_particles[i].age = 1.0f; // inactive
    }
}

// Procedural drawing function for our cute skeleton
static void draw_skeleton(ssd1306_handle_t oled, int cx, int cy,
                          int l_shoulder_x, int l_shoulder_y,
                          int l_elbow_x, int l_elbow_y,
                          int l_hand_x, int l_hand_y,
                          int r_shoulder_x, int r_shoulder_y,
                          int r_elbow_x, int r_elbow_y,
                          int r_hand_x, int r_hand_y,
                          int l_hip_x, int l_hip_y,
                          int l_knee_x, int l_knee_y,
                          int l_foot_x, int l_foot_y,
                          int r_hip_x, int r_hip_y,
                          int r_knee_x, int r_knee_y,
                          int r_foot_x, int r_foot_y,
                          bool slumped) {
    // 1. Draw head (Cute chibi rounded skull, 10x8 pixels)
    int hx = cx;
    int hy = slumped ? (cy - 10) : (cy - 14);

    // Main skull box (safe — coords may be off-screen during walk)
    safe_fill_rect(oled, hx - 5, hy - 4, hx + 4, hy + 3, 1);

    // Trim corners to round the head
    safe_fill_point(oled, hx - 5, hy - 4, 0);
    safe_fill_point(oled, hx + 4, hy - 4, 0);
    safe_fill_point(oled, hx - 5, hy + 3, 0);
    safe_fill_point(oled, hx + 4, hy + 3, 0);

    // Eye sockets (black holes)
    safe_fill_point(oled, hx - 2, hy - 1, 0);
    safe_fill_point(oled, hx + 1, hy - 1, 0);

    // Nose cavity
    safe_fill_point(oled, hx, hy + 1, 0);

    // Smiling teeth line
    safe_fill_point(oled, hx - 2, hy + 3, 0);
    safe_fill_point(oled, hx,     hy + 3, 0);
    safe_fill_point(oled, hx + 2, hy + 3, 0);

    // 2. Spine / Ribcage
    int spine_bottom_y = cy + 4;
    safe_draw_line(oled, cx, hy + 4, cx, spine_bottom_y);

    // Rib horizontal bars
    safe_draw_line(oled, cx - 3, hy + 6,  cx + 3, hy + 6);
    safe_draw_line(oled, cx - 4, hy + 9,  cx + 4, hy + 9);
    safe_draw_line(oled, cx - 3, hy + 12, cx + 3, hy + 12);

    // 3. Pelvis/Hips
    safe_draw_line(oled, cx - 3, spine_bottom_y, cx + 3, spine_bottom_y);

    // 4. Arms
    safe_draw_line(oled, l_shoulder_x, l_shoulder_y, l_elbow_x, l_elbow_y);
    safe_draw_line(oled, l_elbow_x,    l_elbow_y,    l_hand_x,  l_hand_y);
    safe_draw_line(oled, r_shoulder_x, r_shoulder_y, r_elbow_x, r_elbow_y);
    safe_draw_line(oled, r_elbow_x,    r_elbow_y,    r_hand_x,  r_hand_y);

    // 5. Legs
    safe_draw_line(oled, l_hip_x, l_hip_y, l_knee_x, l_knee_y);
    safe_draw_line(oled, l_knee_x, l_knee_y, l_foot_x, l_foot_y);
    safe_draw_line(oled, r_hip_x, r_hip_y, r_knee_x, r_knee_y);
    safe_draw_line(oled, r_knee_x, r_knee_y, r_foot_x, r_foot_y);
}

void screensaver_draw(ssd1306_handle_t oled) {
    int64_t now = esp_timer_get_time();
    double time_sec = now / 1000000.0;

    // Random state change every 6 seconds
    if (now - s_state_change_time > 6000000LL) {
        s_state = (skel_state_t)(esp_random() % SKEL_STATE_COUNT);
        s_state_change_time = now;
        
        // Reset state-specific variables
        if (s_state == SKEL_STATE_SLEEPING) {
            for (int i = 0; i < MAX_Z_PARTICLES; i++) {
                s_z_particles[i].age = 1.0f;
            }
        }
    }

    // Common joint calculations
    int cx = 64;
    int cy = 40;
    
    int l_shoulder_x = cx - 4, l_shoulder_y = cy - 5;
    int r_shoulder_x = cx + 4, r_shoulder_y = cy - 5;
    
    int l_hip_x = cx - 2, l_hip_y = cy + 4;
    int r_hip_x = cx + 2, r_hip_y = cy + 4;

    int l_elbow_x = 0, l_elbow_y = 0, l_hand_x = 0, l_hand_y = 0;
    int r_elbow_x = 0, r_elbow_y = 0, r_hand_x = 0, r_hand_y = 0;
    int l_knee_x = 0,  l_knee_y = 0,  l_foot_x = 0, l_foot_y = 0;
    int r_knee_x = 0,  r_knee_y = 0,  r_foot_x = 0, r_foot_y = 0;
    
    bool slumped = false;

    // Draw bottom ground line
    ssd1306_draw_line(oled, 0, 57, 127, 57);

    switch (s_state) {
        case SKEL_STATE_DANCING: {
            double t = time_sec * 6.0;
            // Hip wiggle and bob
            cx = 64 + (int)(sin(t) * 5.0);
            cy = 40 + (int)(abs((int)(cos(t) * 2.0)));

            l_shoulder_x = cx - 4; l_shoulder_y = cy - 5;
            r_shoulder_x = cx + 4; r_shoulder_y = cy - 5;
            l_hip_x = cx - 2; l_hip_y = cy + 4;
            r_hip_x = cx + 2; r_hip_y = cy + 4;

            // Dancing hands waving up and down
            l_elbow_x = cx - 8;
            l_elbow_y = cy - 7 + (int)(sin(t * 1.5) * 4.0);
            l_hand_x = cx - 11;
            l_hand_y = cy - 11 + (int)(cos(t * 1.5) * 5.0);

            r_elbow_x = cx + 8;
            r_elbow_y = cy - 7 - (int)(sin(t * 1.5) * 4.0);
            r_hand_x = cx + 11;
            r_hand_y = cy - 11 - (int)(cos(t * 1.5) * 5.0);

            // Shuffling knees/feet
            l_knee_x = l_hip_x - 2 + (int)(sin(t) * 2.0);
            l_knee_y = cy + 10;
            l_foot_x = l_hip_x - 3;
            l_foot_y = 56;

            r_knee_x = r_hip_x + 2 - (int)(sin(t) * 2.0);
            r_knee_y = cy + 10;
            r_foot_x = r_hip_x + 3;
            r_foot_y = 56;

            ssd1306_draw_string(oled, 3, 2, (const uint8_t*)"* DANCE *", 12, 1);
            break;
        }

        case SKEL_STATE_WALKING: {
            double t = time_sec * 5.0;
            // Move across screen
            cx = -15 + ((int)(time_sec * 15.0) % 158);
            cy = 40 + (int)(abs((int)(sin(t) * 1.5)));

            l_shoulder_x = cx - 4; l_shoulder_y = cy - 5;
            r_shoulder_x = cx + 4; r_shoulder_y = cy - 5;
            l_hip_x = cx - 2; l_hip_y = cy + 4;
            r_hip_x = cx + 2; r_hip_y = cy + 4;

            // Scissor walk legs
            l_knee_x = l_hip_x + (int)(sin(t) * 3.0);
            l_knee_y = cy + 10;
            l_foot_x = l_knee_x + (int)(sin(t + 0.3) * 2.0);
            l_foot_y = 56;

            r_knee_x = r_hip_x - (int)(sin(t) * 3.0);
            r_knee_y = cy + 10;
            r_foot_x = r_knee_x - (int)(sin(t + 0.3) * 2.0);
            r_foot_y = 56;

            // Swinging arms
            l_elbow_x = l_shoulder_x - 3 - (int)(sin(t) * 2.0);
            l_elbow_y = cy - 1 + (int)(cos(t) * 2.0);
            l_hand_x = l_elbow_x - 2;
            l_hand_y = cy + 3;

            r_elbow_x = r_shoulder_x + 3 + (int)(sin(t) * 2.0);
            r_elbow_y = cy - 1 - (int)(cos(t) * 2.0);
            r_hand_x = r_elbow_x + 2;
            r_hand_y = cy + 3;

            ssd1306_draw_string(oled, 3, 2, (const uint8_t*)"* WALK *", 12, 1);
            break;
        }

        case SKEL_STATE_SLEEPING: {
            slumped = true;
            cy = 44; // Slumped lower, sitting on floor

            l_shoulder_x = cx - 4; l_shoulder_y = cy - 3;
            r_shoulder_x = cx + 4; r_shoulder_y = cy - 3;
            l_hip_x = cx - 2; l_hip_y = cy + 4;
            r_hip_x = cx + 2; r_hip_y = cy + 4;

            // Arms resting on the knees
            l_elbow_x = cx - 6; l_elbow_y = cy + 1;
            l_hand_x = cx - 5;  l_hand_y = cy + 5;

            r_elbow_x = cx + 6; r_elbow_y = cy + 1;
            r_hand_x = cx + 5;  r_hand_y = cy + 5;

            // Legs bent outward sitting down
            l_knee_x = cx - 8;  l_knee_y = cy + 8;
            l_foot_x = cx - 11; l_foot_y = 56;

            r_knee_x = cx + 8;  r_knee_y = cy + 8;
            r_foot_x = cx + 11; r_foot_y = 56;

            // Head breathing rise and fall
            double breath = sin(time_sec * 2.0) * 0.8;
            l_shoulder_y += (int)breath;
            r_shoulder_y += (int)breath;

            // Zzz particles
            for (int i = 0; i < MAX_Z_PARTICLES; i++) {
                if (s_z_particles[i].age >= 1.0f) {
                    // Spawn new Z particle randomly
                    if ((esp_random() % 100) < 5) {
                        s_z_particles[i].x = cx + 5;
                        s_z_particles[i].y = cy - 12;
                        s_z_particles[i].age = 0.0f;
                    }
                } else {
                    // Update particle
                    s_z_particles[i].age += 0.02f;
                    s_z_particles[i].y -= 0.6f;
                    s_z_particles[i].x += sin(time_sec * 4.0 + i) * 0.4f;

                    // Draw Zzz
                    int sz = (s_z_particles[i].age < 0.4f) ? 12 : ((s_z_particles[i].age < 0.8f) ? 12 : 12);
                    char z_str[2] = { s_z_particles[i].age < 0.5f ? 'z' : 'Z', '\0' };
                    ssd1306_draw_string(oled, (int)s_z_particles[i].x, (int)s_z_particles[i].y, (const uint8_t*)z_str, sz, 1);
                }
            }

            ssd1306_draw_string(oled, 3, 2, (const uint8_t*)"* SLEEP *", 12, 1);
            break;
        }

        case SKEL_STATE_WAVING: {
            double t = time_sec * 7.0;

            // Left arm resting at side
            l_elbow_x = l_shoulder_x - 2;
            l_elbow_y = cy - 1;
            l_hand_x = l_elbow_x;
            l_hand_y = cy + 4;

            // Right arm waving back and forth overhead
            r_elbow_x = r_shoulder_x + 3;
            r_elbow_y = cy - 11;
            r_hand_x = r_elbow_x + (int)(sin(t) * 4.0);
            r_hand_y = cy - 16;

            // Standing legs
            l_knee_x = l_hip_x - 1;
            l_knee_y = cy + 10;
            l_foot_x = l_hip_x - 1;
            l_foot_y = 56;

            r_knee_x = r_hip_x + 1;
            r_knee_y = cy + 10;
            r_foot_x = r_hip_x + 1;
            r_foot_y = 56;

            ssd1306_draw_string(oled, 3, 2, (const uint8_t*)"* WAVE *", 12, 1);
            break;
        }

        default:
            break;
    }

    // Draw the skeleton with the calculated joint positions
    draw_skeleton(oled, cx, cy,
                  l_shoulder_x, l_shoulder_y,
                  l_elbow_x, l_elbow_y,
                  l_hand_x, l_hand_y,
                  r_shoulder_x, r_shoulder_y,
                  r_elbow_x, r_elbow_y,
                  r_hand_x, r_hand_y,
                  l_hip_x, l_hip_y,
                  l_knee_x, l_knee_y,
                  l_foot_x, l_foot_y,
                  r_hip_x, r_hip_y,
                  r_knee_x, r_knee_y,
                  r_foot_x, r_foot_y,
                  slumped);
}
