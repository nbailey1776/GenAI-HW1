#include <vector>
#include <cstdint>
#include <iostream>
#include "../inc/champsim_crc2.h"

#define NUM_CORE       1
#define LLC_SETS       (NUM_CORE * 2048)
#define LLC_WAYS       16

// RRIP parameters
static const uint8_t MAX_RRPV      = 3;    // 2-bit RRPV
static const uint8_t INIT_RRPV     = MAX_RRPV;  
static const uint8_t COLD_RRPV     = MAX_RRPV - 1;

// SHiP parameters
static const int     SHCT_SIZE     = 1024;  // power-of-two
static const int     SHCT_CTR_BITS = 3;     
static const int     SHCT_MAX      = (1 << SHCT_CTR_BITS) - 1;
static const int     SHCT_THRESH   = (SHCT_MAX >> 1);

// Idle timeout (in accesses) before forced demotion
static const uint64_t IDLE_TIMEOUT = 10000ULL;

// Replacement state arrays
static uint8_t  rrpv[LLC_SETS][LLC_WAYS];
static bool     valid[LLC_SETS][LLC_WAYS];
static bool     reused[LLC_SETS][LLC_WAYS];
static uint16_t sig[LLC_SETS][LLC_WAYS];
static uint8_t  SHCT[SHCT_SIZE];
static uint64_t timestamp[LLC_SETS][LLC_WAYS];
static uint64_t global_counter = 0;

// Statistics
static uint64_t stat_hits    = 0;
static uint64_t stat_misses  = 0;
static uint64_t stat_bypass  = 0;

void InitReplacementState() {
  // Initialize per‐way state
  for (int s = 0; s < LLC_SETS; s++) {
    for (int w = 0; w < LLC_WAYS; w++) {
      rrpv[s][w]     = INIT_RRPV;
      valid[s][w]    = false;
      reused[s][w]   = false;
      sig[s][w]      = 0;
      timestamp[s][w]= 0;
    }
  }
  // Initialize SHCT to weakly‐hot
  for (int i = 0; i < SHCT_SIZE; i++) {
    SHCT[i] = SHCT_THRESH;
  }
  global_counter = stat_hits = stat_misses = stat_bypass = 0;
}

uint32_t GetVictimInSet(
    uint32_t    cpu,
    uint32_t    set,
    const BLOCK *current_set,
    uint64_t    PC,
    uint64_t    paddr,
    uint32_t    type
) {
  // Bypass cold streaming loads/prefetches
  bool is_stream = (type == LOAD || type == RFO || type == PREFETCH);
  uint16_t cur_sig = uint16_t(PC) & (SHCT_SIZE - 1);
  if (is_stream && SHCT[cur_sig] <= SHCT_THRESH) {
    stat_bypass++;
    return LLC_WAYS;  // bypass cache
  }

  // 1) Install into free way if exists
  for (uint32_t w = 0; w < LLC_WAYS; w++) {
    if (! current_set[w].valid)
      return w;
  }

  // 2) Idle‐timeout based demotion:
  //    any block not reused since IDLE_TIMEOUT → force RRPV = MAX
  for (uint32_t w = 0; w < LLC_WAYS; w++) {
    if (current_set[w].valid &&
        (global_counter - timestamp[set][w] >= IDLE_TIMEOUT)) {
      rrpv[set][w] = MAX_RRPV;
    }
  }

  // 3) Standard SRRIP victim search
  while (true) {
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
      if (rrpv[set][w] == MAX_RRPV)
        return w;
    }
    // age everyone
    for (uint32_t w = 0; w < LLC_WAYS; w++) {
      if (rrpv[set][w] < MAX_RRPV)
        rrpv[set][w]++;
    }
  }
}

void UpdateReplacementState(
    uint32_t cpu,
    uint32_t set,
    uint32_t way,
    uint64_t paddr,
    uint64_t PC,
    uint64_t victim_addr,
    uint32_t type,
    uint8_t  hit
) {
  // bumped on any *actual* cache access/hit/miss
  global_counter++;

  if (hit) {
    stat_hits++;
    // On hit: promote to MRU and mark reused
    rrpv[set][way]   = 0;
    reused[set][way] = true;
    timestamp[set][way] = global_counter;
  } else {
    stat_misses++;
    // 1) Learn from evicted block
    if (valid[set][way]) {
      uint16_t old_sig = sig[set][way];
      if (reused[set][way]) {
        if (SHCT[old_sig] < SHCT_MAX) SHCT[old_sig]++;
      } else {
        if (SHCT[old_sig] > 0)          SHCT[old_sig]--;
      }
    }
    // 2) Compute new signature & reset line metadata
    uint16_t new_sig = uint16_t(PC) & (SHCT_SIZE - 1);
    sig[set][way]    = new_sig;
    reused[set][way] = false;
    valid[set][way]  = true;
    // 3) Insertion RRPV from predictor
    if (SHCT[new_sig] > SHCT_THRESH) {
      rrpv[set][way] = 0;           // predicted hot
    } else {
      rrpv[set][way] = COLD_RRPV;   // predicted cold
    }
    timestamp[set][way] = global_counter;
  }
}

void PrintStats() {
  std::cout << "=== SVB-RRIP Final Stats ===\n";
  std::cout << "Hits    : " << stat_hits   << "\n";
  std::cout << "Misses  : " << stat_misses << "\n";
  std::cout << "Bypasses: " << stat_bypass << "\n";
}

void PrintStats_Heartbeat() {
  std::cout << "[SVB-RRIP] H:" << stat_hits
            << " M:" << stat_misses
            << " Byp:" << stat_bypass
            << std::endl;
}