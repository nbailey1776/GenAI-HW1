#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE       1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS       16

// SRRIP parameters
static const uint8_t MAX_RRPV      = 3;
static uint8_t       rrpv[LLC_SETS][LLC_WAYS];

// SHiP parameters
static const int     SHCT_SIZE     = 1024;    // must be power of two
static const int     SHCT_MAX      = 3;       // 2-bit counter max
static const int     SHCT_INIT     = 0;       // initial counter
static uint8_t       SHCT[SHCT_SIZE];
static uint16_t      b_signature[LLC_SETS][LLC_WAYS];

// Stats
static uint64_t stat_hits   = 0;
static uint64_t stat_misses = 0;

// Helpers
static inline uint32_t get_signature(uint64_t PC, uint64_t paddr) {
    // simple XOR and mask
    return (uint32_t)((PC ^ (paddr >> 6)) & (SHCT_SIZE - 1));
}

// Initialize replacement state
void InitReplacementState() {
    // initialize RRPVs to MAX and block signatures to zero
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            rrpv[s][w]        = MAX_RRPV;
            b_signature[s][w] = 0;
        }
    }
    // init SHCT
    for (int i = 0; i < SHCT_SIZE; i++) {
        SHCT[i] = SHCT_INIT;
    }
    stat_hits   = 0;
    stat_misses = 0;
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
    // 2) find RRPV == MAX; if none, age all and retry
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++)
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        // age all
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
    if (way < LLC_WAYS && hit) {
        // HIT: promote and update SHCT
        stat_hits++;
        rrpv[set][way] = 0;
        uint32_t sig = b_signature[set][way];
        if (SHCT[sig] < SHCT_MAX) SHCT[sig]++;
        return;
    }
    // MISS: will fill 'way'
    stat_misses++;
    // if we're evicting a valid block that never hit, penalize its signature
    if (way < LLC_WAYS) {
        uint32_t old_sig = b_signature[set][way];
        // if it didn't get promoted (i.e., its counter never incremented to HOT),
        // we still decrement because it missed overall
        if (SHCT[old_sig] > 0) SHCT[old_sig]--;
    }
    // choose insertion RRPV based on new block's PC signature
    uint32_t new_sig = get_signature(PC, paddr);
    b_signature[set][way] = new_sig;
    // threshold: counter >=2 => hot insert
    if (SHCT[new_sig] >= 2) {
        rrpv[set][way] = 0;                  // hot insertion
    } else {
        rrpv[set][way] = MAX_RRPV - 1;       // cold insertion
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== SHiP‐SRRIP Final Stats ===\n";
    std::cout << "Hits   : " << stat_hits   << "\n";
    std::cout << "Misses : " << stat_misses << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[SHiP‐SRRIP] H:" << stat_hits
              << " M:" << stat_misses
              << std::endl;
}