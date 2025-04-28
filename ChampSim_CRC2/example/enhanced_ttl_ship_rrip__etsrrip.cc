#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE        1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS        16

// RRIP parameters
static const uint8_t  MAX_RRPV       = 3;           // 2-bit RRPV
static const uint8_t  COLD_RRPV      = MAX_RRPV - 1;

// SHCT (from SHiP)
static const uint32_t SHCT_SIZE      = 16384;       // must be power-of-two
static const uint8_t  SHCT_MAX       = 3;           // 2-bit saturating

// TTL‐based aging
static const uint32_t TTL_DECR_PERIOD= 100000;      // decrement all TTLs every N refs
static const uint32_t INIT_TTL_HOT   =  5000;       // large TTL for high-confidence
static const uint32_t INIT_TTL_COLD  =  1000;       // small TTL for low-confidence

// Replacement state
static uint8_t   rrpv      [LLC_SETS][LLC_WAYS];
static bool      valid     [LLC_SETS][LLC_WAYS];
static uint32_t  sigtable  [LLC_SETS][LLC_WAYS];
static bool      re_ref    [LLC_SETS][LLC_WAYS];
static uint32_t  ttl       [LLC_SETS][LLC_WAYS];

// Global predictors & counters
static uint8_t   SHCT      [SHCT_SIZE];
static uint64_t  global_refs;
static uint64_t  stat_hits, stat_misses, stat_bypass;

// Helpers
static inline uint32_t MakeSignature(uint64_t PC) {
    return (uint32_t)((PC ^ (PC >> 16)) & (SHCT_SIZE - 1));
}

// Initialize replacement state
void InitReplacementState() {
    std::srand(0);
    global_refs   = 0;
    stat_hits     = stat_misses = stat_bypass = 0;
    // Init SHCT
    for (uint32_t i = 0; i < SHCT_SIZE; i++) {
        SHCT[i] = 0;
    }
    // Init per-line state
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            valid[s][w]    = false;
            rrpv [s][w]    = MAX_RRPV;
            sigtable[s][w] = 0;
            re_ref  [s][w] = false;
            ttl     [s][w] = 0;
        }
    }
}

// Periodic TTL aging
static void AgeAllTTLs() {
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (ttl[s][w] > 0) {
                ttl[s][w]--;
                if (ttl[s][w] == 0) {
                    // Stale: force maximal RRPV
                    rrpv[s][w] = MAX_RRPV;
                }
            }
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
    // 1) Signature-based bypass for cold loads/prefetch/RFO
    uint32_t sig = MakeSignature(PC);
    bool   high_conf = (SHCT[sig] >= (SHCT_MAX/2 + 1));
    if (type != WRITEBACK && !high_conf) {
        stat_bypass++;
        return LLC_WAYS;  // signal bypass
    }

    // 2) Fill empty way if available
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (!current_set[w].valid)
            return w;
    }

    // 3) Periodic global TTL aging
    if ((++global_refs % TTL_DECR_PERIOD) == 0) {
        AgeAllTTLs();
    }

    // 4) Standard SRRIP victim search (stale lines have been demoted via TTL)
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        }
        // age all
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
    // If bypassed
    if (way >= LLC_WAYS) {
        if (hit) stat_hits++;
        else     stat_misses++;
        return;
    }

    uint32_t sig = MakeSignature(PC);

    if (hit) {
        // On hit: promote to MRU, refresh TTL, update SHCT
        stat_hits++;
        rrpv[set][way]      = 0;
        re_ref[set][way]    = true;
        // strengthen PC confidence
        if (SHCT[sig] < SHCT_MAX) SHCT[sig]++;
        // refresh TTL to HOT
        ttl[set][way]       = INIT_TTL_HOT;
    }
    else {
        // On miss: possibly weaken SHCT for the evicted line
        stat_misses++;
        if (valid[set][way]) {
            uint32_t old_sig = sigtable[set][way];
            if (!re_ref[set][way] && SHCT[old_sig] > 0)
                SHCT[old_sig]--;
        }
        // Install new line
        valid[set][way]      = true;
        sigtable[set][way]   = sig;
        re_ref[set][way]     = false;
        // Decide insertion
        bool new_high = (SHCT[sig] >= (SHCT_MAX/2 + 1));
        if (new_high) {
            // MRU + long TTL
            rrpv[set][way]  = 0;
            ttl[set][way]   = INIT_TTL_HOT;
        } else {
            // Cold insertion + short TTL
            rrpv[set][way]  = COLD_RRPV;
            ttl[set][way]   = INIT_TTL_COLD;
        }
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== ETSRRIP Final Stats ===\n";
    std::cout << "Hits     : " << stat_hits   << "\n";
    std::cout << "Misses   : " << stat_misses << "\n";
    std::cout << "Bypasses : " << stat_bypass << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[ETSRRIP] H:" << stat_hits
              << " M:"   << stat_misses
              << " Bp:"  << stat_bypass
              << std::endl;
}