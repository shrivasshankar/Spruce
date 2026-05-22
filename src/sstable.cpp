#include "lsm/sstable.h"

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
  (void)path;
  (void)rows;
}

}  // namespace lsm
