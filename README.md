# Spruce

**A from-scratch LSM-tree storage engine in C++20** with [Vertiorizon](https://arxiv.org/abs/2504.17178) ([SIGMOD 2025](https://doi.org/10.1145/3725310)) as its native growth policy.

**Repository:** https://github.com/shrivasshankar/Spruce

Single-threaded, STL-only. WAL durability, block SSTables with Bloom filters, horizontal tiering (Algorithm 2), vertical handoff (L1v/L2v), and a three-way amplification benchmark vs pure horizontal and vertical tiering baselines.

## Features

- **Write path** — WAL → MemTable → flush → SST → compaction → MANIFEST
- **Read path** — MemTable → min/max → Bloom filter → SST probe (newest first)
- **Vertiorizon** — horizontal counters + tiering merges → L1v handoff → partial L1v→L2v compact
- **Growth modes** — Vertiorizon, horizontal-tiering (HR-Tier), vertical-tiering (VT-Tier) for comparison
- **Durability** — fsync SST + MANIFEST before WAL truncate; crash recovery via MANIFEST + WAL replay

## Build

Requires CMake 3.20+ and a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
```

## Test

```bash
./build/spruce
```

Prints `memtable ok` when all checks pass (MemTable, WAL, SSTable, engine flush/reopen, MANIFEST v2, Figure 5 counter replay, vertical compaction).

## Benchmark

```bash
./build/spruce_bench
```

| Metric | Meaning |
|--------|---------|
| **Write amp** | SST bytes on disk ÷ logical user bytes (key + value sizes) |
| **Read amp** | Average SST files **opened** per `Get` (after Bloom + min/max) |
| **Bloom skip** | Files rejected by min/max or Bloom without opening |

Each SST footer includes a Bloom filter (~10 bits/key). No false negatives; false positives only.

### Growth scheme comparison (250 puts, B=1024, T=2)

Same insert-only workload on each mode:

| Scheme | Write amp | Read amp | Bloom skips | SST files |
|--------|-----------|----------|-------------|-----------|
| **Vertiorizon** | 1.11x | 1.00x | **600** | **5** |
| Horizontal-tiering | 1.13x | 1.00x | 1640 | 13 |
| Vertical-tiering | 1.12x | 1.00x | 840 | 7 |

With Bloom filters, read amp is ~1 file opened per key for all schemes. Vertiorizon keeps the tree tighter (fewer SSTs, fewer filter checks). **Before Bloom**, read amp on this workload was **3.4x vs 7.6x vs 4.4x** — same write amp ~1.1x.

Synthetic micro-benchmark, not a full paper reproduction.

## Architecture

```
Put/Delete
   |
   v
 WAL (log.wal) -----> MemTable (sorted map, size B)
                           | IsFull()
                           v
                     Flush -> SST on level 0
                           |
                           v
                   MaybeCompact()  (Algorithm 2)
                     C[0]-- on each flush
                     C[i]==0 -> tiering merge level i -> i+1
                     last counter hits 0 -> handoff to L1v
                           |
                           v
              SyncManifestFromLevels() -> MANIFEST
```

**On disk:**

```
db/
├── MANIFEST     # SST catalog + levels + k + compaction counters
├── wal/log.wal  # in-flight writes (truncated after flush)
└── sst/NNNNNN.sst
```

**Durability on flush:** fsync SST → compact (maybe) → fsync MANIFEST → truncate WAL.

## Vertiorizon / Figure 5

Default horizontal config matches the paper's Figure 5 setup:

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `ell_horizontal` | 2 | horizontal levels |
| `estimated_data_buffers` | 6 | `N/B` estimate for Lemma 4.1 |
| `k` | 3 | smallest k with C(k+ℓ−1, ℓ) ≥ N/B |

Counters `C_i` start at `k`. Each flush decrements `C[0]`. When `C[i]` hits zero, level `i` is compacted into a **new run** on level `i+1` (tiering). When the last horizontal counter hits zero, all horizontal data hands off to **L1v**; overflow compacts partially into **L2v**.

**Implemented:** Algorithm 2 horizontal tiering, L1v/L2v vertical tail, partial overlapping merge, HR/VT comparison modes, Bloom filters.

**Not implemented:** dynamic `n`, file-granular partial compact, self-tuning policy, full RocksDB-scale evaluation.

## Project layout

```
include/lsm/   config, memtable, wal, sstable, bloom, engine, manifest
src/           implementations, main.cpp (tests), benchmark.cpp
```

## Reference

- [How to Grow an LSM-tree? (Vertiorizon, SIGMOD 2025)](https://arxiv.org/abs/2504.17178) — [ACM DOI](https://doi.org/10.1145/3725310)
- Kleppmann, *Designing Data-Intensive Applications* — Ch. 3 (LSM storage) is the best companion read for the core ideas
