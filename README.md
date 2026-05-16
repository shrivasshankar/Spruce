# Spruce

LSM-tree storage engine in C++, STL-only — summer learning project.

This repository was intentionally reset to an empty slate so implementation can be written step-by-step by hand after strengthening C++ fundamentals. Previous scaffolding did not survive here by design.

## Roadmap (high level)

1. MemTable → 2. WAL → 3. SSTable → 4. Bloom filter → 5. Compaction → 6. Engine + benchmarks vs LevelDB/RocksDB

## Layout (when you add it)

```
include/lsm/   headers
src/           implementations
tests/         tests + harness
```

Nothing is here yet — add CMake and the first component when ready.
