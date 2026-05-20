#include "lsm/memtable.h"

void lsm::MemTable::Put(std::string key, std::string value) {
    data_[std::move(key)] = std::move(value);
  }

std::optional<std::string> lsm::MemTable::Get(std::string_view key) const {
    auto it = data_.find(std::string(key));
    if (it != data_.end()) {
        return it->second;
    }
    return std::nullopt;
}