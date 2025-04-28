#include "champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16

uint32_t recency[LLC_SETS][LLC_WAYS];

// initialize replacement state
void InitReplacementState() {
    for(int i = 0; i < LLC_SETS; i++) {
        for(int j = 0; j < LLC_WAYS; j++) {
            recency[i][j] = 0;
        }
    }
}

// find replacement victim
uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type) {
    uint32_t max_value = 0;
    uint32_t victim = 0;

    for (uint32_t i = 0; i < LLC_WAYS; i++) {
        if (recency[set][i] >= max_value) {
            max_value = recency[set][i];
            victim = i;
        }
    }

    return victim;
}

// update replacement state
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit) {
    for (uint32_t i = 0; i < LLC_WAYS; i++) {
        if (i != way) recency[set][i]++;
    }
    recency[set][way] = 0;
}

// heartbeat (optional)
void PrintStats_Heartbeat() {}

// final stats (optional)
void PrintStats() {}
