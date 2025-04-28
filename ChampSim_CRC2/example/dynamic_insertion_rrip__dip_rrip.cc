#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE    1
#define LLC_SETS    (NUM_CORE * 2048)
#define LLC_WAYS    16

// SRRIP parameters
static const uint8_t MAX_RRPV = 3;
static uint8_t rrpv[LLC_SETS][LLC_WAYS];

// DIP parameters
static const int    PSEL_BITS    = 10;
static const int    PSEL_MAX     = (1 << PSEL_BITS) - 1;
static const int    PSEL_INIT    = PSEL_MAX / 2;
static int          PSEL         = PSEL_INIT;
static const int    LEADER_SIZE  = 32;  // 32 leader-set IDs
static const int    HOT_LEADERS  = 16;  // lower 16 IDs are hot leaders

// Stats
static uint64_t stat_hits   = 0;
static uint64_t stat_misses = 0;

// Initialize replacement state
void InitReplacementState() {
    // init RRPVs
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            rrpv[s][w] = MAX_RRPV;
        }
    }
    // init policy selector and stats
    PSEL = PSEL_INIT;
    stat_hits = stat_misses = 0;
}

// Find victim in the set (standard SRRIP aging)
uint32_t GetVictimInSet(
    uint32_t    cpu,
    uint32_t    set,
    const BLOCK *current_set,
    uint64_t    PC,
    uint64_t    paddr,
    uint32_t    type
) {
    // 1) free way?
    for (uint32_t w = 0; w < LLC_WAYS; w++)
        if (! current_set[w].valid)
            return w;
    // 2) find RRPV==MAX; if none, age all and retry
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++)
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        for (uint32_t w = 0; w < LLC_WAYS; w++)
            if (rrpv[set][w] < MAX_RRPV)
                rrpv[set][w]++;
    }
}

// Update on hit/fill
void UpdateReplacementState(
    uint32_t cpu,
    uint32_t set,
    uint32_t way,
    uint64_t paddr,
    uint64_t PC,
    uint64_t victim_addr,
    uint32_t type,
    uint8_t  hit
) {
    // HIT: promote to MRU
    if (hit) {
        stat_hits++;
        rrpv[set][way] = 0;
        return;
    }
    // MISS: choose insertion policy
    stat_misses++;
    // classify set as hot-leader, cold-leader, or follower
    int lid = set % LEADER_SIZE;
    bool is_hot_leader  = (lid < HOT_LEADERS);
    bool is_cold_leader = (lid >= HOT_LEADERS && lid < LEADER_SIZE);
    // update PSEL on misses in leader sets
    if (is_hot_leader) {
        if (PSEL > 0) PSEL--;
    } else if (is_cold_leader) {
        if (PSEL < PSEL_MAX) PSEL++;
    }
    // determine insertion RRPV
    bool insert_hot;
    if (is_hot_leader) {
        insert_hot = true;
    } else if (is_cold_leader) {
        insert_hot = false;
    } else {
        // follower: choose based on PSEL
        insert_hot = (PSEL < PSEL_INIT) ? true : false;
    }
    rrpv[set][way] = insert_hot ? 0 : (MAX_RRPV - 1);
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== DIP‐RRIP Final Stats ===\n";
    std::cout << "Hits   : " << stat_hits   << "\n";
    std::cout << "Misses : " << stat_misses << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[DIP‐RRIP] H:" << stat_hits
              << " M:" << stat_misses
              << " PSEL:" << PSEL
              << std::endl;
}