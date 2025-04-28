#include "champsim_crc2.h"
#include <iostream>
#include <unordered_map>
#include <cassert>

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16
#define MAX_RRPV 3
#define HOT_THRESHOLD 6
#define SHCT_MAX 31

using namespace std;

uint8_t rrpv[LLC_SETS][LLC_WAYS];
uint64_t pc_tracker[LLC_SETS][LLC_WAYS];
unordered_map<uint64_t, uint32_t> pc_reuse_table;
unordered_map<uint64_t, uint64_t> pc_last_seen; // maps PC → last paddr

void InitReplacementState()
{
    cout << "HARP: Initializing hybrid policy..." << endl;
    for (int i = 0; i < LLC_SETS; ++i)
        for (int j = 0; j < LLC_WAYS; ++j) {
            rrpv[i][j] = MAX_RRPV;
            pc_tracker[i][j] = 0;
        }
    pc_reuse_table.clear();
    pc_last_seen.clear();
}

uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type)
{
    while (true) {
        for (int i = 0; i < LLC_WAYS; i++)
            if (rrpv[set][i] == MAX_RRPV)
                return i;
        for (int i = 0; i < LLC_WAYS; i++)
            rrpv[set][i]++;
    }
}

void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                            uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    if (hit) {
        rrpv[set][way] = 0;
        pc_reuse_table[PC]++;
        if (pc_reuse_table[PC] > SHCT_MAX)
            pc_reuse_table[PC] = SHCT_MAX;
    } else {
        pc_tracker[set][way] = PC;
        bool hot_pc = pc_reuse_table[PC] >= HOT_THRESHOLD;
        bool recent_reuse = (pc_last_seen.count(PC) && (pc_last_seen[PC] == paddr));
        pc_last_seen[PC] = paddr;

        if (hot_pc || recent_reuse)
            rrpv[set][way] = 0; // Favor reuse
        else
            rrpv[set][way] = MAX_RRPV - 1; // Conservative
    }
}

void PrintStats_Heartbeat() {}

void PrintStats()
{
    size_t hot_pcs = 0, total = pc_reuse_table.size();
    for (const auto& p : pc_reuse_table)
        if (p.second >= HOT_THRESHOLD)
            hot_pcs++;
    cout << "==== HARP Final Stats ====" << endl;
    cout << "Tracked PCs: " << total << ", Hot PCs: " << hot_pcs << endl;
}
