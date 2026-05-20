#include "lsm/memtable.h"

void lsm::MemTable::Put(std::string key, std::string value) {
    data_[std::move(key)] = std::move(value);  
  }

std::optional<std::string> lsm::MemTable::Get(std::string_view key) const {
    auto it = data_.find(std::string(key));
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void lsm::MemTable::Delete(std::string key) {
    data_[std::move(key)] = std::nullopt;
}

std::vector<std::pair<std::string, std::optional<std::string>>>
lsm::MemTable::GetSorted() const {
  std::vector<std::pair<std::string, std::optional<std::string>>> out;
  out.reserve(data_.size());

  for (const auto& [key, value] : data_) {
    out.emplace_back(key, value);
  }
  return out;
}