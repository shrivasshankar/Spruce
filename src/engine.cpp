#include "lsm/engine.h"

namespace lsm {

LSMEngine::LSMEngine(Config cfg)
    : cfg_(std::move(cfg)),
      memtable_(cfg_.buffer_size_bytes),
      levels_(1) {}

void LSMEngine::Put(std::string key, std::string value) {
  memtable_.Put(std::move(key), std::move(value));
  MaybeFlush();
}

void LSMEngine::Delete(std::string key) {
  memtable_.Delete(std::move(key));
  MaybeFlush();
}

std::optional<std::optional<std::string>> LSMEngine::Get(std::string_view key) const {
  const auto value = memtable_.Get(key);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return *value;
}

void LSMEngine::MaybeFlush() {
  // Phase 2: flush memtable to SST and append to levels_[0].
}

}  // namespace lsm
