#pragma once
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lsm {
    void WriteSSTable(
        const std::string& path,
        const std::vector<std::pair<std::string, std::optional<std::string>>>& rows);
    
    #include <cstdint>

    struct BlockHandle {
    std::string first_key;
    std::uint64_t offset;
    std::uint32_t size;
    };

    struct Footer {
    std::vector<BlockHandle> index;
    std::string min_key;
    std::string max_key;
    std::uint64_t entry_count;
    };

    class SSTableWriter {
        public:
         explicit SSTableWriter(size_t block_size_bytes);
         void Add(std::string key, std::optional<std::string> value);
         void Finish(std::ostream& out);
       
        private:
         static std::string EncodeEntry(const std::string& key,
                                        const std::optional<std::string>& value);
         void FlushCurrentBlock();
       
         size_t block_size_bytes_;
         std::string current_block_;
         std::string first_key_in_block_;
         bool block_has_entries_ = false;
       
         std::vector<std::string> completed_blocks_;
         std::vector<BlockHandle> index_;
       
         std::string min_key_;
         std::string max_key_;
         bool has_entries_ = false;
         std::uint64_t entry_count_ = 0;
         std::uint64_t next_offset_ = 0;
       };
       
       class SSTable {
        public:
         static SSTable Open(const std::string& path);
         std::optional<std::optional<std::string>> Get(std::string_view key) const;
         // outer nullopt = key not in file; inner nullopt = tombstone
       };

    
}