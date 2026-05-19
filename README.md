# Spruce

LSM-tree storage engine in C++, STL-only — summer learning project.

This repository was intentionally reset to an empty slate so implementation can be written step-by-step by hand after strengthening C++ fundamentals. Older scaffolding was removed on purpose; commits from here on are part of that rebuild.

## Build

Requires CMake 3.20+ and a C++20 toolchain (Apple Clang / LLVM is fine).

```bash
cmake -S . -B build
cmake --build build
./build/spruce
```

Expect the program to print `Spruce` and exit successfully. Strict warnings (`-Wall` and related flags) are enabled in `CMakeLists.txt`.

## Current status

- **CMake + entry binary** — toolchain and warning flags wired; runnable `spruce` target from `src/main.cpp`.
- **Engine components** — not started yet (MemTable, WAL, SSTable, Bloom, compaction, engine glue).

## Roadmap (high level)

1. MemTable → 2. WAL → 3. SSTable → 4. Bloom filter → 5. Compaction → 6. Engine + README-level comparison / tradeoffs (no need to “beat” RocksDB)

## Layout (target)

```
include/lsm/   headers
src/           implementations
tests/         tests + harness
```

## Learning log

| Date       | Notes |
| ---------- | ----- |
| 2026-05-18 | e.g. CMake scaffold committed, pushes `spruce`, strict warnings enabled |
