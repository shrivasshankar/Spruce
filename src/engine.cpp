#include "lsm/engine.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

int Binomial(int n, int k) {
  if (k < 0 || k > n) {
    return 0;
  }
  if (k == 0 || k == n) {
    return 1;
  }
  k = std::min(k, n - k);
  int result = 1;
  for (int i = 0; i < k; ++i) {
    result = result * (n - i) / (i + 1);
  }
  return result;
}
std::string RelPathFromFull(const std::string& data_dir, const std::string& full_path) {
  return fs::relative(full_path, data_dir).string();
}

}  // namespace

namespace lsm {

int ComputeHorizontalTieringK(int ell_horizontal, int estimated_data_buffers) {
  if (ell_horizontal < 1) {
    throw std::runtime_error("ell_horizontal must be >= 1");
  }
  if (estimated_data_buffers < 1) {
    throw std::runtime_error("estimated_data_buffers must be >= 1");
  }

  int k = 1;
  while (Binomial(k + ell_horizontal - 1, ell_horizontal) < estimated_data_buffers) {
    ++k;
  }
  return k;
}

LSMEngine::LSMEngine(Config cfg)
    : cfg_(std::move(cfg)),
      memtable_(cfg_.buffer_size_bytes),
      wal_(WalPath()) {
  fs::create_directories(fs::path(cfg_.data_dir) / "sst");
  fs::create_directories(fs::path(cfg_.data_dir) / "wal");

  InitHorizontalLevels();
  InitVerticalLevels();
  LoadFromManifest();
  ReplayWal(WalPath(), memtable_);
}

void LSMEngine::InitHorizontalLevels() {
  if (cfg_.ell_horizontal < 1) {
    throw std::runtime_error("ell_horizontal must be >= 1");
  }

  const auto level_count = static_cast<std::size_t>(cfg_.ell_horizontal);
  levels_.resize(level_count);

  k_ = ComputeHorizontalTieringK(cfg_.ell_horizontal, cfg_.estimated_data_buffers);
  compaction_counters_.assign(level_count, k_);  // Algorithm 2: C_i <- k
}

void LSMEngine::InitVerticalLevels() {
  vertical_levels_.clear();
  vertical_levels_.resize(kVerticalLevelCount);
  n_ = cfg_.n_initial;
  if (n_ < 1) {
    throw std::runtime_error("n_initial must be >= 1");
  }
}

std::uint32_t LSMEngine::VerticalManifestLevelId(std::size_t vertical_idx) const {
  return static_cast<std::uint32_t>(levels_.size() + vertical_idx);
}

bool LSMEngine::IsVerticalManifestLevel(std::uint32_t level) const {
  const auto base = static_cast<std::uint32_t>(levels_.size());
  return level >= base && level < base + static_cast<std::uint32_t>(kVerticalLevelCount);
}

std::size_t LSMEngine::VerticalIndexFromManifestLevel(std::uint32_t level) const {
  return static_cast<std::size_t>(level - levels_.size());
}

void LSMEngine::Put(std::string key, std::string value) {
  wal_.AppendPut(key, value);
  wal_.Sync();
  memtable_.Put(std::move(key), std::move(value));
  MaybeFlush();
}

std::optional<std::optional<std::string>> LSMEngine::Get(std::string_view key) const {
  last_get_probe_stats_ = {};

  const auto mem = memtable_.Lookup(key);
  if (mem.has_value()) {
    return mem;
  }

  auto probe_levels = [this, key](const std::vector<Level>& levels)
      -> std::optional<std::optional<std::string>> {
    for (const Level& level : levels) {
      for (auto run_it = level.runs.rbegin(); run_it != level.runs.rend(); ++run_it) {
        for (auto file_it = run_it->files.rbegin(); file_it != run_it->files.rend();
             ++file_it) {
          ++last_get_probe_stats_.sst_files_probed;
          const auto result = (*file_it)->Get(key);
          if (result.has_value()) {
            return result;
          }
        }
      }
    }
    return std::nullopt;
  };

  if (const auto hit = probe_levels(levels_)) {
    return hit;
  }
  if (const auto hit = probe_levels(vertical_levels_)) {
    return hit;
  }

  return std::nullopt;
}

void LSMEngine::Delete(std::string key) {
  wal_.AppendDelete(key);
  wal_.Sync();
  memtable_.Delete(std::move(key));
  MaybeFlush();
}

std::string LSMEngine::WalPath() const {
  return (fs::path(cfg_.data_dir) / "wal" / "log.wal").string();
}

std::string LSMEngine::SstRelPath(std::uint64_t id) const {
  std::ostringstream name;
  name << std::setw(6) << std::setfill('0') << id << ".sst";
  return (fs::path("sst") / name.str()).string();
}

std::string LSMEngine::SstPath(std::uint64_t id) const {
  return (fs::path(cfg_.data_dir) / SstRelPath(id)).string();
}

void LSMEngine::MaybeFlush() {
  if (!memtable_.IsFull()) {
    return;
  }
  Flush();
}

void LSMEngine::Flush() {
  const auto rows = memtable_.GetSorted();
  if (rows.empty()) {
    return;
  }

  const std::string path = SstPath(next_sst_id_);
  {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("failed to open SST for flush: " + path);
    }

    SSTableWriter writer(cfg_.block_size_bytes);
    for (const auto& [key, value] : rows) {
      writer.Add(key, value);
    }
    writer.Finish(out);
    if (!out.good()) {
      throw std::runtime_error("SST flush failed: " + path);
    }
    out.flush();
    if (out.rdbuf()->pubsync() != 0) {
      throw std::runtime_error("SST sync failed: " + path);
    }
  }

  Run run;
  run.files.push_back(
      std::make_unique<SSTable>(SSTable::Open(path)));
  levels_.front().runs.push_back(std::move(run));

  ++next_sst_id_;

  memtable_ = MemTable(cfg_.buffer_size_bytes);
  wal_.Truncate();

  MaybeCompact();

  SyncManifestFromLevels();
  SaveManifest(cfg_.data_dir, manifest_);
}

void LSMEngine::CompactHorizontalLevel(std::size_t level_idx) {
  if (level_idx + 1 >= levels_.size()) {
    return;  // last horizontal level -> vertical (Phase 5)
  }

  Level& src = levels_[level_idx];
  if (src.runs.empty()) {
    return;
  }

  // Newest run first; first insert wins -> newest version kept.
  std::map<std::string, std::optional<std::string>> merged;
  std::vector<std::string> absorbed_rel_paths;

  for (auto run_it = src.runs.rbegin(); run_it != src.runs.rend(); ++run_it) {
    for (const auto& file : run_it->files) {
      absorbed_rel_paths.push_back(
          RelPathFromFull(cfg_.data_dir, file->Path()));

      for (const auto& [key, value] : file->Entries()) {
        if (merged.find(key) == merged.end()) {
          merged[key] = value;
        }
      }
    }
  }

  src.runs.clear();

  if (merged.empty()) {
    return;
  }

  const std::string path = SstPath(next_sst_id_);
  {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("failed to open SST for compaction: " + path);
    }

    SSTableWriter writer(cfg_.block_size_bytes);
    for (const auto& [key, value] : merged) {
      writer.Add(key, value);
    }
    writer.Finish(out);
    if (!out.good()) {
      throw std::runtime_error("SST compaction failed: " + path);
    }
    out.flush();
    if (out.rdbuf()->pubsync() != 0) {
      throw std::runtime_error("SST compaction sync failed: " + path);
    }
  }

  Run new_run;
  new_run.files.push_back(std::make_unique<SSTable>(SSTable::Open(path)));
  levels_[level_idx + 1].runs.push_back(std::move(new_run));

  ++next_sst_id_;

  for (const auto& absorbed : absorbed_rel_paths) {
    std::error_code ec;
    fs::remove(fs::path(cfg_.data_dir) / absorbed, ec);
  }
}

void LSMEngine::SyncManifestFromLevels() {
  manifest_.next_sst_id = next_sst_id_;
  manifest_.k = k_;
  manifest_.n = n_;
  manifest_.compaction_counters = compaction_counters_;
  manifest_.sst_entries.clear();

  for (std::size_t level_idx = 0; level_idx < levels_.size(); ++level_idx) {
    for (const Run& run : levels_[level_idx].runs) {
      for (const auto& file : run.files) {
        SstEntry entry;
        entry.rel_path = RelPathFromFull(cfg_.data_dir, file->Path());
        entry.level = static_cast<std::uint32_t>(level_idx);
        manifest_.sst_entries.push_back(std::move(entry));
      }
    }
  }

  for (std::size_t vertical_idx = 0; vertical_idx < vertical_levels_.size();
       ++vertical_idx) {
    for (const Run& run : vertical_levels_[vertical_idx].runs) {
      for (const auto& file : run.files) {
        SstEntry entry;
        entry.rel_path = RelPathFromFull(cfg_.data_dir, file->Path());
        entry.level = VerticalManifestLevelId(vertical_idx);
        manifest_.sst_entries.push_back(std::move(entry));
      }
    }
  }
}

void LSMEngine::MaybeCompact() {
  if (compaction_counters_.empty()) {
    return;
  }

  // Each buffer flush decrements C[0] (paper's C_1).
  compaction_counters_[0] -= 1;

  for (std::size_t i = 0; i + 1 < compaction_counters_.size(); ++i) {
    if (compaction_counters_[i] != 0) {
      continue;
    }

    CompactHorizontalLevel(i);

    compaction_counters_[i + 1] -= 1;

    const int reset_value = compaction_counters_[i + 1];
    for (std::size_t j = 0; j <= i; ++j) {
      compaction_counters_[j] = reset_value;
    }
  }
}

void LSMEngine::LoadFromManifest() {
  manifest_ = LoadManifest(cfg_.data_dir);
  next_sst_id_ = manifest_.next_sst_id;

  if (!manifest_.compaction_counters.empty()) {
    k_ = manifest_.k;
    compaction_counters_ = manifest_.compaction_counters;
  }
  if (manifest_.n >= 1) {
    n_ = manifest_.n;
  }

  for (const auto& entry : manifest_.sst_entries) {
    const std::string path = (fs::path(cfg_.data_dir) / entry.rel_path).string();
    Run run;
    run.files.push_back(
        std::make_unique<SSTable>(SSTable::Open(path)));

    if (IsVerticalManifestLevel(entry.level)) {
      const std::size_t vertical_idx = VerticalIndexFromManifestLevel(entry.level);
      vertical_levels_[vertical_idx].runs.push_back(std::move(run));
      continue;
    }

    if (entry.level >= levels_.size()) {
      throw std::runtime_error("MANIFEST SST level out of range");
    }

    levels_[entry.level].runs.push_back(std::move(run));
  }
}

}  // namespace lsm