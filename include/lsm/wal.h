#pragma once
#include <string_view>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace lsm {

enum class WalOp : std::uint8_t { kPut = 0, kDelete = 1 };

struct WalRecord {
  WalOp op;
  std::string key;
  std::optional<std::string> value;  // nullopt for Delete
};

class WalWriter {
 public:
  explicit WalWriter(std::string path);
  void AppendPut(std::string key, std::string value);
  void AppendDelete(std::string key);
  void Sync();
  void Truncate();

 private:
  std::string path_;
  std::fstream file_;
  void WriteRecord(WalOp op, std::string_view key, std::string_view value);
};

class MemTable;
std::vector<WalRecord> ReadWal(const std::string& path);
void ReplayWal(const std::string& path, MemTable& mt);


}  

