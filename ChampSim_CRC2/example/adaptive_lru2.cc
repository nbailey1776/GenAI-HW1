#include "champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16

// LRU state and reuse counters
uint32_t adaptive_lru[LLC_SETS][LLC_WAYS];
uint32_t reuse_counter[LLC_SETS][LLC_WAYS];

// Initialize replacement state
void InitReplacementState()
{
    cout << "Initialize AdaptiveLRU replacement state" << endl;

    for (uint32_t i = 0; i < LLC_SETS; i++) {
        for (uint32_t j = 0; j < LLC_WAYS; j++) {
            adaptive_lru[i][j] = j;
            reuse_counter[i][j] = 0;
        }
    }
}

// Find replacement victim
// return value should be 0 ~ 15 or 16 (bypass)
uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type)
{
    uint32_t victim = 0;
    uint32_t max_lru = 0;
    uint32_t min_reuse = UINT32_MAX;

    for (uint32_t i = 0; i < LLC_WAYS; i++) {
        if (adaptive_lru[set][i] > max_lru ||
            (adaptive_lru[set][i] == max_lru && reuse_counter[set][i] < min_reuse)) {
            max_lru = adaptive_lru[set][i];
            min_reuse = reuse_counter[set][i];
            victim = i;
        }
    }

    return victim;
}

// Called on every cache hit and cache fill
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way,
                            uint64_t paddr, uint64_t PC, uint64_t victim_addr,
                            uint32_t type, uint8_t hit)
{
    if (hit) {
        reuse_counter[set][way]++;
    } else {
        reuse_counter[set][way] = 0;
    }

    // Update LRU stack positions
    for (uint32_t i = 0; i < LLC_WAYS; i++) {
        if (adaptive_lru[set][i] < adaptive_lru[set][way]) {
            adaptive_lru[set][i]++;
        }
    }
    adaptive_lru[set][way] = 0; // Promote to MRU
}

// Optional: Print stats during simulation
void PrintStats_Heartbeat()
{
    // Add stats if needed per heartbeat
}

// Optional: Print final stats
void PrintStats()
{
    cout << "AdaptiveLRU Replacement Policy Stats:" << endl;
    // Print any final policy-specific stats here
}
