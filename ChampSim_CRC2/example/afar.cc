#include "champsim_crc2.h"
#include <iostream>
#include <unordered_map>
#include <cassert>

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16
#define MAX_RRPV 3
#define HIT_THRESHOLD 8  // Promote PC to 'hot' after this many hits
#define SHCT_MAX 31

using namespace std;

uint8_t rrpv[LLC_SETS][LLC_WAYS];
uint64_t pc_history[LLC_SETS][LLC_WAYS];
unordered_map<uint64_t, uint32_t> PC_Hit_Counter; // Tracks hits per PC

// Initialize state
void InitReplacementState()
{
    cout << "AFAR: Initializing replacement state..." << endl;

    for (int i = 0; i < LLC_SETS; i++)
        for (int j = 0; j < LLC_WAYS; j++) {
            rrpv[i][j] = MAX_RRPV;
            pc_history[i][j] = 0;
        }
    PC_Hit_Counter.clear();
}

// Victim selection (classic RRIP)
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

// Update logic
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                            uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    uint64_t sig = PC;

    if (hit) {
        rrpv[set][way] = 0;
        PC_Hit_Counter[sig]++;
        if (PC_Hit_Counter[sig] > SHCT_MAX)
            PC_Hit_Counter[sig] = SHCT_MAX;
    } else {
        pc_history[set][way] = sig;

        if (PC_Hit_Counter[sig] >= HIT_THRESHOLD)
            rrpv[set][way] = 0; // Hot PC → aggressive insert
        else
            rrpv[set][way] = MAX_RRPV - 1; // SRRIP default: long re-reference
    }
}

void PrintStats_Heartbeat() {}

void PrintStats()
{
    size_t hot = 0, total = PC_Hit_Counter.size();
    for (const auto& kv : PC_Hit_Counter)
        if (kv.second >= HIT_THRESHOLD)
            hot++;
    cout << "==== AFAR Final Stats ====\n";
    cout << "Total PCs Tracked: " << total << "\n";
    cout << "Hot PCs (>= " << HIT_THRESHOLD << " hits): " << hot << "\n";
}
