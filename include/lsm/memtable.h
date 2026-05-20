#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace lsm {

class MemTable {
 public:
  void Put(std::string key, std::string value);
  std::optional<std::string> Get(std::string_view key) const;
  void Delete(std::string key);

 private:
    std::map<std::string, std::optional<std::string>> data_;

 
};

}  // namespace lsm