#include "lsm/wal.h"
#include <stdexcept>
#include <string_view>

namespace lsm {
WalWriter::WalWriter(std::string path) {
  file_.open(path, std::ios::binary | std::ios::app);
  if (!file_) {
    throw std::runtime_error("failed to open WAL: " + path);
  }
}

  void WalWriter::WriteRecord(WalOp op, std::string_view key, std::string_view value) {
    const auto type = static_cast<std::uint8_t>(op);
    const auto key_len = static_cast<std::uint32_t>(key.size());
    const auto value_len = static_cast<std::uint32_t>(value.size());
  
    file_.write(reinterpret_cast<const char*>(&type), sizeof(type));
    file_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    file_.write(key.data(), static_cast<std::streamsize>(key.size()));
    file_.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
    if (value_len > 0) {
      file_.write(value.data(), static_cast<std::streamsize>(value.size()));
    }
  }

  void WalWriter::AppendPut(std::string key, std::string value) {
    WriteRecord(WalOp::kPut, key, value);
  }
  
  void WalWriter::AppendDelete(std::string key) {
    WriteRecord(WalOp::kDelete, key, "");
  }

  std::vector<WalRecord> ReadWal(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<WalRecord> records;
    if (!in) {
      return records;
    }
  
    while (true) {
      std::uint8_t type_raw = 0;
      std::uint32_t key_len = 0;
      if (!in.read(reinterpret_cast<char*>(&type_raw), sizeof(type_raw))) break;
      if (!in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len))) break;
  
      std::string key(key_len, '\0');
      if (key_len > 0 &&
          !in.read(key.data(), static_cast<std::streamsize>(key_len))) break;
  
      std::uint32_t value_len = 0;
      if (!in.read(reinterpret_cast<char*>(&value_len), sizeof(value_len))) break;
  
      std::optional<std::string> value;
      if (value_len > 0) {
        std::string val(value_len, '\0');
        if (!in.read(val.data(), static_cast<std::streamsize>(value_len))) break;
        value = std::move(val);
      }
  
      const auto op = static_cast<WalOp>(type_raw);
      records.push_back(WalRecord{op, std::move(key), value});
    }
    return records;
  }

  
}