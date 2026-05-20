#include <iostream>
#include "lsm/memtable.h"

int main() {
    lsm::MemTable mt;

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

    if (mt.SizeBytes() == 0) {
        std::cerr << "fail: empty size\n";
        return 1;
      }
    if (mt.IsFull()) {  // should not be full
        std::cerr << "fail: should not be full\n";
        return 1;
    }

    lsm::MemTable empty;
    if (empty.SizeBytes() != 0 || empty.IsFull()) {
    std::cerr << "fail: empty memtable\n";
    return 1;
    }

    std::cout << "memtable ok\n";
    return 0;


}
