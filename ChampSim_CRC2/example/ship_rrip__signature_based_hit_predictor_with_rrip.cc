#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE     1
#define LLC_SETS     (NUM_CORE * 2048)
#define LLC_WAYS     16

// SRRIP parameters
static const uint8_t MAX_RRPV = 3;       // 2-bit RRPV: 0..3

// SHiP parameters
#define SHCT_SIZE        1024            // number of signatures
#define SHCT_CTR_BITS    3               // 3-bit saturating counters
#define SHCT_CTR_MAX     ((1 << SHCT_CTR_BITS) - 1)
#define SHCT_THRESHOLD   (SHCT_CTR_MAX >> 1)

static uint8_t  rrpv   [LLC_SETS][LLC_WAYS];
static bool     valid  [LLC_SETS][LLC_WAYS];
static bool     reused [LLC_SETS][LLC_WAYS];
static uint16_t sig    [LLC_SETS][LLC_WAYS];   // PC signature per block
static uint8_t  SHCT   [SHCT_SIZE];            // signature counters

// Stats
static uint64_t stat_hits   = 0;
static uint64_t stat_misses = 0;

// Initialize replacement state
void InitReplacementState() {
    // Initialize RRPVs, valid bits, reuse bits
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            rrpv[s][w]   = MAX_RRPV;
            valid[s][w]  = false;
            reused[s][w] = false;
            sig[s][w]    = 0;
        }
    }
    // Initialize SHCT to half‐full
    for (int i = 0; i < SHCT_SIZE; i++) {
        SHCT[i] = SHCT_THRESHOLD;
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
    // 1) check for invalid (free) way
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (! current_set[w].valid)
            return w;
    }
    // 2) otherwise, find RRPV == MAX_RRPV
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        }
        // age everyone
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] < MAX_RRPV)
                rrpv[set][w]++;
        }
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
    if (hit) {
        // On hit: count, promote to MRU, mark reused
        stat_hits++;
        rrpv[set][way]   = 0;
        reused[set][way] = true;
    } else {
        // On miss: handle eviction learning, then insertion
        stat_misses++;
        // 1) Eviction learning: only if victim was valid
        if (valid[set][way]) {
            uint16_t old_sig = sig[set][way];
            if (reused[set][way]) {
                // block was reused => strengthen prediction
                if (SHCT[old_sig] < SHCT_CTR_MAX)
                    SHCT[old_sig]++;
            } else {
                // block never reused => weaken
                if (SHCT[old_sig] > 0)
                    SHCT[old_sig]--;
            }
        }
        // 2) Compute new signature from PC
        uint16_t new_sig = (uint16_t)(PC) & (SHCT_SIZE - 1);
        sig[set][way]    = new_sig;
        // reset reuse and mark valid
        reused[set][way] = false;
        valid[set][way]  = true;
        // 3) Decide insertion RRPV by SHCT prediction
        if (SHCT[new_sig] > SHCT_THRESHOLD) {
            // predicted hot
            rrpv[set][way] = 0;
        } else {
            // predicted cold
            rrpv[set][way] = MAX_RRPV - 1;
        }
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== SHiP‐RRIP Final Stats ===\n";
    std::cout << "Hits   : " << stat_hits   << "\n";
    std::cout << "Misses : " << stat_misses << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[SHiP‐RRIP] H:" << stat_hits
              << " M:" << stat_misses
              << std::endl;
}