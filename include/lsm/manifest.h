#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsm {

struct Manifest {
  std::uint64_t next_sst_id = 1;
  std::vector<std::string> sst_paths;  // oldest → newest
};

std::string ManifestPath(const std::string& data_dir);

// Returns default Manifest if file missing.
Manifest LoadManifest(const std::string& data_dir);

void SaveManifest(const std::string& data_dir, const Manifest& manifest);

}  // namespace lsm