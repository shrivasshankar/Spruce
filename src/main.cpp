#include <iostream>
#include "lsm/memtable.h"
#include "lsm/wal.h"
#include <cstdio> 
#include "lsm/config.h"
#include "lsm/sstable.h"
#include <fstream>
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
    (void)table;

    std::cout << "memtable ok\n";
    return 0;




}
