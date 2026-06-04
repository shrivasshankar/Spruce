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

struct Run {
  std::vector<std::unique_ptr<SSTable>> files;
};

struct Level {
  std::vector<Run> runs;
};

class LSMEngine {
 public:
  explicit LSMEngine(Config cfg);

  void Put(std::string key, std::string value);
  void Delete(std::string key);
  std::optional<std::optional<std::string>> Get(std::string_view key) const;

 private:
  void MaybeFlush();
  void Flush();
  void MaybeCompact();
  void InitHorizontalLevels();
  void LoadFromManifest();
  std::string SstRelPath(std::uint64_t id) const;
  std::string WalPath() const;
  std::string SstPath(std::uint64_t id) const;

  Config cfg_;
  MemTable memtable_;
  WalWriter wal_;
  Manifest manifest_;
  std::vector<Level> levels_;
  std::vector<int> compaction_counters_;
  std::uint64_t next_sst_id_ = 1;
};

}  // namespace lsm