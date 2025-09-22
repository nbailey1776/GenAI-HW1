#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS (NUM_CORE * 2048)
#define LLC_WAYS 16
#define PHASE_HISTORY_LENGTH 8

struct LineReplacementState {
    uint64_t temporal_score;
    uint64_t spatial_score;
};

std::vector<std::vector<LineReplacementState>> cache_state(LLC_SETS, std::vector<LineReplacementState>(LLC_WAYS));
std::vector<uint8_t> phase_history(LLC_SETS, 0);

// Initialize replacement state
void InitReplacementState() {
    for (auto &set : cache_state) {
        for (auto &line : set) {
            line.temporal_score = 0;
            line.spatial_score = 0;
        }
    }
    std::fill(phase_history.begin(), phase_history.end(), 0);
}

// Find victim in the set
uint32_t GetVictimInSet(
    uint32_t cpu,
    uint32_t set,
    const BLOCK *current_set,
    uint64_t PC,
    uint64_t paddr,
    uint32_t type
) {
    uint32_t victim = 0;
    uint64_t min_score = UINT64_MAX;

    for (uint32_t way = 0; way < LLC_WAYS; ++way) {
        if (current_set[way].valid == 0) {
            return way; // Prefer invalid blocks
        }
        uint64_t score = cache_state[set][way].temporal_score + cache_state[set][way].spatial_score;
        if (score < min_score) {
            min_score = score;
            victim = way;
        }
    }

    return victim;
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
    uint8_t hit
) {
    if (hit) {
        cache_state[set][way].temporal_score += 2;
    } else {
        cache_state[set][way].temporal_score = 1;
    }

    if (type == 1) { // Spatial access
        cache_state[set][way].spatial_score += 3;
    } else {
        cache_state[set][way].spatial_score = 1;
    }

    // Update phase history
    phase_history[set] = (phase_history[set] << 1) | (type & 1);
    phase_history[set] &= (1 << PHASE_HISTORY_LENGTH) - 1;

    // Adjust scores based on phase prediction
    if (__builtin_popcount(phase_history[set]) > PHASE_HISTORY_LENGTH / 2) {
        // Favor spatial locality
        for (auto &line : cache_state[set]) {
            line.spatial_score = std::min(line.spatial_score + 1, UINT64_MAX);
        }
    } else {
        // Favor temporal locality
        for (auto &line : cache_state[set]) {
            line.temporal_score = std::min(line.temporal_score + 1, UINT64_MAX);
        }
    }

    // Decay scores to adapt to changes
    for (auto &line : cache_state[set]) {
        line.temporal_score = line.temporal_score > 0 ? line.temporal_score - 1 : 0;
        line.spatial_score = line.spatial_score > 0 ? line.spatial_score - 1 : 0;
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    // Implement if needed
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    // Implement if needed
}