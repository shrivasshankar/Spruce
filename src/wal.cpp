#include "lsm/wal.h"
#include <stdexcept>
#include <string_view>
#include "lsm/durability.h"
#include "lsm/memtable.h"
#include <array>
#include <cstddef>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

namespace {
  const std::array<std::uint32_t, 256>& Crc32Table() {
    static const auto table = [] {
      std::array<std::uint32_t, 256> t{};
      for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
          c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
      }
      return t;
    }();
    return table;
  }

  std::uint32_t Crc32Update(std::uint32_t crc, const void* data, std::size_t len) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto& table = Crc32Table();
    for (std::size_t i = 0; i < len; ++i) {
      crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
  }
  
  std::uint32_t Crc32(const void* data, std::size_t len) {
    return Crc32Update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
  }
  
} 

namespace lsm {
  WalWriter::WalWriter(std::string path) : path_(std::move(path)) {
    const fs::path dir = fs::path(path_).parent_path();
    fs::create_directories(dir);
    const bool is_new = !fs::exists(path_);
    file_.open(path_, std::ios::binary | std::ios::app);
    if (!file_) {
      throw std::runtime_error("failed to open WAL: " + path_);
    }
    if (is_new) {
      // A record is only recoverable if the WAL's own directory entry survives
      // the crash, so persist the name before anything is written into it.
      file_.flush();
      durability::SyncDir(dir.string());
    }
  }

  void WalWriter::WriteRecord(WalOp op, std::string_view key, std::string_view value) {
    const auto type = static_cast<std::uint8_t>(op);
    const auto key_len = static_cast<std::uint32_t>(key.size());
    const auto value_len = static_cast<std::uint32_t>(value.size());
    // payload = type + key_len + key + value_len + value (as bytes)
    std::string payload;
    payload.append(reinterpret_cast<const char*>(&type), sizeof(type));
    payload.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    payload.append(key.data(), key.size());
    payload.append(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
    if (value_len > 0) {
      payload.append(value.data(), value.size());
    }

    const std::uint32_t crc = Crc32(payload.data(), payload.size());
    file_.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
    file_.write(payload.data(), static_cast<std::streamsize>(payload.size()));

    if (!file_.good()) {
      throw std::runtime_error("WAL write failed");
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
      std::uint32_t stored_crc = 0;
    if (!in.read(reinterpret_cast<char*>(&stored_crc), sizeof(stored_crc))) {
      return records;
    }
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
  
      // rebuild payload exactly like WriteRecord
      std::string payload;
      payload.append(reinterpret_cast<const char*>(&type_raw), sizeof(type_raw));
      payload.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
      payload.append(key.data(), key.size());
      payload.append(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
      if (value_len > 0) {
        payload.append(value->data(), value->size());
      }
      if (Crc32(payload.data(), payload.size()) != stored_crc) break;
      // value_len == 0 is ambiguous on disk: a Put with an empty value and a
      // Delete both omit value bytes. A Put always carries a value, so
      // reconstruct it as "" instead of leaving the optional empty.
      if (static_cast<WalOp>(type_raw) == WalOp::kPut && !value.has_value()) {
        value = "";
      }
      records.push_back(WalRecord{static_cast<WalOp>(type_raw), std::move(key), value});
    }
    return records;

  }

  void ReplayWal(const std::string& path, MemTable& mt) {
    for (const auto& rec : ReadWal(path)) {
      if (rec.op == WalOp::kPut) {
        mt.Put(rec.key, *rec.value);
      } else {
        mt.Delete(rec.key);
      }
    }
  }

  void WalWriter::Sync() {
    // Push the userspace buffer into the kernel...
    file_.flush();
    if (!file_) {
      throw std::runtime_error("WAL flush failed: " + path_);
    }
    // ...then force the kernel's page cache onto the media. Without this the
    // record is only in RAM and a power loss takes back an acknowledged write.
    durability::SyncPath(path_);
  }
  void WalWriter::Truncate() {
    file_.close();
    file_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_) {
      throw std::runtime_error("failed to truncate WAL: " + path_);
    }
    file_.close();
    // The truncation itself has to reach the media before it counts: otherwise
    // a crash can resurrect the old records and replay writes that the SSTs
    // already contain.
    durability::SyncPath(path_);
    file_.open(path_, std::ios::binary | std::ios::app);
    if (!file_) {
      throw std::runtime_error("failed to reopen WAL: " + path_);
    }
  }

}

  


  
