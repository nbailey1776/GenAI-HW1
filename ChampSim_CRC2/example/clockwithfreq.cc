#include "champsim_crc2.h"

#define NUM_CORE 1
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16

// Track the most recently used (MRU) block in each set
uint32_t mru[LLC_SETS][LLC_WAYS];

// Track the access count for each block to determine "hot" blocks
uint32_t hotness[LLC_SETS][LLC_WAYS];
#define HOTNESS_THRESHOLD 10

// Initialize replacement state
void InitReplacementState() {
    cout << "Initialize MRU-based Hot/Cold policy replacement state" << endl;

    for (int i = 0; i < LLC_SETS; ++i) {
        for (int j = 0; j < LLC_WAYS; ++j) {
            mru[i][j] = j;
            hotness[i][j] = 0;
        }
    }
}

// Find replacement victim in a set
uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set,
                       uint64_t PC, uint64_t paddr, uint32_t type) {
    // Try to evict from cold blocks first (blocks with low access count)
    for (int i = 0; i < LLC_WAYS; ++i) {
        if (hotness[set][i] <= HOTNESS_THRESHOLD) {
            return i;
        }
    }

    // If all blocks are hot, use simple MRU replacement
    for (int i = 0; i < LLC_WAYS; ++i) {
        if (mru[set][i] == (LLC_WAYS - 1)) {
            return i;
        }
    }

    return 0; // Fallback to first way
}

// Update replacement state on every cache hit or fill
void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way,
                           uint64_t paddr, uint64_t PC, uint64_t victim_addr,
                           uint32_t type, uint8_t hit) {
    if (hit) {
        // Increase hotness count for accessed block
        hotness[set][way]++;

        // If the block is already MRU, do nothing
        if (mru[set][way] != 0) {
            // Move the block to MRU position
            mru[set][way] = 0;

            // Update other blocks' MRU order
            for (int i = 0; i < LLC_WAYS; ++i) {
                if (mru[set][i] > mru[set][way]) {
                    mru[set][i]--;
                }
            }
        }
    } else {
        // If it's a miss, update MRU for victim block
        mru[set][victim_addr] = 0;

        // Decrease hotness count for evicted block (optional)
        hotness[set][victim_addr]--;
    }
}

// use this function to print out your own stats on every heartbeat
void PrintStats_Heartbeat()
{

}

// use this function to print out your own stats at the end of simulation
void PrintStats()
{

}
