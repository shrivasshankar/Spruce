#pragma once
#include <cstddef>
#include <string>
namespace lsm {
enum class MergePolicy { Tiering, Leveling };
struct Config {
  std::string data_dir = "db";
  size_t buffer_size_bytes = 4 * 1024 * 1024;  // B — MemTable cap
  size_t block_size_bytes = 4096;              // Phase 1 SSTable
  int T = 10;                                  // size ratio
  int n_initial = 1;                           // Vertiorizon n
  int ell_horizontal = 2;                      // horizontal levels
  MergePolicy merge_policy = MergePolicy::Tiering;
};
} 