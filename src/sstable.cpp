#include "lsm/sstable.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <ostream>


namespace {

  void AppendU32(std::string& out, std::uint32_t v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(v));
  }
  void AppendU64(std::string& out, std::uint64_t v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(v));
  }
  
  std::string SerializeFooter(const lsm::Footer& footer) {
    std::string blob;
    AppendU64(blob, footer.entry_count);
  
    AppendU32(blob, static_cast<std::uint32_t>(footer.min_key.size()));
    blob.append(footer.min_key);
    AppendU32(blob, static_cast<std::uint32_t>(footer.max_key.size()));
    blob.append(footer.max_key);
  
    AppendU32(blob, static_cast<std::uint32_t>(footer.index.size()));
    for (const auto& handle : footer.index) {
      AppendU32(blob, static_cast<std::uint32_t>(handle.first_key.size()));
      blob.append(handle.first_key);
      AppendU64(blob, handle.offset);
      AppendU32(blob, handle.size);
    }
    return blob;
  }

  
  
  }
namespace lsm {

// SSTable v1 on-disk layout (blueprint):
//   [magic: 4 bytes "SPRU"][entry][entry]...
// Each entry:
//   [key_len: u32][key bytes][value_len: u32][value bytes?]
// Tombstone: value_len == 0 (no value bytes).
// Rows must already be sorted by key (MemTable::GetSorted()).
lsm::SSTableWriter::SSTableWriter(size_t block_size_bytes)
    : block_size_bytes_(block_size_bytes) {}

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

std::string lsm::SSTableWriter::EncodeEntry(
  const std::string& key,
  const std::optional<std::string>& value) {
std::string out;
const auto key_len = static_cast<std::uint32_t>(key.size());
out.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
out.append(key);

if (value) {
  const auto value_len = static_cast<std::uint32_t>(value->size());
  out.append(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
  out.append(*value);
} else {
  const std::uint32_t zero = 0;
  out.append(reinterpret_cast<const char*>(&zero), sizeof(zero));
}
return out;
}

void lsm::SSTableWriter::FlushCurrentBlock() {
  if (current_block_.empty()) {
    return;
  }

  index_.push_back(BlockHandle{
      first_key_in_block_,
      next_offset_,
      static_cast<std::uint32_t>(current_block_.size()),
  });
  next_offset_ += current_block_.size();
  completed_blocks_.push_back(std::move(current_block_));
  current_block_.clear();
  block_has_entries_ = false;
}

void lsm::SSTableWriter::Add(std::string key, std::optional<std::string> value) {
  const std::string encoded = EncodeEntry(key, value);

  // If this entry alone is bigger than block_size, you need a policy.
  // For MVP: throw, or allow oversized block (keep it simple — throw).
  if (encoded.size() > block_size_bytes_) {
    throw std::runtime_error("single entry exceeds block size");
  }

  if (!current_block_.empty() &&
      current_block_.size() + encoded.size() > block_size_bytes_) {
    FlushCurrentBlock();
  }

  if (!block_has_entries_) {
    first_key_in_block_ = key;
    block_has_entries_ = true;
  }

  current_block_.append(encoded);

  if (!has_entries_) {
    min_key_ = key;
    max_key_ = key;
    has_entries_ = true;
  } else {
    if (key < min_key_) min_key_ = key;
    if (key > max_key_) max_key_ = key;
  }
  ++entry_count_;
}
void lsm::SSTableWriter::Finish(std::ostream& out) {
  FlushCurrentBlock();

  for (const auto& block : completed_blocks_) {
    out.write(block.data(), static_cast<std::streamsize>(block.size()));
  }

  Footer footer{
      index_,
      has_entries_ ? min_key_ : "",
      has_entries_ ? max_key_ : "",
      entry_count_,
  };

  const std::string footer_blob = SerializeFooter(footer);
  out.write(footer_blob.data(), static_cast<std::streamsize>(footer_blob.size()));


  const std::uint64_t footer_size = footer_blob.size();
  out.write(reinterpret_cast<const char*>(&footer_size), sizeof(footer_size));

  const char magic[4] = {'S', 'P', 'R', 'U'};
  out.write(magic, 4);

  if (!out.good()) {
    throw std::runtime_error("SSTable finish failed");
  }
}

}  // namespace lsm


  // namespace
