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

void InitReplacementState() {
    for (auto &set : cache_meta) {
        for (auto &block : set) {
            block.recency = 0;
            block.frequency = 0;
        }
    }
    phase_counter.assign(LLC_SETS, 0);
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
    uint32_t min_score = std::numeric_limits<uint32_t>::max();
    uint32_t phase_weight = phase_counter[set] > 15 ? 1 : 2; // Adjust phase detection

    for (uint32_t i = 0; i < LLC_WAYS; i++) {
        auto &meta = cache_meta[set][i];
        
        uint32_t score = (phase_weight * meta.recency) + ((3 - phase_weight) * meta.frequency);

        if (score < min_score) {
            min_score = score;
            victim = i;
        }
    }

    phase_counter[set] = std::max(phase_counter[set] - 1, 0U); // Reduce phase intensity
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
    auto &meta = cache_meta[set][way];
    if (hit) {
        meta.recency = 0;
        meta.frequency = std::min(meta.frequency + 1, 4U);
        phase_counter[set] = std::min(phase_counter[set] + 3, 30U); // Quickly increment phase on hit
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

void PrintStats() {
    // Implement if necessary
}

void PrintStats_Heartbeat() {
    // Implement if necessary
}