#include "champsim_crc2.h"
#include <unordered_map>
#include <vector>
#include <iostream>
#include <cassert>

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16
#define MAX_RRPV 3

using namespace std;

uint8_t rrpv[LLC_SETS][LLC_WAYS]; // Re-reference prediction values
unordered_map<uint64_t, uint32_t> pc_reuse_counter; // Track PC reuse

// Initialize replacement state
void InitReplacementState()
{
    cout << "Initialize Hybrid RRIP+PC predictor state" << endl;

    for (int i = 0; i < LLC_SETS; i++) {
        for (int j = 0; j < LLC_WAYS; j++) {
            rrpv[i][j] = MAX_RRPV; // initialize all lines with distant re-reference
        }
    }
}

// Get victim using RRIP
uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC,
                        uint64_t paddr, uint32_t type)
{
    // Try to find a line with RRPV == MAX_RRPV
    for (int i = 0; i < LLC_WAYS; i++) {
        if (rrpv[set][i] == MAX_RRPV)
            return i;
    }

    // If none found, increment all and try again
    while (true) {
        for (int i = 0; i < LLC_WAYS; i++) {
            rrpv[set][i]++;
        }

        for (int i = 0; i < LLC_WAYS; i++) {
            if (rrpv[set][i] >= MAX_RRPV)
                return i;
        }
    }
}

// Update state on hit/miss
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                            uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    // Increment PC reuse counter
    pc_reuse_counter[PC]++;

    if (hit) {
        rrpv[set][way] = 0; // On hit, block is re-referenced soon
    } else {
        // On fill, determine insertion RRPV based on PC history
        if (pc_reuse_counter[PC] > 5) {
            rrpv[set][way] = 1; // predicted to be reused soon
        } else {
            rrpv[set][way] = MAX_RRPV - 1; // default to distant re-reference
        }
    }
}

// Optional: Print heartbeat stats
void PrintStats_Heartbeat()
{
    cout << "Hybrid policy heartbeat: PC reuse table size = " << pc_reuse_counter.size() << endl;
}

// Optional: Print final stats
void PrintStats()
{
    size_t hot_pc = 0;
    for (const auto& entry : pc_reuse_counter) {
        if (entry.second > 5)
            hot_pc++;
    }

    cout << "Hybrid policy final stats:\n";
    cout << "Total unique PCs seen: " << pc_reuse_counter.size() << endl;
    cout << "Hot PCs (reused >5 times): " << hot_pc << endl;
}
