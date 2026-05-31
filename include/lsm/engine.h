#pragma once

#include "lsm/config.h"
#include "lsm/memtable.h"
#include "lsm/sstable.h"

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

  Config cfg_;
  MemTable memtable_;
  std::vector<Level> levels_;
};

}  // namespace lsm
