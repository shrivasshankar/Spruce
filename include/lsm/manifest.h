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
  std::vector<int> compaction_counters;
  std::vector<SstEntry> sst_entries;  // oldest → newest
};

std::string ManifestPath(const std::string& data_dir);

// Returns default Manifest if file missing.
Manifest LoadManifest(const std::string& data_dir);

void SaveManifest(const std::string& data_dir, const Manifest& manifest);

}  // namespace lsm