#include "lsm/sstable.h"
#include "lsm/sstable.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
namespace lsm {

// SSTable v1 on-disk layout (blueprint):
//   [magic: 4 bytes "SPRU"][entry][entry]...
// Each entry:
//   [key_len: u32][key bytes][value_len: u32][value bytes?]
// Tombstone: value_len == 0 (no value bytes).
// Rows must already be sorted by key (MemTable::GetSorted()).

void WriteSSTable(
    const std::string& path,
    const std::vector<std::pair<std::string, std::optional<std::string>>>& rows) {

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("failed to open SSTable: " + path);
  }

  const char magic[4] = {'S', 'P', 'R', 'U'};
  out.write(magic, 4);

  for (const auto& [key, value] : rows) {
    const auto key_len = static_cast<std::uint32_t>(key.size());
    out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    out.write(key.data(), static_cast<std::streamsize>(key.size()));

    if (value) {
      const auto value_len = static_cast<std::uint32_t>(value->size());
      out.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
      out.write(value->data(), static_cast<std::streamsize>(value->size()));
    } else {
      const std::uint32_t zero = 0;
      out.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
    }
  }

  if (!out.good()) {
    throw std::runtime_error("SSTable write failed");
  }
}

}  // namespace lsm
