#include <vector>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE       1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS       16

// RRIP parameters
static const uint8_t MAX_RRPV       = 3;    // 2-bit RRPV
static const uint8_t COLD_RRPV      = MAX_RRPV - 1;

// Idle timeout (in accesses) before forced demotion
static const uint64_t IDLE_TIMEOUT  = 10000ULL;

// Set-dueling parameters
static const uint32_t DUEL_PERIOD   = 64;   // mod value for sample sets
static const uint32_t SRRIP_SAMPLE  = 0;    // set % DUEL_PERIOD == 0
static const uint32_t BRRIP_SAMPLE  = 1;    // set % DUEL_PERIOD == 1

// Replacement state arrays
static uint8_t   rrpv[LLC_SETS][LLC_WAYS];
static bool      valid[LLC_SETS][LLC_WAYS];
static uint64_t  timestamp[LLC_SETS][LLC_WAYS];
static uint64_t  global_counter;

// Dueling state
static uint64_t  miss_srrip, miss_brrip;
static bool      use_srrip;  // true = follower sets use SRRIP, false = use BRRIP

// Statistics
static uint64_t stat_hits, stat_misses, stat_bypass;

void InitReplacementState() {
    std::srand(0);
    global_counter = 0;
    miss_srrip     = miss_brrip     = 0;
    use_srrip      = true;
    stat_hits      = stat_misses     = stat_bypass = 0;
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            rrpv[s][w]     = MAX_RRPV;
            valid[s][w]    = false;
            timestamp[s][w]= 0;
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
    // 1) Bypass all prefetches
    if (type == PREFETCH) {
        stat_bypass++;
        return LLC_WAYS;
    }
    // 2) Try to find an invalid way
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (!current_set[w].valid) {
            return w;
        }
    }
    // 3) Idle-timeout demotion
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (valid[set][w] &&
            (global_counter - timestamp[set][w] >= IDLE_TIMEOUT)) {
            rrpv[set][w] = MAX_RRPV;
        }
    }
    // 4) Standard SRRIP victim search
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV) {
                return w;
            }
        }
        // age everyone
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
    // global access counter
    global_counter++;

    // If we bypassed the cache entirely, way == LLC_WAYS
    if (way >= LLC_WAYS) {
        if (hit) stat_hits++;
        else      stat_misses++;
        return;
    }

    if (hit) {
        // On hit: set MRU
        stat_hits++;
        rrpv[set][way]      = 0;
        timestamp[set][way] = global_counter;
    } else {
        // On miss: choose insertion policy
        stat_misses++;
        bool is_srrip_sample = ((set % DUEL_PERIOD) == SRRIP_SAMPLE);
        bool is_brrip_sample = ((set % DUEL_PERIOD) == BRRIP_SAMPLE);
        // Track sample misses
        if (is_srrip_sample) {
            miss_srrip++;
        } else if (is_brrip_sample) {
            miss_brrip++;
        }
        // Recompute winner (lower misses wins)
        use_srrip = (miss_brrip >= miss_srrip);

        // Install the new line
        valid[set][way]      = true;
        timestamp[set][way]  = global_counter;
        // Determine which insertion policy to use for this set
        bool policy_srrip = is_srrip_sample
                         || (!is_brrip_sample && use_srrip);
        if (policy_srrip) {
            // SRRIP insertion: always COLD_RRPV
            rrpv[set][way] = COLD_RRPV;
        } else {
            // BRRIP insertion: mostly COLD, occasionally HOT
            // e.g., 1/32 probability to insert as 0
            if ((std::rand() & 31) == 0)
                rrpv[set][way] = 0;
            else
                rrpv[set][way] = COLD_RRPV;
        }
    }
}

void PrintStats() {
    std::cout << "=== DIP-RRIP Final Stats ===\n";
    std::cout << "Hits     : " << stat_hits   << "\n";
    std::cout << "Misses   : " << stat_misses << "\n";
    std::cout << "Bypasses : " << stat_bypass << "\n";
    std::cout << "Duel SRRIP misses: " << miss_srrip << "\n";
    std::cout << "Duel BRRIP misses: " << miss_brrip << "\n";
}

void PrintStats_Heartbeat() {
    std::cout << "[DIP-RRIP] H:" << stat_hits
              << " M:" << stat_misses
              << " Byp:" << stat_bypass
              << " S-miss:" << miss_srrip
              << " B-miss:" << miss_brrip
              << std::endl;
}