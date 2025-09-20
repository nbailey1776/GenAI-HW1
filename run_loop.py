#!/usr/bin/env python3
import sys, os
sys.path.append(os.path.abspath(".."))

from dotenv import load_dotenv
load_dotenv()
import re
import time
import sqlite3
import subprocess
import hashlib
from pathlib import Path
from typing import Optional, Tuple, List, Dict, Any
from openai import OpenAI
from RAG import ExperimentRAG
from PromptGenerator import PolicyPromptGenerator


# ──────────────────────────────────────────────────────────────────────────────
# Configuration
# ──────────────────────────────────────────────────────────────────────────────
DB_PATH = "DB/funsearch.db"
LIB_PATH = "ChampSim_CRC2/lib/config1.a"
INCLUDE_DIR = "ChampSim_CRC2/inc"
EXAMPLE_DIR = Path("ChampSim_CRC2/new_policies")

WARMUP_INST = "1000000"
SIM_INST = "10000000"
MODEL = "gpt-4o"
ITERATIONS = 200
# TEMPERATURE = 0.2

# ── Islands (minimal overhead) ────────────────────────────────────────────────
N_ISLANDS = 3                # number of sub-populations
MIGRATE_EVERY = 5            # after every 5 full passes over islands, copy best→worst

# NEW: per-island temperatures (exploit → explore)
ISLAND_TEMPS = [0.15, 0.35, 0.7]  # isl0=low (exploit), isl1=mid, isl2=high (explore)
DEFAULT_TEMP = 0.35

# NEW: auto exploitation trigger
EXPLOIT_IF_NEAR = 0.98       # if island best ≥ 98% of global best → exploit mode
EXPLOIT_TEMP = 0.05          # extra-low temperature in exploit mode

# ── Small robustness knobs ───────────────────────────────────────────────────
SIM_TIMEOUT_SEC = 600        # avoid “stuck” sims; skip a candidate if a trace runs too long
REPAIR_TRIES = 2             # LLM auto-repair attempts on compile failure

EXAMPLE_DIR.mkdir(parents=True, exist_ok=True)

workloads = [
    {"name": "astar", "trace_path": "ChampSim_CRC2/traces/astar_313B.trace.gz"},
    {"name": "lbm", "trace_path": "ChampSim_CRC2/traces/lbm_564B.trace.gz"},
    {"name": "mcf", "trace_path": "ChampSim_CRC2/traces/mcf_250B.trace.gz"},
    {"name": "milc", "trace_path": "ChampSim_CRC2/traces/milc_409B.trace.gz"},
    {"name": "omnetpp", "trace_path": "ChampSim_CRC2/traces/omnetpp_17B.trace.gz"}
]

# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────
def sanitize(name: str) -> str:
    print("     3. 🔧 [Sanitize] Cleaning policy name")
    return "".join(c if c.isalnum() else "_" for c in name).strip("_").lower()

def parse_policy_content(text: str,) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    def _extract(pattern: str):
        m = re.search(pattern, text, flags=re.DOTALL | re.IGNORECASE)
        return m.group(1).strip() if m else None

    name = _extract(r"##\s*Policy\s*Name\s*\n(.*?)\n")
    desc = _extract(r"##\s*Policy\s*Description\s*\n(.*?)\n")
    code = _extract(r"```cpp\s*(.*?)\s*```")
    return name, desc, code

def compile_policy(cc: Path) -> Path:
    print(f"     4. 🔨 [Compile] Compiling: {cc.name}\n")
    exe = cc.with_suffix(".out")
    subprocess.run(
        [
            "g++",
            "-Wall",
            "-std=c++17",
            f"-I{INCLUDE_DIR}",
            str(cc),
            LIB_PATH,
            "-o",
            str(exe),
        ],
        check=True,
        capture_output=True,   # capture stderr for auto-repair
        text=True,
    )
    return exe

def run_policy(exe: Path, trace_path: Path) -> Optional[str]:
    print(f"     5. ⏳ [Simulation] Starting simulation for: {exe.name} and {str(trace_path)}")
    start_time = time.time()
    try:
        res = subprocess.run(
            [
                str(exe),
                "-warmup_instructions", WARMUP_INST,
                "-simulation_instructions", SIM_INST,
                "-traces", str(trace_path),
            ],
            check=True,
            capture_output=True,
            text=True,
            timeout=SIM_TIMEOUT_SEC,  # ⏱️ avoid hanging
        )
    except subprocess.TimeoutExpired:
        print(f"⏱️  [Timeout] Simulation exceeded {SIM_TIMEOUT_SEC}s → skipping this workload for the candidate")
        return None
    duration = time.time() - start_time
    print(f"     6. 🏁 [Simulation] Finished in {duration:.2f} seconds for: {exe.name} and {trace_path}")
    return res.stdout

def parse_hit_rate(output: str) -> float:
    print("     7. 📊 [Metric] Parsing cache hit rate from output")
    m = re.search(r"LLC TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)", output)
    if not m:
        raise RuntimeError("LLC TOTAL not found")
    return int(m.group(2)) / int(m.group(1))

def record(workload, name, desc, cc: Path, rate, workload_desc):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute(
        """
      INSERT INTO experiments
        (workload, policy, policy_description, workload_description,
         cpp_file_path, cache_hit_rate, score)
      VALUES (?, ?, ?, ?, ?, ?, ?)""",
        (workload, name, desc, workload_desc, str(cc), rate, rate),
    )
    conn.commit()
    conn.close()

# ── NEW: constraints + small sanitizer/repair helpers ─────────────────────────
CONSTRAINTS = """
Constraints (must follow):
- Use ONLY the fields/functions available in the provided C++ template and ChampSim CRC2 policy interface.
- DO NOT add timers or cycle APIs (e.g., GetCurrentCycle, champsim::get_cycle, __rdtsc, <chrono>), I/O, threads, or new headers beyond the template.
- Keep state in the provided replacement metadata only.
- Must compile with -std=c++17 and the provided includes only.
"""

EXPLOIT_GUIDANCE = """
(Exploitation mode is ON)
- Keep the high-level design intact; focus on small, high-confidence tweaks.
- Prefer changing thresholds/weights, tie-breakers, or short-condition logic.
- Avoid new global data structures; keep the template and interfaces unchanged.
- Maintain compile safety: do not add headers or use non-template APIs.
"""

def _append_constraints(prompt: str) -> str:
    return prompt + "\n" + CONSTRAINTS + "\n"

def _append_exploit(prompt: str, enable: bool) -> str:
    return prompt + ("\n" + EXPLOIT_GUIDANCE + "\n" if enable else "")

def _hash_code(s: str) -> str:
    return hashlib.sha256(s.encode("utf-8")).hexdigest()

def _build_repair_prompt(code: str, compiler_err: str, code_template: str) -> str:
    return _append_constraints(
        "The following C++ policy failed to compile. Fix it WITHOUT adding non-template headers or unknown APIs.\n"
        f"Compiler error:\n```\n{compiler_err}\n```\n\n"
        f"Original code:\n```cpp\n{code}\n```\n\n"
        f"Provide a corrected full implementation using this template:\n{code_template}\n"
    )

def patch_headers_and_forbidden(code: str) -> str:
    # Forbid known non-CRC2 APIs
    banned = ["GetCurrentCycle", "__rdtsc", "champsim::get_cycle", "std::chrono", "<chrono>", "<x86intrin.h>"]
    for b in banned:
        if b in code:
            code = code.replace(b, "/*FORBIDDEN_API_REMOVED*/")

    # Ensure headers when tokens used
    need_cfloat = "FLT_MAX" in code or "DBL_MAX" in code
    need_climits = "INT_MAX" in code or "UINT_MAX" in code

    lines = code.splitlines()
    inserted = False
    for i, ln in enumerate(lines):
        if '#include "../inc/champsim_crc2.h"' in ln and not inserted:
            extras = []
            if need_cfloat and "<cfloat>" not in code:
                extras.append("#include <cfloat>")
            if need_climits and "<climits>" not in code:
                extras.append("#include <climits>")
            if extras:
                lines.insert(i+1, "\n".join(extras))
            inserted = True
            break
    return "\n".join(lines)

# ── NEW: tiny per-island state ────────────────────────────────────────────────
class IslandState:
    def __init__(self, island_id: int):
        self.id = island_id
        self.prev_name: Optional[str] = None
        self.prev_desc: Optional[str] = None
        self.prev_code: Optional[str] = None
        self.best_score: float = -1.0
        self.best_name: Optional[str] = None
        self.best_desc: Optional[str] = None

# ──────────────────────────────────────────────────────────────────────────────
# Main Feedback Loop with Reward/Penalty + Islands (minimal changes)
# ──────────────────────────────────────────────────────────────────────────────
def main():

    WORKLOAD = "all"

    # 1) Setup RAG and PromptGenerator
    rag = ExperimentRAG(DB_PATH)
    prompt_gen = PolicyPromptGenerator(DB_PATH)
    load_dotenv(dotenv_path=Path(".env"), override=False)

    client = OpenAI(api_key=os.getenv("OPENAI_API_KEY"),)

    top_policies = rag.get_top_policies_by_score(WORKLOAD, top_n=5) or []
    workload_desc, traces = rag.get_all_workloads_with_description_and_traces()

    if top_policies:
        best_hit = float(top_policies[0]["score"])
        policy_summary = "\n".join(
            f"Policy: {p['policy']}\nHit Rate: {float(p['score']):.2%}\nDescription:\n{p['policy_description']}\n"
            for p in top_policies
        )
    else:
        best_hit = 0.0
        policy_summary = "No prior experiments found. Start from scratch.\n"

    print(f"     📈 [Init] Starting best cache hit rate: {best_hit:.2%}")

    # ── NEW: create islands + novelty filter store ────────────────────────────
    islands = [IslandState(k) for k in range(N_ISLANDS)]
    seen_hashes: Dict[int, set] = {}

    global_best = best_hit

    # interleave islands with minimal change: each i picks island i % N_ISLANDS
    for i in range(ITERATIONS):
        isl = islands[i % N_ISLANDS]
        iter_label = f"isl {isl.id} it {i}"
        seen_hashes.setdefault(isl.id, set())

        # Decide exploration vs exploitation + pick temperature
        base_temp = ISLAND_TEMPS[isl.id] if isl.id < len(ISLAND_TEMPS) else DEFAULT_TEMP
        is_near_elite = (isl.best_score >= 0 and global_best > 0 and isl.best_score >= global_best * EXPLOIT_IF_NEAR)
        mode_exploit = bool(is_near_elite)
        call_temp = EXPLOIT_TEMP if mode_exploit else base_temp

        # Build prompt (same logic, but per-island prev state) + constraints + (optional) exploit guidance
        if isl.prev_name is None:
            prompt = _append_constraints(
                f"The following workloads are under consideration:\n"
                f"{workload_desc}\n\n"
                "The top-performing cache replacement policies from past experiments are:\n"
                f"{policy_summary}\n\n"
                "Your task: Propose a new cache replacement policy that aims to **outperform all of the above policies** "
                "across these workloads. Consider workload characteristics like branching, memory access patterns, spatial and temporal locality, and phase behavior.\n\n"
                "Suggested approach:\n"
                "1) Generate 3-4 distinct policy ideas (divergent thinking), briefly explain why each could help with different workloads.\n"
                "2) Choose the most promising policy and provide a complete C++ implementation.\n"
                "3) Include any tunable parameters or knobs, and note what telemetry/statistics should be tracked.\n\n"
                "Use the exact output format below:\n\n"
                "## Policy Name\n<name>\n\n"
                "## Policy Description\n<one paragraph describing the approach and why it helps>\n\n"
                "## C++ Implementation\n"
                f"{prompt_gen._get_code_template()}\n"
            )
            # First iteration per island should generally explore; still append exploit note if triggered
            prompt = _append_exploit(prompt, mode_exploit)
        else:
            if isl.best_score >= 0 and isl.best_score > global_best:
                global_best = isl.best_score
            feedback = (
                f"Great! Best average hit rate on this island so far is {isl.best_score:.2%}. Please refine further."
                if isl.best_score >= 0 else
                "No improvement yet; try a different approach."
            )
            prompt = _append_constraints(
                f"The following workloads are under consideration:\n"
                f"{workload_desc}\n\n"
                f"Your previous design was **{isl.prev_name}**:\n\n"
                f"Description:\n{isl.prev_desc}\n\n"
                f"Implementation:\n```cpp\n{isl.prev_code}\n```\n\n"
                f"Feedback from the last run:\n{feedback}\n\n"
                "Task: Refine or redesign the policy to achieve better performance across all workloads. "
                "Consider workload characteristics such as branching behavior, memory access patterns, spatial and temporal locality, and phase changes. "
                "You may propose modifications, hybrid approaches, or completely new ideas if needed.\n\n"
                "Produce the output in the exact format below:\n\n"
                "## Policy Name\n<name>\n\n"
                "## Policy Description\n<one paragraph explaining the approach and why it improves performance>\n\n"
                "## C++ Implementation\n"
                f"{prompt_gen._get_code_template()}\n"
            )
            prompt = _append_exploit(prompt, mode_exploit)

        # 5) Call model (now with per-island temperature & exploit override)
        print(f"     1. 📤 [LLM] {iter_label}: Sending prompt to model (temp={call_temp:.2f}, mode={'exploit' if mode_exploit else 'explore'})")
        resp = client.responses.create(
            model=MODEL,
            input=prompt,
            temperature=call_temp,
        )

        text = resp.output_text
        print(f"     2. 📥 [LLM] {iter_label}: Response received from OpenAI")

        # 6) Parse LLM output
        name, desc, code = parse_policy_content(text)
        if not (name and desc and code):
            print(f"❌ Parse failed at {iter_label}; skipping")
            continue

        # Sanitize code: remove forbidden APIs; auto-include needed headers for tokens
        code = patch_headers_and_forbidden(code)

        # Novelty filter (per-island) to avoid re-running near-duplicates
        h = _hash_code(code)
        if h in seen_hashes[isl.id]:
            print("↩️  Duplicate/near-duplicate code for this island; skipping")
            continue
        seen_hashes[isl.id].add(h)

        # 7) Write, compile, run (filenames include island + iter)
        base = sanitize(name)
        cc = EXAMPLE_DIR / f"isl{isl.id:02d}_it{i:03d}_{base}.cc"
        cc.write_text(code, encoding="utf-8")

        try:
            exe = compile_policy(cc)
        except subprocess.CalledProcessError as e:
            print(f"❌ [Compile Error] {iter_label}:\n{e}")
            # Try small LLM auto-repairs (use low temperature for deterministic fixes)
            err_text = e.stderr or str(e)
            repaired = False
            for r in range(REPAIR_TRIES):
                print(f"🩺 [Repair] {iter_label} try {r+1}/{REPAIR_TRIES}")
                repair_prompt = _build_repair_prompt(code, err_text, prompt_gen._get_code_template())
                fix_resp = client.responses.create(model=MODEL, input=repair_prompt, temperature=0.0)
                _, _, fixed_code = parse_policy_content(fix_resp.output_text)
                if not fixed_code:
                    continue
                fixed_code = patch_headers_and_forbidden(fixed_code)
                cc = EXAMPLE_DIR / f"isl{isl.id:02d}_it{i:03d}_{base}_fix{r+1}.cc"
                cc.write_text(fixed_code, encoding="utf-8")
                try:
                    exe = compile_policy(cc)
                    repaired = True
                    break
                except subprocess.CalledProcessError as e2:
                    err_text = e2.stderr or str(e2)
                    continue
            if not repaired:
                print("🛑 [Repair] Failed; skipping candidate")
                continue

        # evaluate across workloads (skip workloads that timeout; require >=1)
        total = 0.0
        seen = 0
        for trace_info in workloads:
            WORKLOAD = trace_info["name"]
            trace_path = trace_info["trace_path"]
            out = run_policy(exe, trace_path)
            if out is None:
                continue  # timed out
            try:
                tmp = parse_hit_rate(out)
            except Exception as e:
                print(f"⚠️  [Parse Metric] {iter_label} {WORKLOAD}: {e} → skipping this workload")
                continue
            total += tmp
            seen += 1
            record(WORKLOAD, name, desc, cc, tmp, f"island={isl.id}; iter={i}")
            print(f"      [+] {iter_label} → {name} → workload: {WORKLOAD} → hit rate: {tmp:.6f}")

        if seen == 0:
            print(f"🛑 [Skip] {iter_label}: no valid workloads completed → skipping candidate\n")
            continue

        current_hit = total / seen
        print(f"✅ [Result] {iter_label}: {name}  → average hit rate {current_hit:.2%} over {seen}/{len(workloads)} workloads\n")

        # 8) Record experiment
        record("all", name, desc, cc, current_hit, f"island={isl.id}; iter={i}; avg_over_all_workloads")

        # 9) Update per-island / global best + prepare next prompt state
        if isl.best_score < 0 or current_hit > isl.best_score:
            isl.best_score = current_hit
            isl.best_name = name
            isl.best_desc = desc
        isl.prev_name, isl.prev_desc, isl.prev_code = name, desc, code

        if current_hit > global_best:
            global_best = current_hit

        # 10) minimal migration every MIGRATE_EVERY full island passes
        # A "full pass" completes when (i+1) % N_ISLANDS == 0
        passes_done = (i + 1) // N_ISLANDS
        if N_ISLANDS > 1 and (i + 1) % N_ISLANDS == 0 and passes_done > 0 and passes_done % MIGRATE_EVERY == 0:
            # copy best island's latest design into worst island's "prev_*" to nudge it
            best_island = max(islands, key=lambda s: (s.best_score if s.best_score is not None else -1))
            worst_island = min(islands, key=lambda s: (s.best_score if s.best_score is not None else 1e9))
            if best_island.prev_name and best_island.prev_code:
                print(f"🔁 [Migration] pass {passes_done}: copy best from isl {best_island.id} "
                      f"(score={best_island.best_score:.4f}) → isl {worst_island.id}")
                worst_island.prev_name = best_island.prev_name
                worst_island.prev_desc = best_island.prev_desc
                worst_island.prev_code = best_island.prev_code

    prompt_gen.close()
    rag.close()


if __name__ == "__main__":
    main()
