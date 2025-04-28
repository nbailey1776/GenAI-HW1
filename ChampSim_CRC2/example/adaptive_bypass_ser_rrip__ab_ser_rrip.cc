#include <vector>
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include "../inc/champsim_crc2.h"

#define NUM_CORE       1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS       16

// RRIP parameters
static const uint8_t  MAX_RRPV       = 3;            // 2-bit RRPV
static const uint8_t  COLD_RRPV      = MAX_RRPV - 1; // 2

// Idle timeout (in accesses) before forced demotion
static const uint64_t IDLE_TIMEOUT   = 10000ULL;

// SHCT parameters
static const uint32_t SHCT_SIZE      = 16384;        // power-of-two
static const uint8_t  SHCT_MAX       = 3;            // 2-bit counters

// Replacement state
static uint8_t   rrpv      [LLC_SETS][LLC_WAYS];
static bool      valid     [LLC_SETS][LLC_WAYS];
static uint64_t  timestamp [LLC_SETS][LLC_WAYS];
static uint32_t  sigtable  [LLC_SETS][LLC_WAYS];
static bool      re_ref    [LLC_SETS][LLC_WAYS];
static uint8_t   SHCT      [SHCT_SIZE];
static uint64_t  global_counter;

// Statistics
static uint64_t stat_hits, stat_misses, stat_bypass;

void InitReplacementState() {
    std::srand(0);
    global_counter = stat_hits = stat_misses = stat_bypass = 0;
    // Clear SHCT
    for (uint32_t i = 0; i < SHCT_SIZE; i++)
        SHCT[i] = 0;
    // Initialize each way
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            valid    [s][w] = false;
            rrpv     [s][w] = MAX_RRPV;
            timestamp[s][w] = 0;
            sigtable [s][w] = 0;
            re_ref   [s][w] = false;
        }
    }
}

uint32_t GetVictimInSet(
    uint32_t    cpu,
    uint32_t    set,
    const BLOCK *current_set,
    uint64_t    PC,
    uint64_t    paddr,
    uint32_t    type
) {
    // 1) Always bypass prefetches
    if (type == PREFETCH) {
        stat_bypass++;
        return LLC_WAYS;
    }
    // 2) Signature-guided bypass: if SHCT==0, do not insert
    uint32_t sig = (uint32_t)(PC ^ (PC >> 16)) & (SHCT_SIZE - 1);
    if (SHCT[sig] == 0) {
        stat_bypass++;
        return LLC_WAYS;
    }
    // 3) If there is an invalid way, choose it
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (!current_set[w].valid)
            return w;
    }
    // 4) Idle-timeout demotion
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (valid[set][w] &&
            (global_counter - timestamp[set][w] >= IDLE_TIMEOUT)) {
            rrpv[set][w] = MAX_RRPV;
        }
    }
    // 5) SRRIP victim selection
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
}

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
    // advance global time
    global_counter++;

    // bypassed => just count hits/misses
    if (way >= LLC_WAYS) {
        if (hit) stat_hits++;
        else     stat_misses++;
        return;
    }

    if (hit) {
        // ===== On Hit =====
        stat_hits++;
        // reset RRPV to MRU
        rrpv[set][way]      = 0;
        timestamp[set][way] = global_counter;
        // strengthen predictor
        uint32_t old_sig = sigtable[set][way];
        if (SHCT[old_sig] < SHCT_MAX)
            SHCT[old_sig]++;
        re_ref[set][way] = true;
    }
    else {
        // ===== On Miss and Alloc =====
        stat_misses++;
        // 1) If evicting a valid block, update its SHCT
        if (valid[set][way]) {
            uint32_t old_sig = sigtable[set][way];
            if (!re_ref[set][way] && SHCT[old_sig] > 0)
                SHCT[old_sig]--;
        }
        // 2) Allocate the new block
        valid[set][way]      = true;
        timestamp[set][way]  = global_counter;
        // compute new signature
        uint32_t newsig = (uint32_t)(PC ^ (PC >> 16)) & (SHCT_SIZE - 1);
        sigtable[set][way]  = newsig;
        re_ref[set][way]    = false;
        // 3) Insert based on SHCT counter:
        //    0 => (shouldn't happen, bypassed earlier)
        //    1 => cold insert, RRPV = COLD_RRPV
        //   ≥2 => MRU insert, RRPV = 0
        if (SHCT[newsig] >= 2)
            rrpv[set][way] = 0;
        else
            rrpv[set][way] = COLD_RRPV;
    }
}

void PrintStats() {
    std::cout << "=== AB-SER-RRIP Final Stats ===\n";
    std::cout << "Hits     : " << stat_hits   << "\n";
    std::cout << "Misses   : " << stat_misses << "\n";
    std::cout << "Bypasses : " << stat_bypass << "\n";
}

void PrintStats_Heartbeat() {
    std::cout << "[AB-SER-RRIP] H:" << stat_hits
              << " M:"    << stat_misses
              << " Byp:"  << stat_bypass
              << std::endl;
}