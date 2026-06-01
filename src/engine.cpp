#include "lsm/engine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace lsm {

namespace {

std::uint64_t ParseSstId(const fs::path& path) {
  const std::string stem = path.stem().string();
  return static_cast<std::uint64_t>(std::stoull(stem));
}

}  // namespace

LSMEngine::LSMEngine(Config cfg)
    : cfg_(std::move(cfg)),
      memtable_(cfg_.buffer_size_bytes),
      wal_(WalPath()),
      levels_(1) {
  fs::create_directories(fs::path(cfg_.data_dir) / "sst");
  fs::create_directories(fs::path(cfg_.data_dir) / "wal");

  LoadExistingSsts();
  ReplayWal(WalPath(), memtable_);
}

void LSMEngine::Put(std::string key, std::string value) {
  wal_.AppendPut(key, value);
  wal_.Sync();
  memtable_.Put(std::move(key), std::move(value));
  MaybeFlush();
}

std::optional<std::optional<std::string>> LSMEngine::Get(std::string_view key) const {
  const auto mem = memtable_.Lookup(key);
  if (mem.has_value()) {
    return mem;
  }

  if (levels_.empty()) {
    return std::nullopt;
  }

  const Level& level = levels_.front();
  for (auto run_it = level.runs.rbegin(); run_it != level.runs.rend(); ++run_it) {
    for (auto file_it = run_it->files.rbegin(); file_it != run_it->files.rend();
         ++file_it) {
      const auto result = (*file_it)->Get(key);
      if (result.has_value()) {
        return result;
      }
    }
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

std::string LSMEngine::SstPath(std::uint64_t id) const {
  std::ostringstream name;
  name << std::setw(6) << std::setfill('0') << id << ".sst";
  return (fs::path(cfg_.data_dir) / "sst" / name.str()).string();
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
  }

  Run run;
  run.files.push_back(
      std::make_unique<SSTable>(SSTable::Open(path)));
  levels_.front().runs.push_back(std::move(run));

  ++next_sst_id_;
  memtable_ = MemTable(cfg_.buffer_size_bytes);
  wal_.Truncate();
}

void LSMEngine::LoadExistingSsts() {
  const fs::path sst_dir = fs::path(cfg_.data_dir) / "sst";
  if (!fs::exists(sst_dir)) {
    return;
  }

  std::vector<fs::path> paths;
  for (const auto& entry : fs::directory_iterator(sst_dir)) {
    if (entry.path().extension() == ".sst") {
      paths.push_back(entry.path());
    }
  }

  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    Run run;
    run.files.push_back(
        std::make_unique<SSTable>(SSTable::Open(path.string())));
    levels_.front().runs.push_back(std::move(run));
    next_sst_id_ = std::max(next_sst_id_, ParseSstId(path) + 1);
  }
}

}  // namespace lsm