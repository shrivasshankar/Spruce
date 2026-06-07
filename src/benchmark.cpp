#include "lsm/config.h"
#include "lsm/engine.h"
#include "lsm/manifest.h"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct BenchMetrics {
  double write_amp = 0.0;
  double read_amp = 0.0;
  std::size_t total_files = 0;
  std::size_t l2v_files = 0;
  std::string layout_summary;
};

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

void PrintLevelLayout(const lsm::Manifest& manifest, int ell_horizontal) {
  const std::size_t horizontal_count = static_cast<std::size_t>(ell_horizontal);
  std::vector<std::size_t> horizontal_counts(horizontal_count, 0);
  std::size_t l1v = 0;
  std::size_t l2v = 0;

  for (const auto& entry : manifest.sst_entries) {
    if (entry.level < horizontal_count) {
      ++horizontal_counts[entry.level];
    } else if (entry.level == horizontal_count) {
      ++l1v;
    } else if (entry.level == horizontal_count + 1) {
      ++l2v;
    }
  }

  std::cout << "  SST layout:";
  for (std::size_t level = 0; level < horizontal_counts.size(); ++level) {
    std::cout << " L" << level << '=' << horizontal_counts[level];
  }
  std::cout << " L1v=" << l1v << " L2v=" << l2v;
  std::cout << " (total " << manifest.sst_entries.size() << " files)\n";
}

const char* GrowthSchemeName(lsm::GrowthScheme scheme) {
  switch (scheme) {
    case lsm::GrowthScheme::Vertiorizon:
      return "Vertiorizon";
    case lsm::GrowthScheme::HorizontalTiering:
      return "Horizontal-tiering";
    case lsm::GrowthScheme::VerticalTiering:
      return "Vertical-tiering";
  }
  return "unknown";
}

std::string SummarizeLayout(const lsm::Manifest& manifest, int ell_horizontal) {
  const std::size_t horizontal_count = static_cast<std::size_t>(ell_horizontal);
  std::ostringstream out;
  for (std::size_t level = 0; level < horizontal_count; ++level) {
    std::size_t count = 0;
    for (const auto& entry : manifest.sst_entries) {
      if (entry.level == level) {
        ++count;
      }
    }
    out << "L" << level << '=' << count << ' ';
  }
  std::size_t l1v = 0;
  std::size_t l2v = 0;
  for (const auto& entry : manifest.sst_entries) {
    if (entry.level == static_cast<std::uint32_t>(ell_horizontal)) {
      ++l1v;
    } else if (entry.level == static_cast<std::uint32_t>(ell_horizontal + 1)) {
      ++l2v;
    }
  }
  if (l1v > 0 || l2v > 0) {
    out << "L1v=" << l1v << " L2v=" << l2v;
  }
  return out.str();
}

bool RunWorkload(lsm::Config cfg, int num_keys, int value_size, BenchMetrics* metrics) {
  fs::remove_all(cfg.data_dir);

  const std::string value(static_cast<std::size_t>(value_size), 'v');
  std::uint64_t user_bytes = 0;

  {
    lsm::LSMEngine db(cfg);
    for (int i = 0; i < num_keys; ++i) {
      const std::string key = FormatKey(i);
      user_bytes += static_cast<std::uint64_t>(key.size() + value.size());
      db.Put(key, value);
    }
  }

  const std::uint64_t sst_bytes = DirBytes(fs::path(cfg.data_dir) / "sst");
  const auto manifest = lsm::LoadManifest(cfg.data_dir);

  lsm::LSMEngine db(cfg);
  std::uint64_t probe_total = 0;
  for (int i = 0; i < num_keys; ++i) {
    const std::string key = FormatKey(i);
    const auto value_opt = db.Get(key);
    if (!value_opt.has_value() || !value_opt->has_value() ||
        value_opt->value() != value) {
      std::cerr << "fail: get " << key << " scheme "
                << GrowthSchemeName(cfg.growth_scheme) << '\n';
      fs::remove_all(cfg.data_dir);
      return false;
    }
    probe_total += db.LastGetProbeStats().sst_files_probed;
  }

  const int layout_ell = cfg.growth_scheme == lsm::GrowthScheme::VerticalTiering
                             ? cfg.vertical_scheme_levels
                             : cfg.ell_horizontal;

  metrics->write_amp = Ratio(sst_bytes, user_bytes);
  metrics->read_amp =
      static_cast<double>(probe_total) / static_cast<double>(num_keys);
  metrics->total_files = manifest.sst_entries.size();
  metrics->l2v_files = 0;
  for (const auto& entry : manifest.sst_entries) {
    if (entry.level == static_cast<std::uint32_t>(cfg.ell_horizontal + 1)) {
      ++metrics->l2v_files;
    }
  }
  metrics->layout_summary = SummarizeLayout(manifest, layout_ell);

  fs::remove_all(cfg.data_dir);
  return true;
}

bool RunSchemeComparison(int num_keys, int value_size) {
  struct Row {
    lsm::GrowthScheme scheme;
    std::string data_dir;
  };

  const std::vector<Row> rows = {
      {lsm::GrowthScheme::Vertiorizon, "/tmp/spruce_bench_cmp_vrn"},
      {lsm::GrowthScheme::HorizontalTiering, "/tmp/spruce_bench_cmp_hr"},
      {lsm::GrowthScheme::VerticalTiering, "/tmp/spruce_bench_cmp_vt"},
  };

  std::cout << "Growth scheme comparison (same workload on each scheme)\n";
  std::cout << "  shared: B=1024 T=2 250 puts value_size=" << value_size << '\n';
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  scheme               write_amp  read_amp  sst_files  layout\n";

  for (const Row& row : rows) {
    lsm::Config cfg;
    cfg.data_dir = row.data_dir;
    cfg.buffer_size_bytes = 1024;
    cfg.block_size_bytes = 4096;
    cfg.estimated_data_buffers = 6;
    cfg.ell_horizontal = 2;
    cfg.n_initial = 1;
    cfg.T = 2;
    cfg.vertical_scheme_levels = 4;
    cfg.growth_scheme = row.scheme;

    BenchMetrics metrics;
    if (!RunWorkload(cfg, num_keys, value_size, &metrics)) {
      return false;
    }

    std::cout << "  " << std::setw(20) << std::left << GrowthSchemeName(row.scheme)
              << std::right << std::setw(9) << metrics.write_amp << 'x' << std::setw(10)
              << metrics.read_amp << 'x' << std::setw(10) << metrics.total_files << "  "
              << metrics.layout_summary << '\n';
  }

  std::cout << "  (lower read_amp = fewer SST probes per get; tradeoffs vary by workload)\n";
  return true;
}

bool RunScenario(const char* title, lsm::Config cfg, int num_keys, int value_size) {
  fs::remove_all(cfg.data_dir);

  const std::string value(static_cast<std::size_t>(value_size), 'v');
  std::uint64_t user_bytes = 0;

  {
    lsm::LSMEngine db(cfg);
    for (int i = 0; i < num_keys; ++i) {
      const std::string key = FormatKey(i);
      user_bytes += static_cast<std::uint64_t>(key.size() + value.size());
      db.Put(key, value);
    }
  }

  const std::uint64_t sst_bytes = DirBytes(fs::path(cfg.data_dir) / "sst");
  const std::uint64_t total_bytes = DirBytes(cfg.data_dir);
  const auto manifest = lsm::LoadManifest(cfg.data_dir);

  const std::uint64_t l1v_cap =
      static_cast<std::uint64_t>(cfg.n_initial) * static_cast<std::uint64_t>(cfg.T) *
      cfg.buffer_size_bytes;
  const std::uint64_t l2v_cap =
      static_cast<std::uint64_t>(cfg.n_initial) * static_cast<std::uint64_t>(cfg.T) *
      static_cast<std::uint64_t>(cfg.T) * cfg.buffer_size_bytes;

  std::cout << title << '\n';
  std::cout << "  config: B=" << cfg.buffer_size_bytes << " ell=" << cfg.ell_horizontal
            << " N/B=" << cfg.estimated_data_buffers << " n=" << cfg.n_initial
            << " T=" << cfg.T
            << " k=" << lsm::ComputeHorizontalTieringK(cfg.ell_horizontal,
                                                      cfg.estimated_data_buffers)
            << " L1v_cap=" << l1v_cap << " L2v_cap=" << l2v_cap << '\n';
  std::cout << "  workload: " << num_keys << " puts, value_size=" << value_size << '\n';
  std::cout << "  user bytes: " << user_bytes << '\n';
  std::cout << "  SST bytes on disk: " << sst_bytes << '\n';
  std::cout << "  total data_dir bytes: " << total_bytes << '\n';
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  write amp (SST / user): " << Ratio(sst_bytes, user_bytes) << "x\n";
  std::cout << "  write amp (total / user): " << Ratio(total_bytes, user_bytes) << "x\n";
  PrintLevelLayout(manifest, cfg.ell_horizontal);
  std::cout << "  compaction counters:";
  for (int counter : manifest.compaction_counters) {
    std::cout << ' ' << counter;
  }
  std::cout << '\n';

  lsm::LSMEngine db(cfg);
  std::uint64_t probe_total = 0;
  for (int i = 0; i < num_keys; ++i) {
    const std::string key = FormatKey(i);
    const auto value_opt = db.Get(key);
    if (!value_opt.has_value() || !value_opt->has_value() ||
        value_opt->value() != value) {
      std::cerr << "fail: " << title << " get " << key << '\n';
      fs::remove_all(cfg.data_dir);
      return false;
    }
    probe_total += db.LastGetProbeStats().sst_files_probed;
  }

  const double read_amp =
      static_cast<double>(probe_total) / static_cast<double>(num_keys);
  std::cout << "  read amp (avg SST files probed / get): " << read_amp << "x\n";

  fs::remove_all(cfg.data_dir);
  return true;
}

}  // namespace

int main() {
  std::cout << std::fixed << std::setprecision(2);

  {
    lsm::Config cfg;
    cfg.data_dir = "/tmp/spruce_bench";
    cfg.buffer_size_bytes = 4096;
    cfg.block_size_bytes = 4096;
    cfg.estimated_data_buffers = 6;
    cfg.ell_horizontal = 2;

    if (!RunScenario("Spruce amplification benchmark (Figure 5 config)", cfg, 300,
                     100)) {
      return 1;
    }
    std::cout << '\n';
  }

  {
    // Small B + T=2 forces L1v overflow into L2v after each horizontal handoff.
    // ~9 puts/flush, 6 flushes/handoff -> ~54 puts per vertical cycle.
    lsm::Config cfg;
    cfg.data_dir = "/tmp/spruce_bench_stress";
    cfg.buffer_size_bytes = 1024;
    cfg.block_size_bytes = 4096;
    cfg.estimated_data_buffers = 6;
    cfg.ell_horizontal = 2;
    cfg.n_initial = 1;
    cfg.T = 2;

    if (!RunScenario(
            "Spruce vertical stress benchmark (multiple handoffs + L1v->L2v)", cfg,
            250, 100)) {
      return 1;
    }
    std::cout << '\n';
  }

  if (!RunSchemeComparison(250, 100)) {
    return 1;
  }

  return 0;
}
