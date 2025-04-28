#include "champsim_crc2.h"

class CRC2PlusPlus : public ReplacementPolicy {
private:
    // Array to store the number of cache lines accessed for each set indexed by set ID
    std::vector<int> accessCount; 

public:
    // Initialize replacement state
    void InitReplacementState() {
        accessCount.resize(sets);
    }

    // Find victim in the set
    uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const std::vector<bool>& blk, uint64_t PC, uint64_t paddr, uint32_t type) {
        // When cache is empty or a write-back happens, replace the line with LRU information
        if (blk[0] || type == WRITEBACK) return 0;  
        
        int leastAccessed = 0;
        for(int i=1; i<associativity; ++i){
            // If two blocks have accessed the same number of times, choose randomly.
            if (accessCount[set] == accessCount[leastAccessed]) {  
                return (rand() % i); 
            }
            // Choose least recently used line in case it has not been accessed as often
            else if(accessCount[set] < accessCount[leastAccessed]){  
                leastAccessed = i;
            }
        }
        
        return leastAccessed; 
    }

    // Update replacement state
    void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit) {
        // Reset access count on cache fill or write-back
        if (type == WRITEBACK || hit) { 
            accessCount[set] = 0;  
        } else {
            ++accessCount[set];  // Increase access count for the accessed block.
        }
    }
    
    // Print statistics
    void PrintStats() {
       std::cout << "Average number of cache lines accessed: " << accumulated_access / (sets * ways) << "\n";
    }

    // Print periodic statistics
    void PrintStats_Heartbeat() {
        // Implementation here
    }
};
