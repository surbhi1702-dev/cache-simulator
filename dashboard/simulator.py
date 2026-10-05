"""Thin Python wrapper around the `cachesim` C++ binary.

The dashboard never re-implements simulation logic: every number it shows comes
from the C++ simulator's JSON output, so the CLI and the UI cannot disagree.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class Level:
    name: str = "L1"
    size: str = "32K"
    assoc: str = "8"  # number of ways or "full"
    block: int = 64
    policy: str = "lru"  # lru | fifo | random
    write: str = "wb"  # wb | wt
    hit: int = 4

    def spec(self) -> str:
        return (f"name={self.name},size={self.size},assoc={self.assoc},block={self.block},"
                f"policy={self.policy},write={self.write},hit={self.hit}")


@dataclass(frozen=True)
class Trace:
    pattern: str = "random"  # sequential | random | strided | file
    accesses: int = 100_000
    working_set: str = "256K"
    element: int = 4
    stride: str = "64"
    write_ratio: float = 0.3
    seed: int = 42
    path: str = ""


@dataclass(frozen=True)
class RunSpec:
    levels: tuple[Level, ...] = field(default_factory=lambda: (Level(),))
    trace: Trace = field(default_factory=Trace)
    mem_latency: int = 200
    label: str = ""


def find_binary() -> Path | None:
    """Locate cachesim: $CACHESIM_BIN, then anywhere under ./build, then $PATH."""
    env = os.environ.get("CACHESIM_BIN", "")
    if env and Path(env).is_file():
        return Path(env)
    build = PROJECT_ROOT / "build"
    # Single-config generators put it in build/, Visual Studio in build/<Config>/.
    preferred = [build / n for n in ("cachesim", "cachesim.exe")]
    preferred += [build / cfg / "cachesim.exe" for cfg in ("Release", "Debug", "RelWithDebInfo")]
    found = [p for p in preferred if p.is_file()]
    if not found and build.is_dir():
        found = sorted(p for p in build.rglob("cachesim*") if p.is_file() and p.stem == "cachesim"
                       and p.suffix in ("", ".exe"))
    for p in found:
        if os.name == "nt" or os.access(p, os.X_OK):
            return p
    which = shutil.which("cachesim")
    return Path(which) if which else None


def build_binary() -> tuple[bool, str]:
    """Configure and build the C++ project with CMake (used by the UI button)."""
    build = PROJECT_ROOT / "build"
    log = []
    for cmd in (["cmake", "-S", str(PROJECT_ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"],
                ["cmake", "--build", str(build), "-j"]):
        p = subprocess.run(cmd, capture_output=True, text=True)
        log.append(p.stdout + p.stderr)
        if p.returncode != 0:
            return False, "\n".join(log)
    return True, "\n".join(log)


def command(spec: RunSpec, binary: Path, fmt: str = "json") -> list[str]:
    t = spec.trace
    cmd = [str(binary), "--format", fmt, "--mem-latency", str(spec.mem_latency), "--seed", str(t.seed)]
    for lvl in spec.levels:
        cmd += ["--level", lvl.spec()]
    if t.pattern == "file":
        cmd += ["--trace", t.path]
    else:
        cmd += ["--pattern", t.pattern, "--accesses", str(t.accesses), "--working-set", t.working_set,
                "--element", str(t.element), "--stride", str(t.stride), "--write-ratio", str(t.write_ratio)]
    if spec.label:
        cmd += ["--label", spec.label]
    return cmd


class SimulationError(RuntimeError):
    pass


def run(spec: RunSpec, binary: Path) -> dict:
    p = subprocess.run(command(spec, binary), capture_output=True, text=True)
    if p.returncode != 0:
        raise SimulationError(p.stderr.strip() or f"cachesim exited with {p.returncode}")
    return json.loads(p.stdout)


def run_many(specs: list[RunSpec], binary: Path, workers: int | None = None) -> list[dict]:
    """Run independent simulations in parallel (each is a separate process)."""
    with ThreadPoolExecutor(max_workers=workers or min(8, os.cpu_count() or 2)) as pool:
        return list(pool.map(lambda s: run(s, binary), specs))


def sample_addresses(trace: Trace, binary: Path, n: int = 2000) -> list[tuple[int, str]]:
    """Generate the first n accesses of a trace (via --dump-trace) for plotting."""
    small = Trace(**{**asdict(trace), "accesses": min(n, trace.accesses)})
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "t.trace"
        cmd = command(RunSpec(trace=small), binary) + ["--dump-trace", str(out)]
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            raise SimulationError(p.stderr.strip())
        rows = []
        for line in out.read_text().splitlines():
            if line.startswith("#") or not line.strip():
                continue
            op, addr, *_ = line.split()
            rows.append((int(addr, 16), op))
        return rows[:n]


def flatten(result: dict) -> list[dict]:
    """One row per cache level, with run-level fields repeated (tidy format)."""
    rows = []
    for lvl in result["levels"]:
        row = {"label": result["label"], "pattern": result["trace"]["pattern"],
               "amat": result["amat"], "amat_formula": result["amat_formula"],
               "mem_reads": result["memory"]["reads"], "mem_writes": result["memory"]["writes"],
               "mem_bytes_read": result["memory"]["bytes_read"],
               "mem_bytes_written": result["memory"]["bytes_written"]}
        row.update({f"{k}": v for k, v in lvl.items()})
        rows.append(row)
    return rows
