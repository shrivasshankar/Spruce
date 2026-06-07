#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsm {

// Standard LSM Bloom filter: no false negatives, possible false positives.
class BloomFilter {
 public:
  BloomFilter() = default;

  static BloomFilter Build(const std::vector<std::string_view>& keys,
                           int bits_per_key = 10);

  bool Empty() const { return bit_count_ == 0; }
  bool MayContain(std::string_view key) const;

  std::string Serialize() const;
  static BloomFilter Deserialize(std::string_view blob);

 private:
  void Init(std::size_t key_count, int bits_per_key);
  void Add(std::string_view key);

  std::vector<std::uint64_t> bits_;
  std::size_t bit_count_ = 0;
  int num_probes_ = 0;
};

}  // namespace lsm
