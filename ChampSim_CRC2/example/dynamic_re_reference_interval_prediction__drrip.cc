#include <vector>
#include <cstdint>
#include <iostream>
#include <cstdlib>           // for rand()
#include "../inc/champsim_crc2.h"

#define NUM_CORE        1
#define LLC_SETS        (NUM_CORE * 2048)
#define LLC_WAYS        16

// RRIP parameters
static const uint8_t MAX_RRPV     = 3;    // 2‐bit RRPV [0..3]
static const uint8_t RRPV_HOT     = 0;
static const uint8_t RRPV_COLD    = MAX_RRPV;

// DRRIP parameters
static const uint16_t PSEL_MAX          = 1023;       // 10‐bit saturating counter
static const uint16_t PSEL_INIT         = (PSEL_MAX/2);
static const uint32_t BRRIP_PROBABILITY = 32;         // 1/32 chance hot insert

// Replacement state
static uint8_t   rrpv      [LLC_SETS][LLC_WAYS];
// Set‐dueling counter
static uint16_t  psel;
// Statistics
static uint64_t  stat_hits, stat_misses;

// Helper to pick policy for a set:
//   0 = SRRIP, 1 = BRRIP
static inline uint8_t select_policy(uint32_t set) {
    // Leader sets: set%32==0 → always SRRIP; set%32==1 → always BRRIP
    if ((set & 0x1f) == 0)       return 0;  // SRRIP leader
    else if ((set & 0x1f) == 1)  return 1;  // BRRIP leader
    // Followers: choose based on PSEL
    return (psel >= PSEL_INIT) ? 0 : 1;
}

// Initialize replacement state
void InitReplacementState() {
    stat_hits   = 0;
    stat_misses = 0;
    psel        = PSEL_INIT;
    // Initialize all lines to cold (max RRPV)
    for (uint32_t s = 0; s < LLC_SETS; s++) {
        for (uint32_t w = 0; w < LLC_WAYS; w++) {
            rrpv[s][w] = MAX_RRPV;
        }
    }
    // Seed random for BRRIP sampling
    std::srand(0xC0FFEE);
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
    if (hit) {
        // On hit: perfect reuse, make hot
        stat_hits++;
        rrpv[set][way] = RRPV_HOT;
        return;
    }
    // On miss: choose replacement style and update PSEL if leader
    stat_misses++;
    uint32_t mod = set & 0x1f;
    if (mod == 0) {
        // SRRIP leader saw a miss → bias away from SRRIP
        if (psel > 0) psel--;
    } else if (mod == 1) {
        // BRRIP leader saw a miss → bias away from BRRIP
        if (psel < PSEL_MAX) psel++;
    }
    // Now insert using chosen policy
    uint8_t policy = select_policy(set);
    if (policy == 0) {
        // SRRIP: always insert with RRPV = MAX_RRPV-1
        rrpv[set][way] = MAX_RRPV - 1;
    } else {
        // BRRIP: mostly cold, occasionally hot
        if ((std::rand() & (BRRIP_PROBABILITY - 1)) == 0)
            rrpv[set][way] = RRPV_HOT;
        else
            rrpv[set][way] = RRPV_COLD;
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    std::cout << "=== DRRIP Final Stats ===\n";
    std::cout << "Hits   : " << stat_hits   << "\n";
    std::cout << "Misses : " << stat_misses << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[DRRIP] H:" << stat_hits
              << " M:"   << stat_misses
              << std::endl;
}