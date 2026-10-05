# CPU Cache & Memory Hierarchy Simulator

A trace-driven simulator of a multi-level CPU cache hierarchy written in C++17,
with a Streamlit dashboard for comparing cache configurations and policies.

- Direct-mapped, N-way set-associative and fully-associative caches
- LRU, FIFO and Random replacement
- Write-back + write-allocate and write-through + no-write-allocate
- Any number of levels (L1 → L2 → L3 → …) in front of main memory
- Sequential, random and strided trace generators, plus a trace-file reader
- Hit/miss rates (local and global), **3C miss classification**
  (compulsory / capacity / conflict), evictions, writebacks, memory traffic
- AMAT, both **measured** (cycle-accurate sum over the trace) and from the
  **analytic formula**
- Text, CSV and JSON output; CMake build; 34 unit tests and 5 CLI tests

```
cache-simulator/
├── CMakeLists.txt
├── include/cachesim/        public headers (one per component)
│   ├── types.hpp            Addr, Cycles, MemAccess, policy enums
│   ├── config.hpp           CacheConfig / TraceConfig / SimConfig + parsers
│   ├── replacement_policy.hpp  LRU, FIFO, Random behind one interface
│   ├── memory_level.hpp     MemoryLevel interface + MainMemory
│   ├── cache.hpp            one cache level (sets × ways of lines)
│   ├── hierarchy.hpp        wires L1…Ln → memory, collects results
│   ├── trace.hpp            generators + trace file I/O
│   └── report.hpp           text / CSV / JSON writers
├── src/                     implementations + main.cpp (CLI)
├── tests/                   unit tests (self-contained framework, no downloads)
├── dashboard/               Streamlit app + Python wrapper around the binary
├── scripts/                 experiment runner, matrix-multiply trace generator
├── configs/                 example config files
├── traces/                  example traces (8×8 transpose, 64×64 matmul naive/tiled)
└── sample_output/           results produced by scripts/run_experiments.py
```

## Build and run

Requirements: a C++17 compiler (GCC ≥ 8, Clang ≥ 7, MSVC 2019) and CMake ≥ 3.16.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure     # 6 CTest targets, 34 unit cases

./build/cachesim --help
./build/cachesim                                  # default 3-level hierarchy, sequential trace
./build/cachesim --config configs/default.cfg     # options from a file
./build/cachesim --level name=L1,size=32K,assoc=8,block=64,policy=lru,write=wb,hit=4 \
                 --level name=L2,size=256K,assoc=8,block=64,policy=lru,write=wb,hit=12 \
                 --mem-latency 200 --pattern random --working-set 1M --accesses 200000 \
                 --format json --output result.json
./build/cachesim --trace traces/matmul_tiled_64.trace --level size=8K,assoc=4 --level name=L2,size=64K
```

Sanitizer build: `cmake -S . -B build-asan -DCACHESIM_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`.

### In VS Code

Open the `cache-simulator` folder (File → Open Folder) and install the recommended
extensions when prompted (C/C++, CMake Tools, Python). Then:

- **Ctrl+Shift+B** builds (configure + compile, Debug)
- **Terminal → Run Task → test** runs CTest; tests also show in the Testing panel
- **Run and Debug (F5)**: pick `cachesim (…)` for your OS to run/debug the simulator with
  `configs/default.cfg`, or `Streamlit dashboard` to launch the UI
- **Terminal → Run Task → dashboard / run experiments** for the other entry points

On Windows you need a compiler first: either *Visual Studio Build Tools* (C++ workload,
use the MSVC launch config) or MinGW-w64 (use the MinGW one), plus CMake and Python 3.

### Dashboard

```bash
pip install -r dashboard/requirements.txt
streamlit run dashboard/app.py
```

The dashboard finds the binary at `build/cachesim` (or `$CACHESIM_BIN`, or on
`$PATH`) and offers to build it with CMake if it is missing. It has four tabs:

| Tab | What it shows |
|---|---|
| Hierarchy | Configure 1–3 levels; AMAT (measured and formula), hit rate per level, 3C miss breakdown, full stats table, equivalent CLI command, JSON download |
| Policy comparison | LRU / FIFO / Random × write-back / write-through × sequential / random / strided: L1 hit rate, AMAT, L1 write traffic |
| Parameter sweep | Sweep L1 size, associativity or block size per pattern; hit rate and AMAT curves; CSV download |
| Access patterns | Scatter plots of the generated address streams |

The dashboard contains no simulation logic: it shells out to `cachesim --format json`
and plots the result, so the CLI and the UI can never disagree. Independent runs
in a sweep execute in parallel processes.

## Command-line reference

| Option | Meaning | Default |
|---|---|---|
| `--level SPEC` | Add a level (first is L1). Keys: `name`, `size` (`32K`, `1M`), `assoc` (N or `full`), `block`, `policy` (`lru`/`fifo`/`random`), `write` (`wb`/`wt`), `hit` (cycles) | 3-level default |
| `--mem-latency N` | Main memory latency, cycles | 200 |
| `--pattern P` | `sequential`, `random`, `strided`, `file` | sequential |
| `--trace PATH` | Trace file (`R 0x1000 [size]` / `W …` per line; Dinero `0`/`1` ops accepted) | |
| `--accesses N` | Accesses to generate | 100000 |
| `--working-set SIZE` | Region the addresses wrap inside | 256K |
| `--element SIZE` | Bytes per access | 4 |
| `--stride SIZE` | Stride for `strided` | 64 |
| `--write-ratio F` | Fraction of stores | 0.3 |
| `--seed N` | Seed for trace generation and Random replacement | 42 |
| `--format F` | `text`, `csv`, `json` | text |
| `--output PATH`, `--no-header`, `--label`, `--dump-trace PATH`, `--config PATH` | | |

A config file holds the same options as `option = value` lines (see `configs/`).
Exit status is 0 on success and 2 on a configuration error (for example a
cache whose set count is not a power of two).

## Design

### Data flow

```
 trace generator / file ──► Hierarchy::access(MemAccess)
                               │
                               ▼
                    ┌────── Cache L1 ──────┐   access()   : demand path, returns latency
                    │ sets × ways of Lines │   writeDown(): writebacks / write-through stores
                    │ ReplacementPolicy    │                (off the critical path)
                    └──────────┬───────────┘
                               ▼  (MemoryLevel* next)
                           Cache L2 … Ln
                               ▼
                          MainMemory (fixed latency, traffic counters)
```

Every level implements the `MemoryLevel` interface, so a cache does not know
whether it misses into another cache or into DRAM. That is what makes the
hierarchy depth arbitrary.

### One cache level

A block address is split into tag, set index and offset:

```
| tag ........................ | index (log2 sets) | offset (log2 block) |
sets = size / (block × ways)      direct-mapped: ways = 1      fully associative: sets = 1
```

Each line stores `{tag, valid, dirty}` (no data: only the timing and
bookkeeping matter). The geometry is validated up front: block and set counts
must be powers of two, exactly as real index hardware requires.

**Replacement** (`replacement_policy.hpp`). Invalid ways are filled first;
only a full set consults the policy.

| Policy | Metadata | On hit | Victim |
|---|---|---|---|
| LRU | last-use timestamp per line (global logical clock) | update stamp | min stamp |
| FIFO | fill timestamp per line | nothing | min stamp |
| Random | seeded `mt19937_64` | nothing | uniform way |

A timestamp is used instead of a linked list per set: an O(1) hit update and an
O(ways) victim search, which is cheaper than list surgery for the usual 2–16 ways.

**Write policies.**

| | Store hit | Store miss | Eviction |
|---|---|---|---|
| Write-back + write-allocate | mark line dirty | fetch block, then write, mark dirty | dirty victim written to next level |
| Write-through + no-write-allocate | update line, forward word to next level | forward word, do **not** allocate | nothing to write |

A writeback arriving at a lower write-back cache that misses is allocated
without a fetch (the whole block is being overwritten). A partial write
(a forwarded write-through word) to a write-back level does fetch the block first.

**Latency model.** A hit costs `hit_latency`. A miss costs this level's
`hit_latency` (the tag lookup) plus whatever the next level returns for the
block fill, so latency composes recursively. Writebacks and write-through
stores are assumed to drain through a write buffer and do not stall the CPU.

**AMAT.** Two values are reported:

- *measured* = total cycles over the trace / CPU accesses
- *formula* = `h1 + m1·(h2 + m2·(h3 + m3·Tmem))` with each level's local miss rate

For an all-write-back hierarchy the two are identical, and a unit test checks
this to 1e-9. With write-through, a store miss costs only the hit time (it is buffered),
so the measured AMAT is lower than the formula. The difference is the stall
time the write buffer hides.

**3C classification.** Each cache keeps a set of every block it has ever seen
and a *shadow fully-associative LRU cache* with the same number of lines. A demand miss is

- **compulsory** if the block was never seen before,
- **conflict** if the shadow fully-associative cache would have hit,
- **capacity** otherwise.

That is the Hill & Smith definition. A fully-associative LRU configuration therefore
always reports zero conflict misses, and a unit test checks that.

**Inclusion.** Levels are non-inclusive / non-exclusive (NINE): a fill goes
into every level it passes through, but an eviction in L2 does not
back-invalidate L1. That is what most simple simulators and many real L2s do.

### Trace generators

| Pattern | Address of access *i* | Models |
|---|---|---|
| sequential | `base + (i·elem) mod WS` | array scan, repeated passes |
| strided | column-major walk: `stride` bytes per step, shifted one element each pass | `a[i][j]` traversed by column, matrix B in naive matmul |
| random | `base + U(0, WS/elem)·elem` | hash tables, pointer chasing |
| file | read from disk | real program traces (`scripts/gen_matmul_trace.py`) |

Each access is a store with probability `write_ratio` (seeded Bernoulli), so
every run is reproducible.

## Results

All numbers below come from `python3 scripts/run_experiments.py`
(200 000 accesses, 4-byte elements, 30 % stores, seed 42, memory 200 cycles).
The raw CSV/JSON is in `sample_output/`.

**Default 3-level hierarchy on a 1 MB random working set** (`sample_output/default_random.txt`):

```
Level   Accesses       Hits     Misses     Hit%   Global%   Compuls.  Capacity  Conflict     Evict    WrBack
L1        200000       6346     193654     3.17     96.83      16384    176457       813    193142     59209
L2        193654      43287     150367    22.35     75.18      16384    128259      5724    146271     52567
L3        150367     133983      16384    89.10      8.19      16384         0         0         0         0

Main memory: 16384 reads (1MB), 0 writes (0B)
AMAT (measured)   : 62.077 cycles
AMAT (formula)    : 62.077 cycles  = h1 + m1*(h2 + m2*(... + Tmem))
```

The hit rate of a uniformly random stream is about cache size / working set
(32K/1M ≈ 3 %, 256K/1M ≈ 25 %). The 2 MB L3 holds the whole 1 MB working set,
so its only misses are the 16 384 compulsory ones, and no dirty data ever
reaches memory during the run.

**Replacement policy, L1 32K 4-way, 64K working set** (`policy_comparison.csv`, write-back):

| Pattern | LRU | FIFO | Random |
|---|---|---|---|
| sequential (loop over 2× L1) | 93.75 % | 93.75 % | **94.78 %** |
| random | 49.68 % | 49.74 % | 49.87 % |
| strided 4K | 0.00 % | 0.00 % | **17.62 %** |

A cyclic access pattern larger than the cache is LRU's worst case: it always
evicts exactly the block that will be needed next, so only spatial hits within
a block remain. Random replacement keeps a fraction of the loop resident.
On uniform random traffic all three policies are equivalent, as theory predicts.

**L1 size sweep, 8-way, 256K working set** (`l1_size_sweep.csv`):

| L1 size | 4K | 16K | 64K | 128K | 256K |
|---|---|---|---|---|---|
| sequential | 93.75 % | 93.75 % | 93.75 % | 93.75 % | **97.95 %** |
| random | 1.55 % | 6.25 % | 25.00 % | 49.84 % | **97.95 %** |
| strided 4K | 0 % | 0 % | 0 % | 0 % | **97.95 %** |

Sequential shows the LRU "cliff": no temporal reuse at all until the whole loop fits.
For strided 4K, the 3C columns show the misses moving from *capacity* (4K, 8K)
to *conflict* (16K–128K): all 64 blocks of a pass map to the same few sets.

**Associativity, 32K L1** (`associativity_sweep.csv`): on a random 48K working
set the hit rate stays flat at about 66.6 %, but conflict misses fall from 29 130
(direct-mapped) to 0 (fully associative) and turn into capacity misses. On a
32K-stride pattern, 8 blocks all map to one set: hit rate is 0 % up to 4-way
and jumps to **93.75 % at 8-way**.

**Block size, sequential** (`block_size_sweep.csv`): hit rate = 1 − elem/block:
16 B → 75 %, 32 B → 87.5 %, 64 B → 93.75 %, 128 B → 96.9 %, 256 B → 98.4 %.

**Write-back vs write-through, single 32K L1, random** (`write_policy_traffic.csv`):

| Working set | WB bytes written to memory | WT bytes written to memory |
|---|---|---|
| 8K (fits) | 0 | 239 744 |
| 64K | 2 941 312 | 239 744 |
| 1M | 3 789 376 | 239 744 |

Write-back wins clearly while the written lines stay resident: repeated
stores coalesce into one dirty line. When lines are evicted soon after being
written, each eviction writes a whole 64 B block for about 1 dirty word, and
write-through's 4 B per store moves fewer bytes. This is the reason real
systems put write-back caches close to the core and write-combining buffers behind them.

**Real program traces: 64×64 double matmul, L1 8K 4-way + L2 64K** (`matmul_traces.csv`):

| Variant | L1 hit rate | AMAT |
|---|---|---|
| naive i-j-k | 48.99 % | 10.70 cycles |
| tiled (16×16) | **82.87 %** | **6.61 cycles** |

Loop tiling keeps a 16×16 tile of B (2 KB) resident in L1 instead of streaming
a 512-byte-stride column of B through it.

## Testing

`tests/` uses a small self-contained test framework, so the project builds offline.
The tests cover:

- address decomposition, geometry validation, size and spec parsing
- LRU vs FIFO on a sequence that tells them apart (A B A C A), Random determinism
  and range, LRU/FIFO thrashing on a cyclic pattern larger than the set
- cold sequential hit rate = 15/16, direct-mapped ping-pong conflicts, 2-way fixing them,
  fully-associative capacity misses with zero conflict misses
- write-back: dirty eviction → exactly one 64 B write; clean eviction → none;
  write-allocate fetch
- write-through: every store reaches memory, no-write-allocate on store miss
- hierarchy latencies per level, dirty L1 victims landing in L2 rather than in memory,
  measured AMAT = formula, memory reads = last-level misses, each level's accesses
  = the misses of the level above, LRU stack property (more ways never fewer hits)
- trace generators (stride, wrap, alignment, seeding, write ratio), trace file round trip
- CLI: help, JSON, trace file, config file, and a non-zero exit on a bad geometry

The suite also runs clean under AddressSanitizer and UBSan (`-DCACHESIM_SANITIZE=ON`).

## Assumptions and limitations

- Timing is a latency model, not a pipeline model: no MLP, prefetching,
  bank conflicts or bus contention. Writes to lower levels drain through a write buffer with unlimited capacity.
- Accesses are assumed not to straddle a block boundary (all generators align them).
- Physical addresses; there is no TLB or virtual-to-physical translation.
- Dirty lines left at the end of a run are reported (`dirty_lines_at_end`) and not flushed.
- Single core, so there is no coherence protocol.

Natural extensions: a TLB/page-walk level, a next-line or stride prefetcher,
a victim cache, inclusive/exclusive policies, PLRU / RRIP replacement, and
MESI coherence for multiple cores.
