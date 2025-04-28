#include <vector>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE       1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS       16

// RRIP parameters
static const uint8_t  MAX_RRPV       = 3;      // 2-bit RRPV
static const uint8_t  COLD_RRPV      = MAX_RRPV - 1;

// Idle timeout (in accesses) before forced demotion
static const uint64_t IDLE_TIMEOUT   = 10000ULL;

// SHCT parameters
static const uint32_t SHCT_SIZE      = 16384;  // power-of-two
static const uint8_t  SHCT_MAX       = 3;      // 2-bit counters

// Replacement state
static uint8_t   rrpv     [LLC_SETS][LLC_WAYS];
static bool      valid    [LLC_SETS][LLC_WAYS];
static uint64_t  timestamp[LLC_SETS][LLC_WAYS];
static uint32_t  sigtable [LLC_SETS][LLC_WAYS];
static bool      re_ref   [LLC_SETS][LLC_WAYS];
static uint8_t   SHCT     [SHCT_SIZE];
static uint64_t  global_counter;

// Statistics
static uint64_t stat_hits, stat_misses, stat_bypass;

void InitReplacementState() {
    std::srand(0);
    global_counter = 0;
    stat_hits      = stat_misses = stat_bypass = 0;
    // Initialize caches and SHCT
    for (uint32_t i = 0; i < SHCT_SIZE; i++) {
        SHCT[i] = 0;
    }
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            valid[s][w]     = false;
            rrpv [s][w]     = MAX_RRPV;
            timestamp[s][w] = 0;
            sigtable[s][w]  = 0;
            re_ref[s][w]    = false;
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
    // 1) Bypass prefetches
    if (type == PREFETCH) {
        stat_bypass++;
        return LLC_WAYS;
    }
    // 2) Look for invalid way
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (!current_set[w].valid)
            return w;
    }
    // 3) Idle-timeout demotion
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (valid[set][w] &&
            (global_counter - timestamp[set][w] >= IDLE_TIMEOUT)) {
            rrpv[set][w] = MAX_RRPV;
        }
    }
    // 4) SRRIP victim search
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        }
        // Age everyone otherwise
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
    // Global tick
    global_counter++;

    // Bypassed access
    if (way >= LLC_WAYS) {
        if (hit) stat_hits++;
        else      stat_misses++;
        return;
    }

    if (hit) {
        // ===== On Hit =====
        stat_hits++;
        // 1) Update RRPV/timestamp
        rrpv[set][way]      = 0;
        timestamp[set][way] = global_counter;
        // 2) Update SHCT: saturating increment
        uint32_t sig = sigtable[set][way];
        if (SHCT[sig] < SHCT_MAX)
            SHCT[sig]++;
        // 3) Mark as re-referenced
        re_ref[set][way] = true;
    }
    else {
        // ===== On Miss =====
        stat_misses++;
        // 1) If evicting a valid block, adjust its SHCT
        if (valid[set][way]) {
            uint32_t old_sig = sigtable[set][way];
            // If the line was never re-referenced since insertion, decrement
            if (!re_ref[set][way] && SHCT[old_sig] > 0) {
                SHCT[old_sig]--;
            }
        }
        // 2) Install new line
        valid[set][way]      = true;
        timestamp[set][way]  = global_counter;
        // 3) Compute PC signature
        uint32_t newsig = (uint32_t)(PC ^ (PC >> 16)) & (SHCT_SIZE - 1);
        sigtable[set][way]  = newsig;
        re_ref[set][way]    = false;
        // 4) Insertion decision based on SHCT MSB
        //    MSB=1 => likely reused => RRPV=0; else RRPV=COLD
        if (SHCT[newsig] & (1 << 1))
            rrpv[set][way] = 0;
        else
            rrpv[set][way] = COLD_RRPV;
    }
}

void PrintStats() {
    std::cout << "=== SER-RRIP Final Stats ===\n";
    std::cout << "Hits     : " << stat_hits   << "\n";
    std::cout << "Misses   : " << stat_misses << "\n";
    std::cout << "Bypasses : " << stat_bypass << "\n";
}

void PrintStats_Heartbeat() {
    std::cout << "[SER-RRIP] H:" << stat_hits
              << " M:"    << stat_misses
              << " Byp:"  << stat_bypass
              << std::endl;
}