#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lsm {

class MemTable {
 public:
  explicit MemTable(size_t buffer_size_bytes);
  void Put(std::string key, std::string value);
  std::optional<std::string> Get(std::string_view key) const;
  std::optional<std::optional<std::string>> Lookup(std::string_view key) const;
  void Delete(std::string key);
  std::vector<std::pair<std::string, std::optional<std::string>>> GetSorted() const;
  size_t SizeBytes() const;
  bool IsFull() const;

 private:
    std::map<std::string, std::optional<std::string>> data_;
    size_t buffer_size_bytes_;

 
};

}  // namespace lsm