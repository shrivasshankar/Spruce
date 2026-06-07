#pragma once
#include <cstddef>
#include <string>
namespace lsm {
enum class MergePolicy { Tiering, Leveling };
enum class GrowthScheme {
  Vertiorizon,       // horizontal-tiering + vertical tail (paper Vertiorizon)
  HorizontalTiering, // Algorithm 2 only, no vertical handoff (paper HR-Tier)
  VerticalTiering,   // fixed capacities B*T^i, tiering merge (paper VT-Tier)
};
struct Config {
  std::string data_dir = "db";
  size_t buffer_size_bytes = 4 * 1024 * 1024;  // B — MemTable cap
  size_t block_size_bytes = 4096;              // Phase 1 SSTable
  int T = 10;                                  // size ratio
  int n_initial = 1;                           // Vertiorizon n (vertical part)
  int estimated_data_buffers = 6;                // N/B estimate for Lemma 4.1 k init
  int ell_horizontal = 2;                      // horizontal levels
  int vertical_scheme_levels = 4;                // levels for VerticalTiering mode
  GrowthScheme growth_scheme = GrowthScheme::Vertiorizon;
  MergePolicy merge_policy = MergePolicy::Tiering;
};
} 