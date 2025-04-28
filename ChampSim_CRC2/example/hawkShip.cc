#include "champsim_crc2.h"
#include <iostream>
#include <unordered_map>
#include <vector>
#include <cassert>

#define NUM_CORE 1
#define LLC_SETS NUM_CORE * 2048
#define LLC_WAYS 16
#define MAX_RRPV 3
#define MAX_SHCT 7
#define INIT_SHCT 3
#define NUM_SAMPLER_SETS 64 // A small subset of sets used for training

using namespace std;

uint8_t rrpv[LLC_SETS][LLC_WAYS];
uint64_t pc_signature[LLC_SETS][LLC_WAYS];
bool reused[LLC_SETS][LLC_WAYS];
unordered_map<uint64_t, uint8_t> SHCT;

// Sampler state
unordered_map<uint64_t, bool> sampler_optimal_reuse;

bool is_sampler_set(uint32_t set) {
    return (set % (LLC_SETS / NUM_SAMPLER_SETS)) == 0;
}

void InitReplacementState()
{
    cout << "Initialize HawkSHiP Replacement State" << endl;
    for (int i = 0; i < LLC_SETS; i++)
        for (int j = 0; j < LLC_WAYS; j++) {
            rrpv[i][j] = MAX_RRPV;
            reused[i][j] = false;
            pc_signature[i][j] = 0;
        }
    SHCT.clear();
    sampler_optimal_reuse.clear();
}

uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type)
{
    while (true) {
        for (int i = 0; i < LLC_WAYS; i++) {
            if (rrpv[set][i] == MAX_RRPV) {
                if (!reused[set][i]) {
                    uint64_t sig = pc_signature[set][i];
                    if (SHCT[sig] > 0)
                        SHCT[sig]--;
                }
                return i;
            }
        }
        for (int i = 0; i < LLC_WAYS; i++) {
            if (rrpv[set][i] < MAX_RRPV)
                rrpv[set][i]++;
        }
    }
}

void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                            uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    uint64_t sig = PC;

    if (hit) {
        rrpv[set][way] = 0;
        reused[set][way] = true;
        if (SHCT[sig] < MAX_SHCT)
            SHCT[sig]++;
    } else {
        pc_signature[set][way] = sig;
        reused[set][way] = false;

        // Sampler logic (simulate Belady)
        if (is_sampler_set(set)) {
            // Fake future knowledge: simulate if this block would’ve been reused
            // Here we assume reused blocks from same PC tend to repeat
            if (sampler_optimal_reuse[sig])
                SHCT[sig] = min<uint8_t>(SHCT[sig] + 1, MAX_SHCT);
            else if (SHCT[sig] > 0)
                SHCT[sig]--;
        }

        // Bypass if PC is known to be polluting
        if (SHCT[sig] == 0) {
            rrpv[set][way] = MAX_RRPV;
            return; // will be chosen soon for eviction
        }

        rrpv[set][way] = (SHCT[sig] >= INIT_SHCT) ? 1 : MAX_RRPV - 1;
    }
}

void PrintStats_Heartbeat()
{
    cout << "HawkSHiP Heartbeat: SHCT entries = " << SHCT.size() << endl;
}

void PrintStats()
{
    int reuse_pc = 0, bad_pc = 0;
    for (auto& e : SHCT) {
        if (e.second >= INIT_SHCT) reuse_pc++;
        else bad_pc++;
    }

    cout << "HawkSHiP Summary:\n";
    cout << "Tracked PCs: " << SHCT.size() << "\n";
    cout << "High-reuse PCs: " << reuse_pc << ", Low-reuse PCs: " << bad_pc << "\n";
}
