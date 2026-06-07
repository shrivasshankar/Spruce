# Spruce

Single-threaded LSM-tree storage engine in C++20 (STL only). Spruce uses **Vertiorizon horizontal tiering** ([SIGMOD 2025](https://doi.org/10.1145/3654923)) as its native growth policy — not a bolt-on compaction plugin.

## Build

Requires CMake 3.20+ and a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
```

## Run tests

```bash
./build/spruce
```

Prints `memtable ok` when all unit checks pass (MemTable, WAL, SSTable, engine flush/reopen, MANIFEST v2, Figure 5 counter replay, compaction).

## Benchmark (write / read amplification)

```bash
./build/spruce_bench
```

Measures a 300-key workload with Figure 5 parameters (`ell=2`, `N/B=6`, `k=3`):

- **Write amp** — SST bytes on disk divided by logical user bytes (Put key + value sizes). Compaction rewrites data, so this is above 1x.
- **Read amp** — average number of SST files probed per point `Get` on a reopened database (MemTable miss path).

Example output shape:

```
Spruce amplification benchmark
  config: B=4096 ell=2 N/B=6 k=3
  workload: 300 puts, value_size=100
  user bytes: ...
  SST bytes on disk: ...
  write amp (SST / user): ...x
  read amp (avg SST files probed / get): ...x
```

## Architecture

```
Put/Delete
   |
   v
 WAL (log.wal) -----> MemTable (sorted map, size B)
                           | IsFull()
                           v
                     Flush -> SST run on level 0
                           |
                           v
                   MaybeCompact()  (Algorithm 2)
                     C[0]-- on each flush
                     C[i]==0 -> merge level i into new run on i+1
                           |
                           v
              SyncManifestFromLevels() -> MANIFEST
```

**On disk:**

```
db/
├── MANIFEST     # SST catalog + level + k + compaction counters
├── wal/log.wal  # in-flight writes (truncated after flush)
└── sst/NNNNNN.sst
```

**Read path:** MemTable → level 0 (newest run first) → level 1 → … First hit wins.

**Durability on flush:** fsync SST → compact (maybe) → fsync MANIFEST → truncate WAL.

## Vertiorizon / Figure 5

Default horizontal config matches the paper’s Figure 5 setup:

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `ell_horizontal` | 2 | horizontal levels |
| `estimated_data_buffers` | 6 | `N/B` estimate for Lemma 4.1 |
| `k` | 3 | smallest k with C(k+ℓ-1, ℓ) ≥ N/B |

Counters `C_i` start at `k`. Each buffer flush decrements `C[0]`. When `C[i]` hits zero, level `i` is compacted into a **new run** on level `i+1` (tiering), `C[i+1]` decrements, and `C[0..i]` reset to `C[i+1]`.

MANIFEST v2 persists SST level placement and live counter state so reopen after compaction restores the correct LSM shape.

## Layout

```
include/lsm/   headers (config, memtable, wal, sstable, engine, manifest)
src/           implementations + main.cpp tests + benchmark.cpp
```

## Status

- MemTable, WAL, block SSTables, LSM engine glue
- Vertiorizon horizontal tiering compaction (Algorithm 2)
- MANIFEST v2 (SST levels + compaction counters)
- Figure 5 counter replay test
- Vertical tiering: horizontal→L1v handoff + partial L1v→L2v at capacity `n·T·B`
- Write / read amplification benchmark

**Not yet:** dynamic `n` growth when L2v fills (Phase 5b), self-tuning merge policy.

## Roadmap

1. Dynamic `n` when L2v reaches capacity
2. Optional: CI, scan iterator, mmap read path
