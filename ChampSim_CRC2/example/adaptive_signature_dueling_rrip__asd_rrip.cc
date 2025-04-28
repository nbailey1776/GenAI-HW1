#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE       1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS       16

// RRIP parameters
static const uint8_t  MAX_RRPV       = 3;           // 2-bit RRPV
static const uint8_t  COLD_RRPV      = MAX_RRPV - 1;

// Idle-timeout: demote lines not touched in this many accesses
static const uint64_t IDLE_TIMEOUT   = 10000ULL;

// SHCT parameters (from SHiP)
static const uint32_t SHCT_SIZE      = 16384;       // must be power-of-two
static const uint8_t  SHCT_MAX       = 3;           // 2-bit saturating

// Set-dueling parameters (from DIP)
static const uint32_t DUEL_PERIOD    = 64;
static const uint32_t SRRIP_SAMPLE   = 0;            // set % period == 0
static const uint32_t BRRIP_SAMPLE   = 1;            // set % period == 1

// Replacement state
static uint8_t   rrpv      [LLC_SETS][LLC_WAYS];
static bool      valid     [LLC_SETS][LLC_WAYS];
static uint64_t  timestamp [LLC_SETS][LLC_WAYS];
static uint32_t  sigtable  [LLC_SETS][LLC_WAYS];
static bool      re_ref    [LLC_SETS][LLC_WAYS];

// Global predictors & counters
static uint8_t   SHCT      [SHCT_SIZE];
static uint64_t  global_counter;
static uint64_t  miss_srrip, miss_brrip;
static bool      use_srrip;    // follower sets use SRRIP if true

// Statistics
static uint64_t stat_hits, stat_misses, stat_bypass;

// Initialize replacement state
void InitReplacementState() {
    std::srand(0);
    global_counter = 0;
    miss_srrip     = miss_brrip     = 0;
    use_srrip      = true;
    stat_hits      = stat_misses     = stat_bypass = 0;
    // Initialize SHCT
    for (uint32_t i = 0; i < SHCT_SIZE; i++) {
        SHCT[i] = 0;
    }
    // Initialize per-line state
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
    uint32_t sig = (uint32_t)((PC ^ (PC >> 16)) & (SHCT_SIZE - 1));
    bool   high_conf = (SHCT[sig] & (1 << 1)) != 0;
    // Only bypass if NOT a writeback and PC is low confidence
    if (type != WRITEBACK && !high_conf) {
        stat_bypass++;
        return LLC_WAYS;
    }
    // 2) Fill empty way if any
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (!current_set[w].valid)
            return w;
    }
    // 3) Idle-timeout demotion: push stale lines to RRPV=MAX
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (valid[set][w] &&
            (global_counter - timestamp[set][w] >= IDLE_TIMEOUT)) {
            rrpv[set][w] = MAX_RRPV;
        }
    }
    // 4) Standard SRRIP victim search
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        }
        // Age all others
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
    // advance global clock
    global_counter++;

    // If bypassed
    if (way >= LLC_WAYS) {
        if (hit) stat_hits++;
        else     stat_misses++;
        return;
    }

    if (hit) {
        // On hit: promote to MRU and update SHCT
        stat_hits++;
        rrpv[set][way]      = 0;
        timestamp[set][way] = global_counter;
        uint32_t osig = sigtable[set][way];
        // saturating increment
        if (SHCT[osig] < SHCT_MAX)
            SHCT[osig]++;
        re_ref[set][way] = true;
    } else {
        // On miss: dynamic dueling + signature update
        stat_misses++;
        bool is_srrip_sample = ((set % DUEL_PERIOD) == SRRIP_SAMPLE);
        bool is_brrip_sample = ((set % DUEL_PERIOD) == BRRIP_SAMPLE);
        if (is_srrip_sample) miss_srrip++;
        else if (is_brrip_sample) miss_brrip++;
        // decide global winner
        use_srrip = (miss_brrip >= miss_srrip);

        // If evicting valid line, adjust its SHCT on cold eviction
        if (valid[set][way]) {
            uint32_t old_sig = sigtable[set][way];
            if (!re_ref[set][way] && SHCT[old_sig] > 0)
                SHCT[old_sig]--;
        }
        // Install new line
        valid[set][way]      = true;
        timestamp[set][way]  = global_counter;
        uint32_t newsig = (uint32_t)((PC ^ (PC >> 16)) & (SHCT_SIZE - 1));
        sigtable[set][way]   = newsig;
        re_ref[set][way]     = false;

        // Insertion logic:
        // 1) High-confidence PCs => MRU
        if (SHCT[newsig] & (1 << 1)) {
            rrpv[set][way] = 0;
        }
        else {
            // 2) Low-confidence: choose between SRRIP or BRRIP
            bool use_static = is_srrip_sample
                           || (!is_brrip_sample && use_srrip);
            if (use_static) {
                // SRRIP: always COLD insertion
                rrpv[set][way] = COLD_RRPV;
            } else {
                // BRRIP: cold most times, occasionally hot
                if ((std::rand() & 31) == 0)
                    rrpv[set][way] = 0;
                else
                    rrpv[set][way] = COLD_RRPV;
            }
        }
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== ASD-RRIP Final Stats ===\n";
    std::cout << "Hits           : " << stat_hits   << "\n";
    std::cout << "Misses         : " << stat_misses << "\n";
    std::cout << "Bypasses       : " << stat_bypass << "\n";
    std::cout << "Duel SRRIP miss: " << miss_srrip << "\n";
    std::cout << "Duel BRRIP miss: " << miss_brrip << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[ASD-RRIP] H:" << stat_hits
              << " M:" << stat_misses
              << " Byp:" << stat_bypass
              << " S-miss:" << miss_srrip
              << " B-miss:" << miss_brrip
              << std::endl;
}