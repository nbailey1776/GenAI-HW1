#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE        1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS        16

// RRIP parameters
static const uint8_t  MAX_RRPV        = 3;   // 2-bit RRPV [0..3]
static const uint8_t  BRRIP_RRPV      = MAX_RRPV - 1; // cold insertion

// DRRIP dueling parameters
static const uint32_t DIP_PERIOD      = 32;      // sample every 32 sets
static const uint32_t PSEL_BITS       = 10;
static const uint32_t PSEL_MAX        = ((1 << PSEL_BITS) - 1);
static const uint32_t PSEL_INIT       = (PSEL_MAX >> 1);

// Replacement state
static uint8_t   rrpv      [LLC_SETS][LLC_WAYS];
static bool      valid     [LLC_SETS][LLC_WAYS];
// Statistics
static uint64_t  stat_hits, stat_misses;
// Policy selector
static uint32_t  PSEL;

// Helpers
static inline bool is_leader_srrip(uint32_t set) {
    return ((set & (DIP_PERIOD-1)) == 0);
}
static inline bool is_leader_brrip(uint32_t set) {
    return ((set & (DIP_PERIOD-1)) == 1);
}

// Initialize replacement state
void InitReplacementState() {
    stat_hits   = 0;
    stat_misses = 0;
    PSEL        = PSEL_INIT;
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            valid[s][w] = false;
            rrpv[s][w]  = MAX_RRPV;
        }
    }
}

// Find victim in the set
uint32_t GetVictimInSet(
    uint32_t    cpu,
    uint32_t    set,
    const BLOCK *current_set,
    uint64_t    PC,
    uint64_t    paddr,
    uint32_t    type
) {
    // 1) Fill an empty way if available
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (!current_set[w].valid)
            return w;
    }
    // 2) Standard SRRIP victim search
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        }
        // age all lines
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] < MAX_RRPV)
                rrpv[set][w]++;
        }
    }
    return 0; // unreachable
}

// Update replacement state
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
    // On hit: reset RRPV and update PSEL if in a leader
    if (way < LLC_WAYS && hit) {
        stat_hits++;
        rrpv[set][way] = 0;
        if (is_leader_srrip(set)) {
            if (PSEL < PSEL_MAX) PSEL++;
        } else if (is_leader_brrip(set)) {
            if (PSEL > 0)      PSEL--;
        }
        return;
    }
    // Miss handling
    stat_misses++;
    // Determine which insertion policy to use
    bool use_srrip;
    if (is_leader_srrip(set)) {
        use_srrip = true;
    } else if (is_leader_brrip(set)) {
        use_srrip = false;
    } else {
        use_srrip = (PSEL >= PSEL_INIT);
    }
    // Install new line
    valid[set][way] = true;
    // Set RRPV based on chosen policy
    if (use_srrip) {
        rrpv[set][way] = 0;              // SRRIP: hot insertion
    } else {
        rrpv[set][way] = BRRIP_RRPV;     // BRRIP: mostly cold insertion
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== DRRIP Final Stats ===\n";
    std::cout << "Hits   : " << stat_hits   << "\n";
    std::cout << "Misses : " << stat_misses << "\n";
    std::cout << "PSEL   : " << PSEL        << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[DRRIP] H:" << stat_hits
              << " M:"   << stat_misses
              << " PSEL:" << PSEL
              << std::endl;
}