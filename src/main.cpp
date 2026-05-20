#include <iostream>
#include "lsm/memtable.h"

int main() {
    lsm::MemTable mt;
    mt.Put("a", "1");
    auto v = mt.Get("a");
    if (!v || *v != "1") {
        std::cerr << "fail\n";
        return 1;
    }
    std::cout << "memtable ok\n";
    return 0;
}
