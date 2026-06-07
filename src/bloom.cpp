#include "lsm/bloom.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace lsm {
namespace {

std::uint32_t Hash32(std::string_view key, std::uint32_t seed) {
  std::uint32_t h = seed;
  for (unsigned char c : key) {
    h ^= static_cast<std::uint32_t>(c);
    h *= 0x01000193u;
  }
  return h;
}

void AppendU32(std::string& out, std::uint32_t v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void AppendU64(std::string& out, std::uint64_t v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

bool ReadU32(std::string_view data, std::size_t& pos, std::uint32_t& out) {
  if (pos + sizeof(out) > data.size()) {
    return false;
  }
  std::memcpy(&out, data.data() + pos, sizeof(out));
  pos += sizeof(out);
  return true;
}

bool ReadU64(std::string_view data, std::size_t& pos, std::uint64_t& out) {
  if (pos + sizeof(out) > data.size()) {
    return false;
  }
  std::memcpy(&out, data.data() + pos, sizeof(out));
  pos += sizeof(out);
  return true;
}

}  // namespace

void BloomFilter::Init(std::size_t key_count, int bits_per_key) {
  bit_count_ = std::max<std::size_t>(64, key_count * static_cast<std::size_t>(bits_per_key));
  num_probes_ = std::max(1, static_cast<int>(std::lround(0.69 * bits_per_key)));
  const std::size_t word_count = (bit_count_ + 63) / 64;
  bits_.assign(word_count, 0);
}

void BloomFilter::Add(std::string_view key) {
  if (bit_count_ == 0) {
    return;
  }

  const std::uint32_t h1 = Hash32(key, 0xbc9f1d34u);
  const std::uint32_t h2 = Hash32(key, 0x7a5b223cu) | 1u;

  for (int i = 0; i < num_probes_; ++i) {
    const std::uint64_t combined =
        static_cast<std::uint64_t>(h1) +
        static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(h2);
    const std::size_t bit = combined % bit_count_;
    bits_[bit / 64] |= (std::uint64_t{1} << (bit % 64));
  }
}

BloomFilter BloomFilter::Build(const std::vector<std::string_view>& keys,
                               int bits_per_key) {
  BloomFilter filter;
  if (keys.empty()) {
    return filter;
  }

  filter.Init(keys.size(), bits_per_key);
  for (const std::string_view key : keys) {
    filter.Add(key);
  }
  return filter;
}

bool BloomFilter::MayContain(std::string_view key) const {
  if (Empty()) {
    return true;
  }

  const std::uint32_t h1 = Hash32(key, 0xbc9f1d34u);
  const std::uint32_t h2 = Hash32(key, 0x7a5b223cu) | 1u;

  for (int i = 0; i < num_probes_; ++i) {
    const std::uint64_t combined =
        static_cast<std::uint64_t>(h1) +
        static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(h2);
    const std::size_t bit = combined % bit_count_;
    if ((bits_[bit / 64] & (std::uint64_t{1} << (bit % 64))) == 0) {
      return false;
    }
  }
  return true;
}

std::string BloomFilter::Serialize() const {
  std::string out;
  AppendU64(out, static_cast<std::uint64_t>(bit_count_));
  AppendU32(out, static_cast<std::uint32_t>(num_probes_));
  AppendU32(out, static_cast<std::uint32_t>(bits_.size()));
  for (std::uint64_t word : bits_) {
    AppendU64(out, word);
  }
  return out;
}

BloomFilter BloomFilter::Deserialize(std::string_view blob) {
  BloomFilter filter;
  if (blob.empty()) {
    return filter;
  }

  std::size_t pos = 0;
  std::uint64_t bit_count = 0;
  std::uint32_t num_probes = 0;
  std::uint32_t word_count = 0;

  if (!ReadU64(blob, pos, bit_count) || !ReadU32(blob, pos, num_probes) ||
      !ReadU32(blob, pos, word_count)) {
    throw std::runtime_error("corrupt Bloom filter");
  }

  filter.bit_count_ = static_cast<std::size_t>(bit_count);
  filter.num_probes_ = static_cast<int>(num_probes);
  filter.bits_.resize(word_count);
  for (std::uint32_t i = 0; i < word_count; ++i) {
    if (!ReadU64(blob, pos, filter.bits_[i])) {
      throw std::runtime_error("corrupt Bloom filter");
    }
  }

  if (pos != blob.size()) {
    throw std::runtime_error("corrupt Bloom filter trailing bytes");
  }
  return filter;
}

}  // namespace lsm
