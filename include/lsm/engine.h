#pragma once

#include "lsm/config.h"
#include "lsm/memtable.h"
#include "lsm/sstable.h"
#include "lsm/wal.h"
#include "lsm/manifest.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lsm {

// Smallest k with binom(k + ell - 1, ell) >= estimated_data_buffers (Lemma 4.1).
int ComputeHorizontalTieringK(int ell_horizontal, int estimated_data_buffers);

struct Run {
  std::vector<std::unique_ptr<SSTable>> files;
};

struct Level {
  std::vector<Run> runs;
};

struct GetProbeStats {
  std::size_t sst_files_probed = 0;
  std::size_t sst_files_skipped_by_bloom = 0;
};

inline constexpr std::size_t kVerticalLevelCount = 2;

class LSMEngine {
 public:
  explicit LSMEngine(Config cfg);

  void Put(std::string key, std::string value);
  void Delete(std::string key);
  std::optional<std::optional<std::string>> Get(std::string_view key) const;
  GetProbeStats LastGetProbeStats() const { return last_get_probe_stats_; }

 private:
  void MaybeFlush();
  void Flush();
  void MaybeCompact();
  void CompactHorizontalLevel(std::size_t level_idx);
  void CompactHorizontalToVertical();
  void CompactVerticalPartial();
  void MaybeCompactVertical();
  void MaybeCompactVerticalScheme();
  void InitForGrowthScheme();
  void InitHorizontalLevels();
  void InitVerticalLevels();
  std::uint64_t HorizontalLevelBytes(std::size_t level_idx) const;
  std::uint64_t VerticalSchemeLevelCapacity(std::size_t level_idx) const;
  void SyncManifestFromLevels();
  void LoadFromManifest();
  std::uint32_t VerticalManifestLevelId(std::size_t vertical_idx) const;
  bool IsVerticalManifestLevel(std::uint32_t level) const;
  std::size_t VerticalIndexFromManifestLevel(std::uint32_t level) const;
  std::uint64_t VerticalLevelBytes(std::size_t vertical_idx) const;
  std::uint64_t VerticalLevelCapacity(std::size_t vertical_idx) const;
  std::string SstRelPath(std::uint64_t id) const;
  std::string WalPath() const;
  std::string SstPath(std::uint64_t id) const;

  Config cfg_;
  MemTable memtable_;
  WalWriter wal_;
  Manifest manifest_;
  std::vector<Level> levels_;
  std::vector<Level> vertical_levels_;    // L1v, L2v (Vertiorizon vertical part)
  std::vector<int> compaction_counters_;  // current C_i; initialized to k_
  int k_ = 1;                             // reset target from Lemma 4.1
  int n_ = 1;                             // horizontal capacity scale; persisted in MANIFEST
  std::uint64_t next_sst_id_ = 1;
  mutable GetProbeStats last_get_probe_stats_;
};

}  // namespace lsm