#include <vector>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include "../inc/champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS (NUM_CORE * 2048)
#define LLC_WAYS 16

struct CacheLine {
    uint64_t tag;
    uint32_t locality_score;
    uint32_t phase_counter;
    uint32_t last_access;
    uint32_t branch_miss_penalty;
};

std::vector<std::vector<CacheLine>> cache(LLC_SETS, std::vector<CacheLine>(LLC_WAYS));
std::unordered_map<uint64_t, uint32_t> pc_reuse_map;
std::unordered_map<uint64_t, uint32_t> pc_stride_map;
std::unordered_map<uint64_t, uint32_t> pc_phase_map;
uint32_t global_time = 0;

void InitReplacementState() {
    for (auto &set : cache) {
        for (auto &line : set) {
            line.locality_score = 0;
            line.phase_counter = 0;
            line.last_access = 0;
            line.branch_miss_penalty = 0;
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
    uint32_t min_score = UINT32_MAX;

    for (uint32_t way = 0; way < LLC_WAYS; ++way) {
        if (current_set[way].valid == 0) {
            return way;
        }
        uint32_t age = global_time - cache[set][way].last_access;
        uint32_t score = cache[set][way].locality_score + cache[set][way].phase_counter + age + cache[set][way].branch_miss_penalty;
        if (score < min_score) {
            min_score = score;
            victim = way;
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
    global_time++;
    cache[set][way].last_access = global_time;

    if (hit) {
        cache[set][way].locality_score++;
    } else {
        uint32_t stride = paddr - pc_stride_map[PC];
        pc_stride_map[PC] = paddr;

        if (stride % 64 == 0) {
            cache[set][way].locality_score = 2 * pc_reuse_map[PC];
        } else {
            cache[set][way].locality_score = pc_reuse_map[PC];
        }
        cache[set][way].phase_counter = pc_phase_map[PC];
    }

    pc_reuse_map[PC] = cache[set][way].locality_score;

    if (cache[set][way].locality_score > 5) {
        cache[set][way].phase_counter++;
    } else {
        cache[set][way].phase_counter = 0;
    }

    // Enhanced decay mechanism for scores
    if (global_time % 50 == 0) {
        for (auto &set : cache) {
            for (auto &line : set) {
                if (line.locality_score > 0) {
                    line.locality_score--;
                }
                if (line.branch_miss_penalty > 0) {
                    line.branch_miss_penalty--;
                }
            }
        }
    }

    // Update phase map to detect changes
    pc_phase_map[PC] = (pc_phase_map[PC] + 1) % 10;

    // Simulate branch miss penalty
    if (type == 1) { // Assuming type 1 is a branch miss
        cache[set][way].branch_miss_penalty += 2;
    }
}

void PrintStats() {
    std::cout << "Final Cache Statistics" << std::endl;
}

void PrintStats_Heartbeat() {
    std::cout << "Heartbeat Cache Statistics" << std::endl;
}