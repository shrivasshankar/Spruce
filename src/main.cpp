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

    lsm::LSMEngine db(cfg);
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
          m.compaction_counters = {3, 3};
          m.sst_entries = {
              {"sst/000001.sst", 0},
              {"sst/000002.sst", 1},
          };
          lsm::SaveManifest(manifest_dir, m);

          const auto loaded = lsm::LoadManifest(manifest_dir);
          if (loaded.next_sst_id != 3 || loaded.sst_entries.size() != 2) {
            std::cerr << "fail: manifest load\n";
            return 1;
          }
          if (loaded.k != 3 || loaded.compaction_counters != std::vector<int>({3, 3})) {
            std::cerr << "fail: manifest counters\n";
            return 1;
          }
          if (loaded.sst_entries[0].rel_path != "sst/000001.sst" ||
              loaded.sst_entries[0].level != 0 ||
              loaded.sst_entries[1].rel_path != "sst/000002.sst" ||
              loaded.sst_entries[1].level != 1) {
            std::cerr << "fail: manifest entries\n";
            return 1;
          }
    
          fs::remove_all(manifest_dir);
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
          lsm::Config cfg;
          cfg.data_dir = "/tmp/spruce_block3_test";
          cfg.buffer_size_bytes = 32;
          cfg.estimated_data_buffers = 6;  // k=3
          cfg.ell_horizontal = 2;
          std::filesystem::remove_all(cfg.data_dir);
        
          lsm::LSMEngine db(cfg);
          for (int i = 0; i < 9; ++i) {
            db.Put("key" + std::to_string(i), std::string(25, 'x'));
          }
          // Watch stderr: after 3rd flush expect C[0]=2 C[1]=2
        
          std::filesystem::remove_all(cfg.data_dir);
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
          lsm::Config cfg;
          cfg.data_dir = "/tmp/spruce_reopen_compact_test";
          cfg.buffer_size_bytes = 32;
          cfg.estimated_data_buffers = 6;
          cfg.ell_horizontal = 2;
          std::filesystem::remove_all(cfg.data_dir);

          {
            lsm::LSMEngine db(cfg);
            for (int i = 0; i < 9; ++i) {
              db.Put("key" + std::to_string(i), std::string(25, 'x'));
            }
          }

          lsm::LSMEngine reopened(cfg);
          if (reopened.Get("key0") != std::optional<std::string>(std::string(25, 'x'))) {
            std::cerr << "fail: reopen after compaction\n";
            return 1;
          }

          std::filesystem::remove_all(cfg.data_dir);
        }

    std::cout << "memtable ok\n";
    return 0;




}
