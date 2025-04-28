import os
import subprocess
import pandas as pd
from openai import OpenAI
import csv
import subprocess
import os
import random

# OpenAI setup
client = OpenAI(api_key="")

# Paths
PROJECT_DIR = "/share/csc591s25/project_klay/ChampSim_CRC2"
OUTPUT_DIR = "/share/csc591s25/project_klay/outputs"
EXAMPLE_DIR = os.path.join(PROJECT_DIR, "example")

os.makedirs(OUTPUT_DIR, exist_ok=True)

DB_PATH = os.path.join(OUTPUT_DIR, "policy_rl_results.csv")

# Initialize the CSV database
with open(DB_PATH, "w", newline='') as f:
    writer = csv.writer(f)
    writer.writerow(["Iteration", "Prompt", "HitRate", "RewardOrPenalty", "C++ Filename"])

# Define a starting base prompt (simple LRU-based)
base_prompt = """
You are tasked to generate a complete, compilable C++ file for a CPU cache eviction policy for ChampSim CRC2.

DO NOT output any English text, description, or explanation.  
ONLY output C++ code.

You are free to design **any valid eviction policy** — it could be based on:
- LRU (Least Recently Used)
- Random
- LFU (Least Frequently Used)
- LFU+LRU hybrid
- Custom heuristics
- Anything that is internally consistent

However, you must strictly satisfy the following rules:

- Correctly match the ChampSim CRC2 interface:
  - `void InitReplacementState()`
  - `uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type)`
  - `void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)`
  - `void PrintStats_Heartbeat()`
  - `void PrintStats()`
- Maintain any necessary per-line metadata (like access timestamps, access counters, etc.) appropriately
- Assume flat cache structure:
  - Number of sets = `NUM_SET`
  - Associativity = `ASSOC`
- At the top of your code, manually define:
  - `#define NUM_SET 2048`
  - `#define ASSOC 16`
- Only use standard C++11 or newer features
- Output pure C++ between `%%` markers only (no markdown, no ```cpp)

Below is an **example starter structure** — you must complete it based on the policy you choose:

%%
#include "champsim_crc2.h"
#include <vector>
#include <cstdint>
#include <climits>
#include <cstdlib>

#define NUM_SET 2048
#define ASSOC 16

// (Your metadata structs here)

void InitReplacementState() {
    // (Your initialization code here)
}

uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK* current_set, uint64_t PC, uint64_t paddr, uint32_t type) {
    // (Your victim selection logic here)
}

void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit) {
    // (Your state update logic here)
}

void PrintStats_Heartbeat() {
    // (Heartbeat stats here)
}

void PrintStats() {
    // (Final stats here)
}
%%
"""

def safe_openai_message(role, content):
    """
    Returns a correctly formatted message object 
    depending on OpenAI's latest API requirements.
    """
    return {"role": role, "content": [{"type": "text", "text": content}]}

def generate_cpp_code(prompt):
    response = client.chat.completions.create(
        model="o4-mini",
        messages=[
            safe_openai_message("system", "You are a helpful assistant that outputs only clean C++ code without any explanations."),
            safe_openai_message("user", prompt)
        ]
    )
    generated_text = response.choices[0].message.content

    # Extract C++ code between %%
    code_start = generated_text.find("%%") + 2
    code_end = generated_text.rfind("%%")
    return generated_text[code_start:code_end].strip()

def compile_and_run(cpp_filename, idx):
    # Move to correct directory
    os.chdir(PROJECT_DIR)

    # Step 1: Compile
    compile_command = f"g++ -Wall --std=c++11 -Iinc -o {cpp_filename}-config1 example/{cpp_filename}.cc lib/config1.a"
    compile_result = subprocess.run(compile_command, shell=True, capture_output=True, text=True)

    if compile_result.returncode != 0:
        print(f"Compilation failed for {cpp_filename}:")
        print(compile_result.stderr)
        return None

    print(f"Compilation successful for {cpp_filename}.")

    # Step 2: Prepare output path
    output_file = os.path.join(OUTPUT_DIR, f"{cpp_filename}_run_output.txt")
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Step 3: Run simulation and save output to file
    run_command = f"./{cpp_filename}-config1 -warmup_instructions 1000000 -simulation_instructions 10000000 -traces trace/astar_313B.trace.gz"
    
    # Open output file manually
    with open(output_file, "w") as f:
        run_result = subprocess.run(run_command, shell=True, stdout=f, stderr=subprocess.STDOUT)

    if run_result.returncode != 0:
        print(f"Simulation failed for {cpp_filename}.")
        print(f"Check simulation log in {output_file}")
        return None

    print(f"Simulation completed for {cpp_filename}.")
    print(f"Output saved at {output_file}")

    return output_file

def parse_hit_rate(output_file):
    total_access = None
    total_hit = None

    with open(output_file, "r") as f:
        for line in f:
            if "LLC TOTAL" in line and "ACCESS" in line and "HIT" in line and "MISS" in line:
                parts = line.split()
                # Example line parts:
                # ['LLC', 'TOTAL', 'ACCESS:', '104771', 'HIT:', '84983', 'MISS:', '19788']
                try:
                    total_access = int(parts[3])
                    total_hit = int(parts[5])
                except (IndexError, ValueError) as e:
                    print(f"Error parsing line: {line}")
                    print(f"Error: {e}")
                    return 0.0
                break

    if total_access is None or total_hit is None:
        print("Could not find TOTAL ACCESS or TOTAL HIT in simulation output!")
        return 0.0

    hit_rate = total_hit / total_access
    return hit_rate

# Start variables
best_hit_rate = 0.3
current_prompt = base_prompt

for iteration in range(1, 6):
    print(f"\nIteration {iteration}")

    # 1. Generate C++ code
    cpp_code = generate_cpp_code(current_prompt)

    # 2. Save C++ file
    cpp_filename = f"replacement_policy_{iteration}"
    cpp_filepath = os.path.join(EXAMPLE_DIR, f"{cpp_filename}.cc")
    with open(cpp_filepath, "w") as f:
        f.write(cpp_code)
    print(f"Saved {cpp_filename}.cc")

    # 3. Compile and Run
    output_file = compile_and_run(cpp_filename, iteration)
    if output_file is None:
        print("Skipping this iteration due to compilation/runtime error.")
        continue

    # 4. Parse hit rate
    hit_rate = parse_hit_rate(output_file)
    print(f"Hit Rate: {hit_rate:.4f}")

    # 5. Decide Reward or Penalty
    if hit_rate > best_hit_rate:
        reward = "Reward"
        best_hit_rate = hit_rate
        print(f"New Best Hit Rate! ({hit_rate:.4f}) ➔ REWARD")
    else:
        reward = "Penalty"
        print(f"Hit Rate worsened ➔ PENALTY (Exploration needed)")

    # 6. Save results to DB
    with open(DB_PATH, "a", newline='') as f:
        writer = csv.writer(f)
        writer.writerow([iteration, current_prompt, hit_rate, reward, cpp_filename])

    # 7. Update Prompt for Next Iteration
    if reward == "Penalty":
        # HEAVY mutation (Wild exploration)
        heavy_mutations = [
            "Completely abandon LRU and use purely Random eviction.",
            "Switch to LFU (Least Frequently Used) based on access counts.",
            "Implement a weighted random eviction based on last access time.",
            "Keep prefetched blocks longer than normal ones.",
            "Age every cache line aggressively and evict the oldest.",
            "Prefer to evict dirty cache lines first.",
            "Invent a fresh eviction heuristic combining random + frequency.",
            "Mimic Belady's future knowledge using random farthest accesses.",
            "Use program counter (PC)-based prediction for eviction scoring.",
            "Mix recency and frequency into a custom scoring function.",
            "Implement MRU (Most Recently Used) instead of LRU to test corner behavior.",
            "Randomly shuffle eviction candidates and pick based on modular hashing."
        ]
        heavy_mutation = random.choice(heavy_mutations)

        current_prompt = base_prompt + f"\n\n‼URGENT - New Mutation Instruction:\n{heavy_mutation}"
        current_prompt += "\n\nWorkload Specific Hint:\nFocus the eviction policy on handling workloads like astar_313B, where large working sets and irregular access patterns cause high cache contention. Prioritize evicting less recently and less frequently used blocks."


        print(f"🔁 Heavy Mutation Applied: {heavy_mutation}")

    else:
        # Light refinement after reward
        light_refinements = [
            "Refine the eviction policy by minimizing metadata overhead.",
            "Tune timestamp granularity to reduce contention.",
            "Optimize metadata initialization to avoid unnecessary resets.",
            "Prefer static inline functions for victim selection logic.",
            "Introduce priority between load hits and RFO hits.",
            "Slightly prioritize lines with low reuse distance.",
            "Add minimal adaptive randomization to avoid pathological worst cases.",
            "Balance dirty vs clean lines more delicately during eviction."
        ]
        light_refinement = random.choice(light_refinements)

        current_prompt = base_prompt + f"\n\nFine-tuning Suggestion:\n{light_refinement}"
        current_prompt += "\n\nWorkload Specific Hint:\nFocus the eviction policy on handling workloads like astar_313B, where large working sets and irregular access patterns cause high cache contention. Prioritize evicting less recently and less frequently used blocks."
        
        print(f"Light Refinement Applied: {light_refinement}")

# Read database and sort results
df = pd.read_csv(DB_PATH)

# Sort policies by HitRate descending
sorted_df = df.sort_values(by="HitRate", ascending=False)

# Save sorted table
sorted_csv_path = os.path.join(OUTPUT_DIR, "policy_rl_final_rankings.csv")
sorted_df.to_csv(sorted_csv_path, index=False)

print(f"Final rankings saved at: {sorted_csv_path}")
display(sorted_df)