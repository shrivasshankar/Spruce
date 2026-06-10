#include <iostream>
#include "lsm/memtable.h"
#include "lsm/wal.h"
#include <cstdio> 
#include "lsm/config.h"
#include "lsm/engine.h"
#include "lsm/sstable.h"
#include <fstream>
#include <filesystem>
#include <string>
#include "lsm/manifest.h"


namespace fs = std::filesystem;

namespace {

lsm::Config Figure5Config(const std::string& data_dir) {
  lsm::Config cfg;
  cfg.data_dir = data_dir;
  // Each Put is 29 bytes (4-byte key + 25-byte value). Need 2*29 < B <= 3*29
  // so exactly three Puts trigger one flush (Figure 5 counts flushes, not Puts).
  cfg.buffer_size_bytes = 64;
  cfg.estimated_data_buffers = 6;  // N/B -> k=3 with ell=2
  cfg.ell_horizontal = 2;
  return cfg;
}

void PutFlushBatch(lsm::LSMEngine& db, int start_key, int count) {
  for (int i = start_key; i < start_key + count; ++i) {
    db.Put("key" + std::to_string(i), std::string(25, 'x'));
  }
}

std::size_t CountSstsAtLevel(const lsm::Manifest& manifest, std::uint32_t level) {
  std::size_t count = 0;
  for (const auto& entry : manifest.sst_entries) {
    if (entry.level == level) {
      ++count;
    }
  }
  return count;
}

bool CountersEqual(const lsm::Manifest& manifest,
                   const std::vector<int>& expected) {
  return manifest.compaction_counters == expected;
}

}  // namespace

int main() {
    const lsm::Config cfg;
    lsm::MemTable mt(cfg.buffer_size_bytes);

    mt.Delete("a");
    if (mt.Get("a").has_value()) {
    std::cerr << "fail: delete\n";
    return 1;
    }
    mt.Put("a", "2");
    if (mt.Get("a") != std::optional<std::string>("2")) {
    std::cerr << "fail: revive\n";
    return 1;
    }

    mt.Put("a", "1");
    auto v = mt.Get("a");
    if (!v || *v != "1") {
        std::cerr << "fail\n";
        return 1;
    }

    mt.Put("b", "2");
    mt.Delete("c");   // tombstone for key never Put — still ok

    auto rows = mt.GetSorted();
    if (rows.size() < 2) {
    std::cerr << "fail: getsorted size\n";
    return 1;
    }
    // map order: "a" before "b" before "c"
    for (size_t i = 1; i < rows.size(); ++i) {
    if (rows[i].first < rows[i - 1].first) {
        std::cerr << "fail: not sorted\n";
        return 1;
    }
    }

  
    if (mt.IsFull()) {  // should not be full
        std::cerr << "fail: should not be full\n";
        return 1;
    }

    lsm::MemTable empty(cfg.buffer_size_bytes);
    if (empty.SizeBytes() != 0 || empty.IsFull()) {
    std::cerr << "fail: empty memtable\n";
    return 1;
    }

    

    const std::string wal_path = "/tmp/spruce_test.wal";
    std::remove(wal_path.c_str());

    {
    lsm::WalWriter w(wal_path);
    w.AppendPut("a", "1");
    w.AppendDelete("a");
    }
    
    auto recs = lsm::ReadWal(wal_path);
    if (recs.size() != 2) { 
        std::cerr << "fail: wal size\n"; return 1; 
    }

    std::remove(wal_path.c_str());
    {
    lsm::WalWriter w(wal_path);
    w.AppendPut("x", "hello");
    w.AppendDelete("y");
    w.AppendPut("z", "3");
    w.AppendPut("empty", "");  // empty value must replay as "", not crash
    }
    lsm::MemTable recovered(cfg.buffer_size_bytes);
    lsm::ReplayWal(wal_path, recovered);

    if (recovered.Get("x") != std::optional<std::string>("hello")) {
        std::cerr << "fail: replay x\n";
        return 1;
      }
      if (recovered.Get("y").has_value()) {
        std::cerr << "fail: replay y deleted\n";
        return 1;
      }
      if (recovered.Get("z") != std::optional<std::string>("3")) {
        std::cerr << "fail: replay z\n";
        return 1;
      }
      if (recovered.Get("empty") != std::optional<std::string>("")) {
        std::cerr << "fail: replay empty-value put\n";
        return 1;
      }

      

// ...

    const std::string sst_path = "/tmp/spruce_test.sst";
    std::remove(sst_path.c_str());

    {
      std::ofstream out(sst_path, std::ios::binary);
      if (!out) {
        std::cerr << "fail: open sst\n";
        return 1;
      }

      lsm::SSTableWriter writer(cfg.block_size_bytes);
      writer.Add("a", "1");
      writer.Add("b", std::nullopt);  // tombstone
      writer.Add("c", "3");
      writer.Finish(out);
    }

    const auto table = lsm::SSTable::Open(sst_path);

    if (table.Get("a") != std::optional<std::string>("1")) {
      std::cerr << "fail: sst get a\n";
      return 1;
    }

    const auto b = table.Get("b");
    if (!b.has_value() || b->has_value()) {
      std::cerr << "fail: sst tombstone b\n";
      return 1;
    }

    if (table.Get("c") != std::optional<std::string>("3")) {
      std::cerr << "fail: sst get c\n";
      return 1;
    }

    if (table.Get("zzz").has_value()) {
      std::cerr << "fail: sst miss zzz\n";
      return 1;
    }

    {
      const std::string engine_basic_dir = "/tmp/spruce_engine_basic_test";
      fs::remove_all(engine_basic_dir);

      lsm::Config engine_basic_cfg;
      engine_basic_cfg.data_dir = engine_basic_dir;

      lsm::LSMEngine db(engine_basic_cfg);
      db.Put("engine_key", "engine_val");
      if (db.Get("engine_key") != std::optional<std::string>("engine_val")) {
        std::cerr << "fail: engine get\n";
        return 1;
      }
      db.Delete("engine_key");
      const auto deleted = db.Get("engine_key");
      if (!deleted.has_value() || deleted->has_value()) {
        std::cerr << "fail: engine delete\n";
        return 1;
      }

      fs::remove_all(engine_basic_dir);
    }

    const std::string engine_dir = "/tmp/spruce_engine_test";
        fs::remove_all(engine_dir);
    
        lsm::Config engine_cfg;
        engine_cfg.data_dir = engine_dir;
        engine_cfg.buffer_size_bytes = 32;  // 7-byte key + 25-byte value triggers flush
    
        {
          lsm::LSMEngine flush_db(engine_cfg);
          flush_db.Put("on_disk", std::string(25, 'x'));
          if (flush_db.Get("on_disk") != std::optional<std::string>(std::string(25, 'x'))) {
            std::cerr << "fail: engine flush get same session\n";
            return 1;
          }
        }
        {
          lsm::LSMEngine reopened(engine_cfg);
          if (reopened.Get("on_disk") != std::optional<std::string>(std::string(25, 'x'))) {
            std::cerr << "fail: engine flush get reopened\n";
            return 1;
          }
        }
        fs::remove_all(engine_dir);
    
        lsm::Config wal_cfg;
        wal_cfg.data_dir = "/tmp/spruce_engine_wal_test";
        fs::remove_all(wal_cfg.data_dir);
    
        {
          lsm::LSMEngine wal_db(wal_cfg);
          wal_db.Put("persist", "wal");
        }
        {
          lsm::LSMEngine wal_reopened(wal_cfg);
          if (wal_reopened.Get("persist") != std::optional<std::string>("wal")) {
            std::cerr << "fail: engine wal replay\n";
            return 1;
          }
        }
        fs::remove_all(wal_cfg.data_dir);

        {
          const std::string manifest_dir = "/tmp/spruce_manifest_test";
          fs::remove_all(manifest_dir);
    
          lsm::Manifest m;
          m.next_sst_id = 3;
          m.k = 3;
          m.n = 2;
          m.compaction_counters = {3, 3};
          m.sst_entries = {
              {"sst/000001.sst", 0},
              {"sst/000002.sst", 1},
              {"sst/000003.sst", 2},  // vertical L1v when ell=2
          };
          lsm::SaveManifest(manifest_dir, m);

          const auto loaded = lsm::LoadManifest(manifest_dir);
          if (loaded.next_sst_id != 3 || loaded.sst_entries.size() != 3) {
            std::cerr << "fail: manifest load\n";
            return 1;
          }
          if (loaded.k != 3 || loaded.n != 2 ||
              loaded.compaction_counters != std::vector<int>({3, 3})) {
            std::cerr << "fail: manifest counters\n";
            return 1;
          }
          if (loaded.sst_entries[0].rel_path != "sst/000001.sst" ||
              loaded.sst_entries[0].level != 0 ||
              loaded.sst_entries[1].rel_path != "sst/000002.sst" ||
              loaded.sst_entries[1].level != 1 ||
              loaded.sst_entries[2].rel_path != "sst/000003.sst" ||
              loaded.sst_entries[2].level != 2) {
            std::cerr << "fail: manifest entries\n";
            return 1;
          }

          fs::remove_all(manifest_dir);
        }

        {
          const std::string dir = "/tmp/spruce_vertical_scaffold_test";
          fs::remove_all(dir);
          fs::create_directories(dir + "/sst");
          fs::create_directories(dir + "/wal");

          const std::string scaffold_sst_path = dir + "/sst/000001.sst";
          {
            std::ofstream out(scaffold_sst_path, std::ios::binary);
            lsm::SSTableWriter writer(cfg.block_size_bytes);
            writer.Add("vertical_key", "vertical_val");
            writer.Finish(out);
          }

          lsm::Manifest m;
          m.next_sst_id = 2;
          m.k = 3;
          m.n = 2;
          m.compaction_counters = {2, 2};
          m.sst_entries = {{"sst/000001.sst", 2}};  // L1v for ell=2
          lsm::SaveManifest(dir, m);

          lsm::Config vertical_cfg;
          vertical_cfg.data_dir = dir;
          vertical_cfg.ell_horizontal = 2;
          vertical_cfg.n_initial = 2;

          lsm::LSMEngine db(vertical_cfg);
          if (db.Get("vertical_key") != std::optional<std::string>("vertical_val")) {
            std::cerr << "fail: vertical scaffold get\n";
            return 1;
          }

          fs::remove_all(dir);
        }

        {
          const std::string dir = "/tmp/spruce_vertical_handoff_test";
          fs::remove_all(dir);
          lsm::Config handoff_cfg = Figure5Config(dir);
          handoff_cfg.n_initial = 1;
          handoff_cfg.T = 20;  // L1v cap = 20*64 = 1280 B, fits one handoff SST (~675 B)

          {
            lsm::LSMEngine db(handoff_cfg);
            PutFlushBatch(db, 0, 18);  // 6 flushes -> one horizontal->vertical handoff
          }

          const auto manifest = lsm::LoadManifest(dir);
          if (CountSstsAtLevel(manifest, 2) < 1) {
            std::cerr << "fail: vertical handoff missing L1v SST\n";
            return 1;
          }
          if (CountSstsAtLevel(manifest, 1) != 0) {
            std::cerr << "fail: vertical handoff horizontal L1 not empty\n";
            return 1;
          }

          lsm::LSMEngine db(handoff_cfg);
          for (int i = 0; i < 18; ++i) {
            if (db.Get("key" + std::to_string(i)) !=
                std::optional<std::string>(std::string(25, 'x'))) {
              std::cerr << "fail: vertical handoff get key" << i << '\n';
              return 1;
            }
          }

          fs::remove_all(dir);
        }

        {
          const std::string dir = "/tmp/spruce_vertical_overflow_test";
          fs::remove_all(dir);
          lsm::Config overflow_cfg = Figure5Config(dir);
          overflow_cfg.n_initial = 1;
          overflow_cfg.T = 2;  // L1v cap = 2*B = 128 bytes with B=64

          {
            lsm::LSMEngine db(overflow_cfg);
            PutFlushBatch(db, 0, 18);  // handoff + L1v overflow -> L2v
          }

          const auto manifest = lsm::LoadManifest(dir);
          if (CountSstsAtLevel(manifest, 3) < 1) {
            std::cerr << "fail: vertical overflow missing L2v SST\n";
            return 1;
          }

          lsm::LSMEngine db(overflow_cfg);
          for (int i = 0; i < 18; ++i) {
            if (db.Get("key" + std::to_string(i)) !=
                std::optional<std::string>(std::string(25, 'x'))) {
              std::cerr << "fail: vertical overflow get key" << i << '\n';
              return 1;
            }
          }

          fs::remove_all(dir);
        }

        if (lsm::ComputeHorizontalTieringK(2, 6) != 3) {
          std::cerr << "fail: Figure 5 k init (ell=2, N/B=6 -> k=3)\n";
          return 1;
        }

        {
          const std::string engine_manifest_dir = "/tmp/spruce_engine_manifest_test";
          fs::remove_all(engine_manifest_dir);

          lsm::Config manifest_engine_cfg;
          manifest_engine_cfg.data_dir = engine_manifest_dir;
          manifest_engine_cfg.buffer_size_bytes = 32;

          {
            lsm::LSMEngine manifest_db(manifest_engine_cfg);
            manifest_db.Put("manifest_key", std::string(25, 'y'));
          }

          if (!fs::exists(engine_manifest_dir + "/MANIFEST")) {
            std::cerr << "fail: MANIFEST missing after flush\n";
            return 1;
          }

          const auto manifest = lsm::LoadManifest(engine_manifest_dir);
          if (manifest.sst_entries.size() != 1) {
            std::cerr << "fail: MANIFEST sst count\n";
            return 1;
          }

          lsm::LSMEngine manifest_reopened(manifest_engine_cfg);
          if (manifest_reopened.Get("manifest_key") !=
              std::optional<std::string>(std::string(25, 'y'))) {
            std::cerr << "fail: engine manifest reopen\n";
            return 1;
          }

          fs::remove_all(engine_manifest_dir);
        }

        {
          const std::string dir = "/tmp/spruce_figure5_test";
          fs::remove_all(dir);
          const lsm::Config figure5_cfg = Figure5Config(dir);

          // Figure 5: ell=2, N/B=6 -> k=3. Each flush decrements C[0].
          // Flush 1: C=[2,3], one run on level 0.
          {
            lsm::LSMEngine db(figure5_cfg);
            PutFlushBatch(db, 0, 3);
          }
          {
            const auto manifest = lsm::LoadManifest(dir);
            if (manifest.k != 3) {
              std::cerr << "fail: figure5 k after flush 1\n";
              return 1;
            }
            if (!CountersEqual(manifest, {2, 3})) {
              std::cerr << "fail: figure5 counters after flush 1\n";
              return 1;
            }
            if (CountSstsAtLevel(manifest, 0) != 1 ||
                CountSstsAtLevel(manifest, 1) != 0) {
              std::cerr << "fail: figure5 layout after flush 1\n";
              return 1;
            }
          }

          // Flush 2: C=[1,3], two runs on level 0.
          {
            lsm::LSMEngine db(figure5_cfg);
            PutFlushBatch(db, 3, 3);
          }
          {
            const auto manifest = lsm::LoadManifest(dir);
            if (!CountersEqual(manifest, {1, 3})) {
              std::cerr << "fail: figure5 counters after flush 2\n";
              return 1;
            }
            if (CountSstsAtLevel(manifest, 0) != 2 ||
                CountSstsAtLevel(manifest, 1) != 0) {
              std::cerr << "fail: figure5 layout after flush 2\n";
              return 1;
            }
          }

          // Flush 3: C[0] hits 0 -> compact level 0 -> C=[2,2], one run on level 1.
          {
            lsm::LSMEngine db(figure5_cfg);
            PutFlushBatch(db, 6, 3);
          }
          {
            const auto manifest = lsm::LoadManifest(dir);
            if (!CountersEqual(manifest, {2, 2})) {
              std::cerr << "fail: figure5 counters after flush 3\n";
              return 1;
            }
            if (CountSstsAtLevel(manifest, 0) != 0 ||
                CountSstsAtLevel(manifest, 1) != 1) {
              std::cerr << "fail: figure5 layout after flush 3\n";
              return 1;
            }
          }

          // Reopen restores counters + tiered layout; all keys still readable.
          {
            lsm::LSMEngine db(figure5_cfg);
            const auto manifest = lsm::LoadManifest(dir);
            if (!CountersEqual(manifest, {2, 2})) {
              std::cerr << "fail: figure5 counters after reopen\n";
              return 1;
            }
            for (int i = 0; i < 9; ++i) {
              if (db.Get("key" + std::to_string(i)) !=
                  std::optional<std::string>(std::string(25, 'x'))) {
                std::cerr << "fail: figure5 get after reopen key" << i << '\n';
                return 1;
              }
            }
          }

          fs::remove_all(dir);
        }

        {
          lsm::Config compact_cfg;
          compact_cfg.data_dir = "/tmp/spruce_compact_test";
          compact_cfg.buffer_size_bytes = 32;
          compact_cfg.estimated_data_buffers = 6;
          compact_cfg.ell_horizontal = 2;
          std::filesystem::remove_all(compact_cfg.data_dir);

          lsm::LSMEngine compact_db(compact_cfg);
          for (int i = 0; i < 9; ++i) {
            compact_db.Put("key" + std::to_string(i), std::string(25, 'x'));
          }

          if (compact_db.Get("key0") != std::optional<std::string>(std::string(25, 'x'))) {
            std::cerr << "fail: get after compaction\n";
            return 1;
          }

          std::filesystem::remove_all(compact_cfg.data_dir);
        }

        {
          lsm::Config reopen_cfg;
          reopen_cfg.data_dir = "/tmp/spruce_reopen_compact_test";
          reopen_cfg.buffer_size_bytes = 32;
          reopen_cfg.estimated_data_buffers = 6;
          reopen_cfg.ell_horizontal = 2;
          std::filesystem::remove_all(reopen_cfg.data_dir);

          {
            lsm::LSMEngine db(reopen_cfg);
            for (int i = 0; i < 9; ++i) {
              db.Put("key" + std::to_string(i), std::string(25, 'x'));
            }
          }

          lsm::LSMEngine reopened(reopen_cfg);
          if (reopened.Get("key0") != std::optional<std::string>(std::string(25, 'x'))) {
            std::cerr << "fail: reopen after compaction\n";
            return 1;
          }

          std::filesystem::remove_all(reopen_cfg.data_dir);
        }

    std::cout << "memtable ok\n";
    return 0;




}
