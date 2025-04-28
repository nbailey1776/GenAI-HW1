#include "../inc/champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16

// LRU state and reuse counters
uint32_t adaptive_lru[LLC_SETS][LLC_WAYS];
uint32_t reuse_counter[LLC_SETS][LLC_WAYS];

// initialize replacement state
void InitReplacementState()
{
    cout << "Initialize AdaptiveLRU replacement state" << endl;

    for (int i=0; i<LLC_SETS; i++) {
        for (int j=0; j<LLC_WAYS; j++) {
            adaptive_lru[i][j] = j;
            reuse_counter[i][j] = 0;
        }
    }
}

// find replacement victim
// return value should be 0 ~ 15 or 16 (bypass)
uint32_t GetVictimInSet (uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type)
{
    // Prefer to evict the block with highest LRU position and lowest reuse count
    uint32_t victim = 0;
    int32_t max_lru = -1;
    uint32_t min_reuse = UINT32_MAX;

    for (int i = 0; i < LLC_WAYS; i++) {
        if (adaptive_lru[set][i] > max_lru ||
            (adaptive_lru[set][i] == max_lru && reuse_counter[set][i] < min_reuse)) {
            max_lru = adaptive_lru[set][i];
            min_reuse = reuse_counter[set][i];
            victim = i;
        }
    }

    return victim;
}

// called on every cache hit and cache fill
void UpdateReplacementState (uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    // Increment reuse counter on hit
    if (hit) {
        reuse_counter[set][way]++;
    } else {
        // On fill, reset reuse counter
        reuse_counter[set][way] = 0;
    }

    // Update LRU positions
    for (int i = 0; i < LLC_WAYS; i++) {
        if (adaptive_lru[set][i] < adaptive_lru[set][way]) {
            adaptive_lru[set][i]++;
        }
    }
    adaptive_lru[set][way] = 0;
}

// use this function to print out your own stats on every heartbeat
void PrintStats_Heartbeat()
{
    // Optional: add per-heartbeat stats for debugging
}

// use this function to print out your own stats at the end of simulation
void PrintStats()
{
    cout << "AdaptiveLRU Replacement Policy Stats:" << endl;
    // Optional: add final stats printout
}
