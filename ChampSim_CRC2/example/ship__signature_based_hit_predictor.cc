#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE       1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS       16

// RRIP parameters
static const uint8_t  MAX_RRPV    = 3;     // 2-bit RRPV [0..3]
static const uint8_t  RRPV_HOT    = 0;
static const uint8_t  RRPV_COLD   = MAX_RRPV;

// SHiP parameters
static const uint32_t SHT_SIZE    = 1024;  // entries in Signature History Table
static const uint8_t  SHT_MAX     = 7;     // 3-bit saturating counter: [0..7]
static const uint8_t  SHT_INIT    = 4;     // weakly “likely reusable” threshold
static const uint8_t  SHT_THRESHOLD = 4;   // on insert: counter >= threshold => hot

// Replacement state
static uint8_t   rrpv       [LLC_SETS][LLC_WAYS];
static bool      valid      [LLC_SETS][LLC_WAYS];
// Per-line reuse flag (set on hit, cleared at insertion)
static bool      reuse_bit  [LLC_SETS][LLC_WAYS];
// Per-line signature (low bits of PC)
static uint32_t  line_sig   [LLC_SETS][LLC_WAYS];
// Signature History Table
static uint8_t   SHT        [SHT_SIZE];

// Statistics
static uint64_t  stat_hits, stat_misses;

// Helpers
static inline uint32_t get_signature(uint64_t PC) {
    // simple low-bits hash
    return (uint32_t)( (PC >> 3) ^ (PC >> 13) ) & (SHT_SIZE - 1);
}

// Initialize replacement state
void InitReplacementState() {
    stat_hits   = 0;
    stat_misses = 0;
    // init RRIP arrays and reuse bits
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            valid[s][w]      = false;
            rrpv[s][w]       = MAX_RRPV;
            reuse_bit[s][w]  = false;
            line_sig[s][w]   = 0;
        }
    }
    // init SHT to weakly reusable
    for (uint32_t i = 0; i < SHT_SIZE; i++) {
        SHT[i] = SHT_INIT;
    }
}

// Find victim in the set (standard SRRIP search)
uint32_t GetVictimInSet(
    uint32_t    cpu,
    uint32_t    set,
    const BLOCK *current_set,
    uint64_t    PC,
    uint64_t    paddr,
    uint32_t    type
) {
    // 1) If there is an invalid way, use it
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (!current_set[w].valid)
            return w;
    }
    // 2) Otherwise find an RRPV == MAX_RRPV
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        }
        // 3) Age everyone
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] < MAX_RRPV)
                rrpv[set][w]++;
        }
    }
    // unreachable
    return 0;
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
    if (hit) {
        // On hit: credit a hit, reset RRPV, mark reuse
        stat_hits++;
        rrpv[set][way]     = RRPV_HOT;
        reuse_bit[set][way] = true;
        return;
    }
    // On miss: we are about to install into [set][way]
    stat_misses++;
    // 1) If replacing a valid block, update its signature counter
    if (valid[set][way]) {
        uint32_t sig = line_sig[set][way];
        if (reuse_bit[set][way]) {
            // block was reused at least once
            if (SHT[sig] < SHT_MAX) SHT[sig]++;
        } else {
            // dead block
            if (SHT[sig] > 0)      SHT[sig]--;
        }
    }
    // 2) Install new block
    valid[set][way]     = true;
    reuse_bit[set][way] = false;
    uint32_t sig        = get_signature(PC);
    line_sig[set][way]  = sig;
    // 3) Choose insertion RRPV based on predictor
    if (SHT[sig] >= SHT_THRESHOLD) {
        // likely to reuse => hot insertion
        rrpv[set][way] = RRPV_HOT;
    } else {
        // likely dead => cold insertion
        rrpv[set][way] = RRPV_COLD;
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== SHiP Final Stats ===\n";
    std::cout << "Hits   : " << stat_hits   << "\n";
    std::cout << "Misses : " << stat_misses << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[SHiP] H:" << stat_hits
              << " M:"   << stat_misses
              << std::endl;
}