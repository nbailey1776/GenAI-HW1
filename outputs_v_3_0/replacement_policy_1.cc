#include "champsim_crc2.h"
#include <vector>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <iostream>

#define NUM_SET 2048
#define ASSOC 16

struct LineState {
    uint64_t freq;
    uint64_t last_access;
};

static LineState repl[NUM_SET][ASSOC];
static uint64_t global_clock;
static uint64_t stat_accesses, stat_hits, stat_misses, stat_evictions;

void InitReplacementState() {
    global_clock = 0;
    stat_accesses = stat_hits = stat_misses = stat_evictions = 0;
    for (uint32_t s = 0; s < NUM_SET; ++s) {
        for (uint32_t w = 0; w < ASSOC; ++w) {
            repl[s][w].freq = 0;
            repl[s][w].last_access = 0;
        }
    }
}

uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK* current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type) {
    uint32_t victim_way = 0;
    uint64_t min_freq = ULLONG_MAX;
    uint64_t min_time = ULLONG_MAX;
    for (uint32_t w = 0; w < ASSOC; ++w) {
        if (repl[set][w].freq < min_freq ||
            (repl[set][w].freq == min_freq && repl[set][w].last_access < min_time)) {
            min_freq = repl[set][w].freq;
            min_time = repl[set][w].last_access;
            victim_way = w;
        }
    }
    return victim_way;
}

void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way,
                            uint64_t paddr, uint64_t PC, uint64_t victim_addr,
                            uint32_t type, uint8_t hit) {
    global_clock++;
    stat_accesses++;
    if (hit) {
        stat_hits++;
        repl[set][way].freq++;
        repl[set][way].last_access = global_clock;
    } else {
        stat_misses++;
        if (repl[set][way].freq != 0 || repl[set][way].last_access != 0) {
            stat_evictions++;
        }
        repl[set][way].freq = 1;
        repl[set][way].last_access = global_clock;
    }
}

void PrintStats_Heartbeat() {
    std::cout << "LFU Heartbeat: accesses=" << stat_accesses
              << " hits=" << stat_hits
              << " misses=" << stat_misses
              << " evictions=" << stat_evictions
              << std::endl;
}

void PrintStats() {
    std::cout << "LFU Final Stats: accesses=" << stat_accesses
              << " hits=" << stat_hits
              << " misses=" << stat_misses
              << " evictions=" << stat_evictions
              << std::endl;
}