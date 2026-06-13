/**
 * Attack Module - Deauth, Beacon Spam, Probe Flood
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ATTACK_NONE   = 0,
    ATTACK_DEAUTH = (1 << 0),
    ATTACK_BEACON = (1 << 1),
    ATTACK_PROBE  = (1 << 2),
} attack_type_t;

typedef struct {
    uint32_t deauth_sent;
    uint32_t beacon_sent;
    uint32_t probe_sent;
    uint32_t packets_per_sec;
    bool     running;
    uint8_t  active_types;  // bitmask of attack_type_t
} attack_status_t;

void attack_init(void);
void attack_start(uint8_t types);  // bitmask of attack_type_t
void attack_stop(void);
bool attack_is_running(void);
attack_status_t attack_get_status(void);

// Called periodically from main loop
void attack_update(void);
