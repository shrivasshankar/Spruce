#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsm {

struct SstEntry {
  std::string rel_path;
  std::uint32_t level = 0;
};

struct Manifest {
  std::uint64_t next_sst_id = 1;
  int k = 1;
  int n = 1;  // horizontal part capacity scale (n * B); Vertiorizon §5.1
  std::vector<int> compaction_counters;
  // level 0..ell-1 horizontal; ell..ell+1 vertical (L1v, L2v)
  std::vector<SstEntry> sst_entries;
};

std::string ManifestPath(const std::string& data_dir);

// Returns default Manifest if file missing.
Manifest LoadManifest(const std::string& data_dir);

void SaveManifest(const std::string& data_dir, const Manifest& manifest);

}  // namespace lsm