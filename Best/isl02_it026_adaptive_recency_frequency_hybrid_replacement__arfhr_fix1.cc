#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS (NUM_CORE * 2048)
#define LLC_WAYS 16

struct BlockMeta {
    uint32_t recency;
    uint32_t frequency;
};

std::vector<std::vector<BlockMeta>> cache_meta(LLC_SETS, std::vector<BlockMeta>(LLC_WAYS));
std::vector<uint32_t> phase_counter(LLC_SETS, 0);

// Initialize replacement state
void InitReplacementState() {
    for (auto &set : cache_meta) {
        for (auto &block : set) {
            block.recency = 0;
            block.frequency = 0;
        }
    }
    phase_counter.assign(LLC_SETS, 0);
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
    uint32_t min_score = static_cast<uint32_t>(-1); // Max possible uint32_t value
    uint32_t phase_weight = phase_counter[set] > 15 ? 1 : 2; // Adjust phase detection

    for (uint32_t i = 0; i < LLC_WAYS; i++) {
        auto &meta = cache_meta[set][i];

        uint32_t score = (phase_weight * meta.recency) + ((3 - phase_weight) * meta.frequency);

        if (score < min_score) {
            min_score = score;
            victim = i;
        }
    }

    if (phase_counter[set] > 0) {
        phase_counter[set]--; // Reduce phase intensity
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
    auto &meta = cache_meta[set][way];
    if (hit) {
        meta.recency = 0;
        meta.frequency = meta.frequency < 4 ? meta.frequency + 1 : 4;
        phase_counter[set] = phase_counter[set] + 3 < 30 ? phase_counter[set] + 3 : 30;
    } else {
        meta.recency = LLC_WAYS - 1;
        meta.frequency = 1;
    }

    for (uint32_t i = 0; i < LLC_WAYS; i++) {
        if (i != way) {
            if (cache_meta[set][i].recency > 0) {
                cache_meta[set][i].recency--;
            }
        }
    }
}

// Print end-of-simulation statistics
void PrintStats() {
    // Implement if necessary
}

// Print periodic (heartbeat) statistics
void PrintStats_Heartbeat() {
    // Implement if necessary
}