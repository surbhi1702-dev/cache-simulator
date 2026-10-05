#!/usr/bin/env python3
"""Reproduce the experiments in the README and write CSVs to sample_output/.

    python3 scripts/run_experiments.py            # uses build/cachesim or $CACHESIM_BIN
"""
from __future__ import annotations

import csv
import subprocess
import sys
from dataclasses import replace
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "dashboard"))
import simulator as sim  # noqa: E402

OUT = ROOT / "sample_output"
PATTERNS = ["sequential", "random", "strided"]
BASE_TRACE = sim.Trace(accesses=200_000, working_set="1M", stride="4K", write_ratio=0.3, seed=42)
L2 = sim.Level("L2", "256K", "8", 64, "lru", "wb", 12)
L3 = sim.Level("L3", "2M", "16", 64, "lru", "wb", 40)


def write_csv(name: str, rows: list[dict]) -> None:
    with open(OUT / name, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote sample_output/{name} ({len(rows)} rows)")


def main() -> int:
    binary = sim.find_binary()
    if binary is None:
        print("cachesim binary not found: build with CMake first or set CACHESIM_BIN", file=sys.stderr)
        return 1
    OUT.mkdir(exist_ok=True)

    # 1. Replacement x write policy x pattern, L1 32K 4-way over a 256K L2.
    #    64K working set = 2x L1, so the policies actually have to choose.
    grid = [(p, pol, wp) for p in PATTERNS for pol in ("lru", "fifo", "random") for wp in ("wb", "wt")]
    specs = [sim.RunSpec((sim.Level("L1", "32K", "4", 64, pol, wp, 4), L2),
                         replace(BASE_TRACE, pattern=p, working_set="64K"), 200) for p, pol, wp in grid]
    rows = []
    for (p, pol, wp), r in zip(grid, sim.run_many(specs, binary)):
        l1 = r["levels"][0]
        rows.append({"pattern": p, "replacement": l1["replacement"], "write_policy": l1["write_policy"],
                     "l1_hit_rate": round(l1["hit_rate"], 4), "l2_hit_rate": round(r["levels"][1]["hit_rate"], 4),
                     "amat": round(r["amat"], 3), "l1_writebacks": l1["writebacks"],
                     "l1_write_throughs": l1["write_throughs"],
                     "l1_write_traffic_bytes": l1["writebacks"] * 64 + l1["write_throughs"] * 4,
                     "mem_bytes_read": r["memory"]["bytes_read"],
                     "mem_bytes_written": r["memory"]["bytes_written"]})
    write_csv("policy_comparison.csv", rows)

    # 2. L1 size sweep (8-way, LRU, write-back) for each pattern, over L2 + L3.
    #    256K working set, so sequential makes ~3 passes (LRU loop cliff).
    sizes = ["4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K", "1M"]
    grid = [(p, s) for p in PATTERNS for s in sizes]
    specs = [sim.RunSpec((sim.Level("L1", s, "8", 64, "lru", "wb", 4), sim.Level("L2", "4M", "16", 64, "lru", "wb", 12)),
                         replace(BASE_TRACE, pattern=p, working_set="256K"), 200) for p, s in grid]
    write_csv("l1_size_sweep.csv", [
        {"pattern": p, "l1_size": s, "l1_hit_rate": round(r["levels"][0]["hit_rate"], 4), "amat": round(r["amat"], 3),
         "compulsory": r["levels"][0]["compulsory"], "capacity": r["levels"][0]["capacity"],
         "conflict": r["levels"][0]["conflict"]}
        for (p, s), r in zip(grid, sim.run_many(specs, binary))])

    # 3. Associativity sweep at fixed 32K.
    #    random 48K   : conflict misses turn into capacity misses, hit rate ~flat
    #    strided 32K  : 8 blocks that all map to one set; thrashes until 8 ways
    ways = ["1", "2", "4", "8", "16", "full"]
    traces = {"random": replace(BASE_TRACE, pattern="random", working_set="48K"),
              "strided": replace(BASE_TRACE, pattern="strided", working_set="256K", stride="32K")}
    grid = [(p, w) for p in traces for w in ways]
    specs = [sim.RunSpec((sim.Level("L1", "32K", w, 64, "lru", "wb", 4), L2), traces[p], 200) for p, w in grid]
    write_csv("associativity_sweep.csv", [
        {"pattern": p, "ways": w, "organisation": r["levels"][0]["organisation"], "l1_hit_rate": round(r["levels"][0]["hit_rate"], 4),
         "amat": round(r["amat"], 3), "compulsory": r["levels"][0]["compulsory"],
         "capacity": r["levels"][0]["capacity"], "conflict": r["levels"][0]["conflict"]}
        for (p, w), r in zip(grid, sim.run_many(specs, binary))])

    # 4. Block size sweep on a sequential scan: spatial locality.
    blocks = [16, 32, 64, 128, 256]
    trace = replace(BASE_TRACE, pattern="sequential")
    specs = [sim.RunSpec((sim.Level("L1", "32K", "8", b, "lru", "wb", 4), sim.Level("L2", "256K", "8", 256, "lru", "wb", 12)),
                         trace, 200) for b in blocks]
    write_csv("block_size_sweep.csv", [
        {"block_bytes": b, "l1_hit_rate": round(r["levels"][0]["hit_rate"], 4), "amat": round(r["amat"], 3)}
        for b, r in zip(blocks, sim.run_many(specs, binary))])

    # 5. Write-back vs write-through traffic as the working set outgrows L1.
    grid = [(ws, wp) for ws in ("8K", "16K", "64K", "256K", "1M") for wp in ("wb", "wt")]
    specs = [sim.RunSpec((sim.Level("L1", "32K", "8", 64, "lru", wp, 4),), replace(BASE_TRACE, pattern="random", working_set=ws),
                         200) for ws, wp in grid]
    write_csv("write_policy_traffic.csv", [
        {"working_set": ws, "write_policy": r["levels"][0]["write_policy"], "l1_hit_rate": round(r["levels"][0]["hit_rate"], 4),
         "amat": round(r["amat"], 3), "mem_bytes_written": r["memory"]["bytes_written"],
         "mem_bytes_read": r["memory"]["bytes_read"]}
        for (ws, wp), r in zip(grid, sim.run_many(specs, binary))])

    # 6. Real program traces: naive vs loop-tiled matrix multiply.
    traces_dir = ROOT / "traces"
    rows = []
    for name in ("matmul_naive_64", "matmul_tiled_64"):
        path = traces_dir / f"{name}.trace"
        if not path.exists():
            continue
        spec = sim.RunSpec((sim.Level("L1", "8K", "4", 64, "lru", "wb", 4), sim.Level("L2", "64K", "8", 64, "lru", "wb", 12)),
                           sim.Trace(pattern="file", path=str(path)), 200)
        r = sim.run(spec, binary)
        rows.append({"trace": name, "accesses": r["trace"]["accesses"], "l1_hit_rate": round(r["levels"][0]["hit_rate"], 4),
                     "l2_hit_rate": round(r["levels"][1]["hit_rate"], 4), "amat": round(r["amat"], 3),
                     "mem_bytes_read": r["memory"]["bytes_read"]})
    if rows:
        write_csv("matmul_traces.csv", rows)

    # 7. Full JSON/CSV/text reports of the default 3-level hierarchy, per pattern.
    for p in PATTERNS:
        spec = sim.RunSpec((sim.Level("L1", "32K", "8", 64, "lru", "wb", 4), L2, L3), replace(BASE_TRACE, pattern=p), 200,
                           label=f"default-{p}")
        for fmt, ext in (("text", "txt"), ("json", "json"), ("csv", "csv")):
            out = subprocess.run(sim.command(spec, binary, fmt), capture_output=True, text=True, check=True).stdout
            (OUT / f"default_{p}.{ext}").write_text(out)
        print(f"wrote sample_output/default_{p}.{{txt,json,csv}}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
