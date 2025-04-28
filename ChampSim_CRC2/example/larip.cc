#include "champsim_crc2.h"
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cassert>

#define NUM_CORE 1
#define LLC_SETS NUM_CORE * 2048
#define LLC_WAYS 16
#define MAX_RRPV 3
#define INIT_SHCT 4
#define MAX_SHCT 7
#define REGION_TABLE_SIZE 4096 // Max tracked regions
#define REGION_SHIFT 6         // Granularity of region = 64B blocks

using namespace std;

uint8_t rrpv[LLC_SETS][LLC_WAYS];
uint64_t pc_signature[LLC_SETS][LLC_WAYS];
bool reused[LLC_SETS][LLC_WAYS];

unordered_map<uint64_t, uint8_t> SHCT;                 // PC reuse predictor
unordered_set<uint64_t> recent_regions;                // For region reuse

// Init state
void InitReplacementState()
{
    cout << "LARIP: Initializing replacement state...\n";
    for (int i = 0; i < LLC_SETS; ++i)
        for (int j = 0; j < LLC_WAYS; ++j) {
            rrpv[i][j] = MAX_RRPV;
            reused[i][j] = false;
            pc_signature[i][j] = 0;
        }

    SHCT.clear();
    recent_regions.clear();
}

// Choose victim way
uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                        uint64_t PC, uint64_t paddr, uint32_t type)
{
    while (true) {
        for (int i = 0; i < LLC_WAYS; i++) {
            if (rrpv[set][i] == MAX_RRPV) {
                // Update SHCT on eviction
                if (!reused[set][i]) {
                    uint64_t sig = pc_signature[set][i];
                    if (SHCT[sig] > 0)
                        SHCT[sig]--;
                }
                return i;
            }
        }
        // Increment RRPVs
        for (int i = 0; i < LLC_WAYS; i++)
            if (rrpv[set][i] < MAX_RRPV)
                rrpv[set][i]++;
    }
}

// Update replacement state
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                            uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    uint64_t sig = PC;
    uint64_t region = paddr >> REGION_SHIFT;

    if (hit) {
        rrpv[set][way] = 0;
        reused[set][way] = true;
        if (SHCT[sig] < MAX_SHCT)
            SHCT[sig]++;
        recent_regions.insert(region); // Track reused regions
        if (recent_regions.size() > REGION_TABLE_SIZE)
            recent_regions.erase(recent_regions.begin()); // Keep table size manageable
    } else {
        pc_signature[set][way] = sig;
        reused[set][way] = false;

        bool good_pc = SHCT[sig] >= INIT_SHCT;
        bool hot_region = recent_regions.count(region);

        // Conservative bypass for low-quality blocks
        if (!good_pc && !hot_region) {
            rrpv[set][way] = MAX_RRPV;
            return;
        }

        // If block is from a hot region or good PC, insert more aggressively
        rrpv[set][way] = (good_pc && hot_region) ? 0 : 1;
    }
}

void PrintStats_Heartbeat()
{
    cout << "[Heartbeat] LARIP: SHCT size = " << SHCT.size()
         << ", Regions tracked = " << recent_regions.size() << endl;
}

void PrintStats()
{
    int reusable = 0, not_reused = 0;
    for (auto& e : SHCT)
        (e.second >= INIT_SHCT) ? reusable++ : not_reused++;

    cout << "\n==== LARIP FINAL STATS ====\n";
    cout << "PCs Tracked: " << SHCT.size() << "\n";
    cout << "Reusable PCs: " << reusable << ", Polluting PCs: " << not_reused << "\n";
    cout << "Tracked Regions: " << recent_regions.size() << "\n";
}
