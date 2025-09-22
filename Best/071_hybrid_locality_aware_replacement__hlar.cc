#include <vector>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include "../inc/champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS (NUM_CORE * 2048)
#define LLC_WAYS 16

// Parameters
const int PHASE_WINDOW = 512;
const int DECAY_FACTOR = 2;
const int MAX_WEIGHT = 10;
const int INITIAL_WEIGHT = 5;
const int REUSE_DISTANCE_THRESHOLD = 10;

// Replacement state
struct CacheLine {
    uint64_t tag;
    uint64_t last_access;
    uint64_t access_count;
    int locality_score;
    int reuse_distance;
};

std::vector<std::vector<CacheLine>> cache(LLC_SETS, std::vector<CacheLine>(LLC_WAYS));
int phase_counter = 0;
int spatial_weight = INITIAL_WEIGHT;
int temporal_weight = INITIAL_WEIGHT;

void InitReplacementState() {
    for (auto& set : cache) {
        for (auto& line : set) {
            line.tag = 0;
            line.last_access = 0;
            line.access_count = 0;
            line.locality_score = 0;
            line.reuse_distance = 0;
        }
    }
}

uint32_t GetVictimInSet(
    uint32_t cpu,
    uint32_t set,
    const BLOCK *current_set,
    uint64_t PC,
    uint64_t paddr,
    uint32_t type
) {
    uint32_t victim = 0;
    int min_score = INT32_MAX;

    for (uint32_t i = 0; i < LLC_WAYS; ++i) {
        if (current_set[i].valid == 0) {
            return i; // Empty line
        }
        int score = cache[set][i].locality_score;
        if (score < min_score) {
            min_score = score;
            victim = i;
        }
    }

    return victim;
}

void UpdateReplacementState(
    uint32_t cpu,
    uint32_t set,
    uint32_t way,
    uint64_t paddr,
    uint64_t PC,
    uint64_t victim_addr,
    uint32_t type,
    uint8_t hit
) {
    uint64_t tag = paddr >> 6;
    auto& line = cache[set][way];
    line.tag = tag;
    line.last_access = phase_counter;
    line.access_count = hit ? line.access_count + 1 : 1;

    // Calculate reuse distance
    line.reuse_distance = phase_counter - line.last_access;

    // Update locality score with reuse distance consideration
    int spatial_component = (paddr / 64) % LLC_WAYS;
    int temporal_component = line.access_count;
    int reuse_component = (line.reuse_distance < REUSE_DISTANCE_THRESHOLD) ? 1 : -1;
    line.locality_score = spatial_weight * spatial_component + temporal_weight * temporal_component + reuse_component;

    phase_counter++;

    // Adaptive aging and phase detection
    if (phase_counter % PHASE_WINDOW == 0) {
        for (auto& set : cache) {
            for (auto& line : set) {
                line.access_count = std::max(line.access_count / DECAY_FACTOR, uint64_t(1));
                line.locality_score = std::max(line.locality_score - 1, 0);
            }
        }
        // Adjust weights based on observed phase changes and cache misses
        if (hit == 0) {
            if (spatial_weight < MAX_WEIGHT) spatial_weight++;
            if (temporal_weight < MAX_WEIGHT) temporal_weight++;
        } else {
            if (spatial_weight > 1) spatial_weight--;
            if (temporal_weight > 1) temporal_weight--;
        }
    }
}

void PrintStats() {
    std::cout << "HLAR Policy Statistics" << std::endl;
}

void PrintStats_Heartbeat() {
    // Periodic statistics can be printed here
}