# CacheForge + ChampSim: APARP Policy, Ablations, and Reproducibility

This repo contains:
- **APARP** (Adaptive Phase-Aware Recency/Frequency) – the best policy discovered in our search.
- Two **ablations**:
  - **Ablation A (Pure Temperature)**: single island, τ ∈ {0.2, 0.4, 0.6}.
  - **Ablation B (Islands with Temperature)**: 3 islands with per-island temperatures and optional migrate/5.
- Scripts to **reproduce** results and **export** metrics for plotting.

> **Goal**: Improve mean cache **hit rate** vs. baselines and analyze how different ablations affect exploration/exploitation in policy search.

---

## 1) Requirements

- OS: Windows 11 (WSL) / Linux / macOS
- Tools: `conda`, `python ≥ 3.9`, `gcc/g++` or `clang`, `make`, `sqlite3`
- ChampSim repo
- CacheForge Repo + run_loop.py and best policies from this repo
- **OpenAI API key** (put in `.env` )

---

## 2) Environment Setup

# In the CacheForge repo
conda env create -f environment.yml -n cacheforge
conda activate cacheforge

# Provide your API key (or use a .env file and direnv)
export OPENAI_API_KEY="sk-..."

---

## 2) Running

# To run training
cd cacheforge
python run_loop.py

# To run best policies
g++ -Wall -std=c++11 -o Best/049_adaptive_phase_aware_replacement_policy__aparp.cc lib/config1.a
./049_adaptive_phase_aware_replacement_policy__aparp-config1 -warmup_instructions 1000000 -simulation_instructions 1000000  -traces /traces/<trace_name>.trace.gz

