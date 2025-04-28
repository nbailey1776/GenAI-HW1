#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE      1
#define LLC_SETS     (NUM_CORE * 2048)
#define LLC_WAYS      16

// SRRIP parameters
static const uint8_t MAX_RRPV = 3;
static uint8_t rrpv[LLC_SETS][LLC_WAYS];

// Set‐dueling parameters
static const int   LEADER_SETS = 64;       // number of leader sets per policy
static const int   DUEL_PERIOD = LEADER_SETS * 2;
static const int   PSEL_MAX    = 1023;     // 10‐bit counter
static int         PSEL;                  // saturating counter

// Stats
static uint64_t stat_hits   = 0;
static uint64_t stat_misses = 0;

// Helpers: identify set type
static inline bool is_leader_lru(uint32_t set) {
    int idx = set % DUEL_PERIOD;
    return (idx < LEADER_SETS);
}
static inline bool is_leader_srrip(uint32_t set) {
    int idx = set % DUEL_PERIOD;
    return (idx >= LEADER_SETS && idx < 2*LEADER_SETS);
}
static inline bool use_lru_insert(uint32_t set) {
    // Leader sets override followers
    if (is_leader_lru(set))    return true;
    if (is_leader_srrip(set))  return false;
    // Follower sets choose based on PSEL
    return (PSEL > (PSEL_MAX/2));
}

// Initialize replacement state
void InitReplacementState() {
    // Initialize all RRPVs to MAX
    for (uint32_t s = 0; s < LLC_SETS; s++)
        for (uint32_t w = 0; w < LLC_WAYS; w++)
            rrpv[s][w] = MAX_RRPV;
    // Init PSEL to middle
    PSEL = PSEL_MAX / 2;
    stat_hits = stat_misses = 0;
}

// Find victim (standard SRRIP aging)
uint32_t GetVictimInSet(
    uint32_t    cpu,
    uint32_t    set,
    const BLOCK *current_set,
    uint64_t    PC,
    uint64_t    paddr,
    uint32_t    type
) {
    // 1) free way?
    for (uint32_t w = 0; w < LLC_WAYS; w++)
        if (! current_set[w].valid)
            return w;
    // 2) find RRPV == MAX; if none, age all and retry
    while (true) {
        for (uint32_t w = 0; w < LLC_WAYS; w++)
            if (rrpv[set][w] == MAX_RRPV)
                return w;
        for (uint32_t w = 0; w < LLC_WAYS; w++)
            if (rrpv[set][w] < MAX_RRPV)
                rrpv[set][w]++;
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
    // standard SRRIP hit/promotion
    if (way < LLC_WAYS) {
        if (hit) {
            stat_hits++;
            // hit promotion
            rrpv[set][way] = 0;
            return;
        }
        // miss: we will fill/insert here
        stat_misses++;
        // adjust PSEL on misses in leader sets
        if (is_leader_lru(set)) {
            // LRU leader missed => penalize LRU => move PSEL down
            if (PSEL > 0) PSEL--;
        }
        else if (is_leader_srrip(set)) {
            // SRRIP leader missed => penalize SRRIP => move PSEL up
            if (PSEL < PSEL_MAX) PSEL++;
        }
        // now choose insertion RRPV
        if (use_lru_insert(set)) {
            // LRU‐style: most recently used
            rrpv[set][way] = 0;
        } else {
            // SRRIP‐style: nearly cold
            rrpv[set][way] = MAX_RRPV - 1;
        }
    }
}

// Print end‐of‐simulation statistics
void PrintStats() {
    std::cout << "=== DIP‐SRRIP Final Stats ===\n";
    std::cout << "Hits   : " << stat_hits   << "\n";
    std::cout << "Misses : " << stat_misses << "\n";
    std::cout << "PSEL   : " << PSEL        << "\n";
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    std::cout << "[DIP‐SRRIP] H:" << stat_hits
              << " M:" << stat_misses
              << " PSEL:" << PSEL
              << std::endl;
}