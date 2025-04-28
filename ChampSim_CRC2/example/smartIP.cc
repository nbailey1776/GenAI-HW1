#include "champsim_crc2.h"
#include <iostream>
#include <unordered_map>
#include <vector>
#include <cassert>

#define NUM_CORE 1
#define LLC_SETS NUM_CORE * 2048
#define LLC_WAYS 16
#define MAX_RRPV 3

using namespace std;

uint8_t rrpv[LLC_SETS][LLC_WAYS];
uint64_t pc_signature_table[LLC_SETS][LLC_WAYS]; // For SHiP-like tracking
unordered_map<uint64_t, int> pc_reuse_table;     // Positive for reuse, negative for pollution

// Initialize replacement state
void InitReplacementState()
{
    cout << "Initialize SmartIP Replacement State" << endl;
    for (int i = 0; i < LLC_SETS; i++) {
        for (int j = 0; j < LLC_WAYS; j++) {
            rrpv[i][j] = MAX_RRPV;
            pc_signature_table[i][j] = 0;
        }
    }
}

// Choose victim based on RRIP
uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type)
{
    while (true) {
        for (int i = 0; i < LLC_WAYS; i++) {
            if (rrpv[set][i] == MAX_RRPV)
                return i;
        }

        // Increment RRPVs
        for (int i = 0; i < LLC_WAYS; i++) {
            if (rrpv[set][i] < MAX_RRPV)
                rrpv[set][i]++;
        }
    }
}

// Update reuse predictor and RRPV state
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                            uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    uint64_t pc_sig = PC;

    if (hit) {
        rrpv[set][way] = 0;

        // Reward reuse PCs
        pc_reuse_table[pc_sig]++;
    } else {
        // Save PC signature in cache line metadata
        pc_signature_table[set][way] = pc_sig;

        // If PC has positive reuse, insert with RRPV=1 (likely reuse)
        if (pc_reuse_table[pc_sig] >= 1) {
            rrpv[set][way] = 1;
        } else {
            // Penalize new or polluting PCs
            rrpv[set][way] = MAX_RRPV - 1;
        }
    }
}

// Print debug or tracking stats at heartbeat
void PrintStats_Heartbeat()
{
    cout << "SmartIP heartbeat - PC reuse table size: " << pc_reuse_table.size() << endl;
}

// Print stats at simulation end
void PrintStats()
{
    size_t good = 0, bad = 0;
    for (const auto& entry : pc_reuse_table) {
        if (entry.second > 0) good++;
        else bad++;
    }

    cout << "SmartIP Final Stats:\n";
    cout << "Total PCs tracked: " << pc_reuse_table.size() << endl;
    cout << "Reuse-friendly PCs: " << good << ", Pollution-heavy PCs: " << bad << endl;
}
