#include "../inc/champsim_crc2.h"
#include <vector>
#include <cstdint>
#include <iostream>
using std::cout;
using std::endl;

#define NUM_CORE 1
#define LLC_SETS (NUM_CORE * 2048)
#define LLC_WAYS 16

// SRRIP parameters
static uint8_t rrpv[LLC_SETS][LLC_WAYS];
static const uint8_t MAX_RRPV = 3;

// SHCT (Signature History Counter Table) parameters
static const size_t SHCT_SIZE = 1024;           // 2^10 entries
static uint8_t SHCT[SHCT_SIZE];                 // 2-bit saturating counters [0..3]

// Per-line metadata
static uint16_t line_sig[LLC_SETS][LLC_WAYS];   // PC signature stored on fill
static bool     line_reused[LLC_SETS][LLC_WAYS]; // was line hit after fill?

// Statistics
static uint64_t stat_hits     = 0;
static uint64_t stat_misses   = 0;
static uint64_t stat_bypasses = 0;

// Helper: extract PC signature
static inline uint16_t
GetPCSig(uint64_t PC) {
    return (PC >> 2) & (SHCT_SIZE - 1);
}

// Initialize replacement state
void InitReplacementState() {
    // Initialize SRRIP RRPVs to MAX
    for (uint32_t s = 0; s < LLC_SETS; s++)
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            rrpv[s][w]        = MAX_RRPV;
            line_sig[s][w]    = 0;
            line_reused[s][w] = false;
        }
    // Initialize SHCT to weakly not reused (1)
    for (size_t i = 0; i < SHCT_SIZE; i++)
        SHCT[i] = 1;
    stat_hits = stat_misses = stat_bypasses = 0;
}

// Find victim (or bypass)
uint32_t GetVictimInSet(
    uint32_t    cpu,
    uint32_t    set,
    const BLOCK *current_set,
    uint64_t    PC,
    uint64_t    paddr,
    uint32_t    type
) {
    // 1) Bypass cold streams: only LOAD/RFO/PREFETCH, not WRITEBACK
    if ( (type == LOAD || type == RFO || type == PREFETCH) ) {
        uint16_t sig = GetPCSig(PC);
        if (SHCT[sig] == 0) {
            // strongly not reused => bypass
            stat_bypasses++;
            return LLC_WAYS;
        }
    }
    // 2) Fill empty way if available
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
        if (! current_set[w].valid) {
            return w;
        }
    }
    // 3) SRRIP eviction: find way with RRPV == MAX_RRPV
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] == MAX_RRPV) {
                return w;
            }
        }
        // age all lines
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            if (rrpv[set][w] < MAX_RRPV)
                rrpv[set][w]++;
        }
    }
}

// Update replacement state on hit or fill
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
    // 1) Bypassed fill => no update
    if (way == LLC_WAYS) return;

    uint16_t sig = GetPCSig(PC);

    if (hit) {
        // On hit: promote to RRPV=0 and mark reused
        stat_hits++;
        rrpv[set][way]     = 0;
        line_reused[set][way] = true;
    }
    else {
        // On miss fill: this way just evicted something (or was empty)
        stat_misses++;
        // If valid, update SHCT from the victim metadata
        // (the way we are about to overwrite)
        // victim_addr tells us which block was evicted
        // but we index by 'way'
        // We use line_sig & line_reused from the old occupant
        uint16_t old_sig = line_sig[set][way];
        bool     was_reused = line_reused[set][way];
        if (was_reused) {
            if (SHCT[old_sig] < 3) SHCT[old_sig]++;
        } else {
            if (SHCT[old_sig] > 0) SHCT[old_sig]--;
        }
        // Now install the new line
        line_sig[set][way]    = sig;
        line_reused[set][way] = false;
        // Set insertion RRPV based on new line's predictor
        // hot => 0 (immediate MRU), cold => MAX_RRPV-1
        if (SHCT[sig] >= 2) {
            rrpv[set][way] = 0;
        } else {
            rrpv[set][way] = MAX_RRPV - 1;
        }
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    cout << "=== ASHR Final Stats ===" << endl;
    cout << "Hits     : " << stat_hits     << endl;
    cout << "Misses   : " << stat_misses   << endl;
    cout << "Bypasses : " << stat_bypasses << endl;
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    cout << "[ASHR] H:" << stat_hits
         << " M:" << stat_misses
         << " Bp:" << stat_bypasses
         << endl;
}