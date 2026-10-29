#pragma once

#include <stddef.h>
#include <stdint.h>

// Canais reais da M1: GPIO4, GPIO27, GPIO13, GPIO32.
#define TOUCH_A_CHANNEL 0
#define TOUCH_B_CHANNEL 7
#define TOUCH_C_CHANNEL 4
#define TOUCH_D_CHANNEL 9
#define INPUT_A_MASK (1UL << TOUCH_A_CHANNEL)
#define INPUT_B_MASK (1UL << TOUCH_B_CHANNEL)
#define INPUT_C_MASK (1UL << TOUCH_C_CHANNEL)
#define INPUT_D_MASK (1UL << TOUCH_D_CHANNEL)
#define EVENT_SCRIPT_COUNT 13U
#define EVENT_SCRIPT_SEQUENCE "CBCABCBCBCBCDC"

typedef struct {
    uint64_t offset_us;  // Tempo desde o inicio do contador GPTimer.
    uint32_t mask;       // B+C no mesmo alarme, B enfileirado antes de C.
} scripted_event_t;

extern const scripted_event_t EVENT_SCRIPT[EVENT_SCRIPT_COUNT];
