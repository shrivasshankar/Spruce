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
          m.sst_paths = {"sst/000001.sst", "sst/000002.sst"};
          lsm::SaveManifest(manifest_dir, m);
    
          const auto loaded = lsm::LoadManifest(manifest_dir);
          if (loaded.next_sst_id != 3 || loaded.sst_paths.size() != 2) {
            std::cerr << "fail: manifest load\n";
            return 1;
          }
          if (loaded.sst_paths[0] != "sst/000001.sst" ||
              loaded.sst_paths[1] != "sst/000002.sst") {
            std::cerr << "fail: manifest paths\n";
            return 1;
          }
    
          fs::remove_all(manifest_dir);
        }

    std::cout << "memtable ok\n";
    return 0;




}
