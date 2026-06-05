#include "lsm/config.h"
#include "lsm/engine.h"
#include "lsm/manifest.h"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::uint64_t DirBytes(const fs::path& dir) {
  std::uint64_t total = 0;
  if (!fs::exists(dir)) {
    return 0;
  }

  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      total += static_cast<std::uint64_t>(entry.file_size());
    }
  }
  return total;
}

std::string FormatKey(int id) {
  std::ostringstream key;
  key << "key" << std::setw(6) << std::setfill('0') << id;
  return key.str();
}

double Ratio(std::uint64_t numerator, std::uint64_t denominator) {
  if (denominator == 0) {
    return 0.0;
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

void PrintLevelLayout(const lsm::Manifest& manifest) {
  std::vector<std::size_t> counts(manifest.compaction_counters.size(), 0);
  for (const auto& entry : manifest.sst_entries) {
    if (entry.level < counts.size()) {
      ++counts[entry.level];
    }
  }

  std::cout << "  SST layout:";
  for (std::size_t level = 0; level < counts.size(); ++level) {
    std::cout << " L" << level << '=' << counts[level];
  }
  std::cout << " (total " << manifest.sst_entries.size() << " files)\n";
}

}  // namespace

int main() {
  const std::string data_dir = "/tmp/spruce_bench";
  fs::remove_all(data_dir);

  lsm::Config cfg;
  cfg.data_dir = data_dir;
  cfg.buffer_size_bytes = 4096;
  cfg.block_size_bytes = 4096;
  cfg.estimated_data_buffers = 6;  // Figure 5: N/B with ell=2 -> k=3
  cfg.ell_horizontal = 2;

  constexpr int kNumKeys = 300;
  constexpr int kValueSize = 100;
  const std::string value(kValueSize, 'v');

  std::uint64_t user_bytes = 0;
  {
    lsm::LSMEngine db(cfg);
    for (int i = 0; i < kNumKeys; ++i) {
      const std::string key = FormatKey(i);
      user_bytes += static_cast<std::uint64_t>(key.size() + value.size());
      db.Put(key, value);
    }
  }

  const std::uint64_t sst_bytes = DirBytes(fs::path(data_dir) / "sst");
  const std::uint64_t total_bytes = DirBytes(data_dir);
  const auto manifest = lsm::LoadManifest(data_dir);

  std::cout << "Spruce amplification benchmark\n";
  std::cout << "  config: B=" << cfg.buffer_size_bytes
            << " ell=" << cfg.ell_horizontal
            << " N/B=" << cfg.estimated_data_buffers
            << " k=" << lsm::ComputeHorizontalTieringK(cfg.ell_horizontal,
                                                      cfg.estimated_data_buffers)
            << '\n';
  std::cout << "  workload: " << kNumKeys << " puts, value_size=" << kValueSize << '\n';
  std::cout << "  user bytes: " << user_bytes << '\n';
  std::cout << "  SST bytes on disk: " << sst_bytes << '\n';
  std::cout << "  total data_dir bytes: " << total_bytes << '\n';
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  write amp (SST / user): " << Ratio(sst_bytes, user_bytes) << "x\n";
  std::cout << "  write amp (total / user): " << Ratio(total_bytes, user_bytes) << "x\n";
  PrintLevelLayout(manifest);
  std::cout << "  compaction counters:";
  for (int counter : manifest.compaction_counters) {
    std::cout << ' ' << counter;
  }
  std::cout << '\n';

  lsm::LSMEngine db(cfg);
  std::uint64_t probe_total = 0;
  for (int i = 0; i < kNumKeys; ++i) {
    const std::string key = FormatKey(i);
    const auto value_opt = db.Get(key);
    if (!value_opt.has_value() || !value_opt->has_value() ||
        value_opt->value() != value) {
      std::cerr << "fail: benchmark get " << key << '\n';
      return 1;
    }
    probe_total += db.LastGetProbeStats().sst_files_probed;
  }

  const double read_amp =
      static_cast<double>(probe_total) / static_cast<double>(kNumKeys);
  std::cout << "  read amp (avg SST files probed / get): " << read_amp << "x\n";

  fs::remove_all(data_dir);
  return 0;
}
