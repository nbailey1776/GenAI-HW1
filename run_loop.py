#!/usr/bin/env python3
import os
from dotenv import load_dotenv
import re
import sqlite3
import subprocess
from pathlib import Path
from typing import Optional, Tuple

from openai import OpenAI
from RAG import ExperimentRAG
from PromptGenerator import PolicyPromptGenerator

# ──────────────────────────────────────────────────────────────────────────────
# Configuration
# ──────────────────────────────────────────────────────────────────────────────
DB_PATH = "funsearch.db"
WORKLOAD = "Astar"
TRACE_PATH = Path("ChampSim_CRC2/trace/astar_313B.trace.gz")
LIB_PATH = "ChampSim_CRC2/lib/config1.a"
INCLUDE_DIR = "ChampSim_CRC2/inc"
EXAMPLE_DIR = Path("ChampSim_CRC2/example")
WARMUP_INST = "1000000"
SIM_INST = "10000000"
MODEL = "o4-mini"
ITERATIONS = 5

EXAMPLE_DIR.mkdir(parents=True, exist_ok=True)


# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────
def sanitize(name: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in name).strip("_").lower()


def parse_policy_content(
    text: str,
) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    def _extract(pattern: str):
        m = re.search(pattern, text, flags=re.DOTALL | re.IGNORECASE)
        return m.group(1).strip() if m else None

    name = _extract(r"##\s*Policy\s*Name\s*\n(.*?)\n")
    desc = _extract(r"##\s*Policy\s*Description\s*\n(.*?)\n")
    code = _extract(r"```cpp\s*(.*?)\s*```")
    return name, desc, code


def compile_policy(cc: Path) -> Path:
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
    )
    return exe


def run_policy(exe: Path) -> str:
    res = subprocess.run(
        [
            str(exe),
            "-warmup_instructions",
            WARMUP_INST,
            "-simulation_instructions",
            SIM_INST,
            "-traces",
            str(TRACE_PATH),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return res.stdout


def parse_hit_rate(output: str) -> float:
    m = re.search(r"LLC TOTAL\s+ACCESS:\s+(\d+)\s+HIT:\s+(\d+)", output)
    if not m:
        raise RuntimeError("LLC TOTAL not found")
    return int(m.group(2)) / int(m.group(1))


def record(name, desc, cc: Path, rate, workload_desc):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute(
        """
      INSERT INTO experiments
        (workload, policy, policy_description, workload_description,
         cpp_file_path, cache_hit_rate, score)
      VALUES (?, ?, ?, ?, ?, ?, ?)""",
        (WORKLOAD, name, desc, workload_desc, str(cc), rate, rate),
    )
    conn.commit()
    conn.close()


# ──────────────────────────────────────────────────────────────────────────────
# Main Feedback Loop with Reward/Penalty
# ──────────────────────────────────────────────────────────────────────────────
def main():
    # 1) Setup RAG and PromptGenerator
    rag = ExperimentRAG(DB_PATH)
    prompt_gen = PolicyPromptGenerator(DB_PATH)
    load_dotenv(dotenv_path=Path(".env"), override=False)

    client = OpenAI(
        api_key=os.getenv("OPENAI_API_KEY"),
    )

    # 2) Stash workload description
    top2 = rag.get_top_policies_by_cache_hit(WORKLOAD, top_n=2)
    if not top2:
        raise RuntimeError("No RAG data for workload")
    workload_desc = top2[0]["workload_description"]

    # 3) Initialize best_hit from the database
    top1 = rag.get_top_policies_by_cache_hit(WORKLOAD, top_n=1)
    best_hit = top1[0]["cache_hit_rate"] if top1 else 0.0
    print(f"Starting best_hit (from DB): {best_hit:.2%}")

    prev_name = prev_desc = prev_code = None
    current_hit = best_hit

    for i in range(ITERATIONS):
        # 4) Build prompt
        if i == 0:
            prompt = prompt_gen.generate_prompt(WORKLOAD)
        else:
            # Reward vs. Penalty message
            if current_hit > best_hit:
                feedback = (
                    f"Great! Policy improved from {best_hit:.2%} to "
                    f"{current_hit:.2%}. Please refine further."
                )
                best_hit = current_hit
            else:
                feedback = (
                    f"Policy hit rate was {current_hit:.2%}, not better than "
                    f"{best_hit:.2%}. Try a different approach."
                )
            prompt = (
                f"You previously designed **{prev_name}**:\n\n"
                f"Description:\n{prev_desc}\n\n"
                f"Implementation:\n```cpp\n{prev_code}\n```\n\n"
                f"{feedback}\n\n"
                "Now improve or redesign this cache replacement policy for the same workload.\n"
                "Use the exact output format:\n\n"
                "## Policy Name\n<name>\n\n"
                "## Policy Description\n<one paragraph>\n\n"
                "## C++ Implementation\n"
                f"{prompt_gen._get_code_template()}\n"
            )

        # 5) Call model
        resp = client.chat.completions.create(
            model=MODEL,
            messages=[{"role": "user", "content": prompt}],
            # temperature=0.3
        )
        text = resp.choices[0].message.content

        # 6) Parse LLM output
        name, desc, code = parse_policy_content(text)
        if not (name and desc and code):
            raise RuntimeError(f"Parse failed at iteration {i}")

        # 7) Write, compile, run
        base = sanitize(name)
        cc = EXAMPLE_DIR / f"{base}.cc"
        cc.write_text(code, encoding="utf-8")
        exe = compile_policy(cc)
        out = run_policy(exe)
        current_hit = parse_hit_rate(out)
        print(f"[Iter {i}] {name} → hit rate {current_hit:.2%}")

        # 8) Record experiment
        record(name, desc, cc, current_hit, workload_desc)

        # 9) Prepare for next iteration
        prev_name, prev_desc, prev_code = name, desc, code

    prompt_gen.close()
    rag.close()


if __name__ == "__main__":
    main()
