#include "lsm/manifest.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

void AppendU32(std::string& out, std::uint32_t v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void AppendU64(std::string& out, std::uint64_t v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void AppendI32(std::string& out, std::int32_t v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

bool ReadI32(const std::string& data, std::size_t& pos, std::int32_t& out) {
  if (pos + sizeof(out) > data.size()) return false;
  std::memcpy(&out, data.data() + pos, sizeof(out));
  pos += sizeof(out);
  return true;
}

bool ReadU32(const std::string& data, std::size_t& pos, std::uint32_t& out) {
  if (pos + sizeof(out) > data.size()) return false;
  std::memcpy(&out, data.data() + pos, sizeof(out));
  pos += sizeof(out);
  return true;
}

bool ReadU64(const std::string& data, std::size_t& pos, std::uint64_t& out) {
  if (pos + sizeof(out) > data.size()) return false;
  std::memcpy(&out, data.data() + pos, sizeof(out));
  pos += sizeof(out);
  return true;
}

bool ReadString(const std::string& data, std::size_t& pos, std::string& out) {
  std::uint32_t len = 0;
  if (!ReadU32(data, pos, len)) return false;
  if (pos + len > data.size()) return false;
  out.assign(data.data() + pos, len);
  pos += len;
  return true;
}

}  // namespace

namespace lsm {

std::string ManifestPath(const std::string& data_dir) {
  return (fs::path(data_dir) / "MANIFEST").string();
}

Manifest LoadManifest(const std::string& data_dir) {
  Manifest manifest;
  const std::string path = ManifestPath(data_dir);

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return manifest;  // fresh db
  }

  std::string blob((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (blob.size() < 16) {
    throw std::runtime_error("MANIFEST too small: " + path);
  }

  if (blob[0] != 'M' || blob[1] != 'A' || blob[2] != 'N' || blob[3] != 'I') {
    throw std::runtime_error("bad MANIFEST magic: " + path);
  }

  std::size_t pos = 4;
  if (!ReadU64(blob, pos, manifest.next_sst_id)) {
    throw std::runtime_error("failed to parse MANIFEST: " + path);
  }

  std::int32_t k = 1;
  if (!ReadI32(blob, pos, k)) {
    throw std::runtime_error("failed to parse MANIFEST: " + path);
  }
  manifest.k = k;

  std::int32_t n = 1;
  if (!ReadI32(blob, pos, n)) {
    throw std::runtime_error("failed to parse MANIFEST: " + path);
  }
  manifest.n = n;

  std::uint32_t counter_count = 0;
  if (!ReadU32(blob, pos, counter_count)) {
    throw std::runtime_error("failed to parse MANIFEST: " + path);
  }

  manifest.compaction_counters.clear();
  manifest.compaction_counters.reserve(counter_count);
  for (std::uint32_t i = 0; i < counter_count; ++i) {
    std::int32_t counter = 0;
    if (!ReadI32(blob, pos, counter)) {
      throw std::runtime_error("failed to parse MANIFEST: " + path);
    }
    manifest.compaction_counters.push_back(counter);
  }

  std::uint32_t sst_count = 0;
  if (!ReadU32(blob, pos, sst_count)) {
    throw std::runtime_error("failed to parse MANIFEST: " + path);
  }

  manifest.sst_entries.clear();
  manifest.sst_entries.reserve(sst_count);
  for (std::uint32_t i = 0; i < sst_count; ++i) {
    SstEntry entry;
    if (!ReadString(blob, pos, entry.rel_path)) {
      throw std::runtime_error("failed to parse MANIFEST: " + path);
    }
    std::uint32_t level = 0;
    if (!ReadU32(blob, pos, level)) {
      throw std::runtime_error("failed to parse MANIFEST: " + path);
    }
    entry.level = level;
    manifest.sst_entries.push_back(std::move(entry));
  }

  if (pos != blob.size()) {
    throw std::runtime_error("trailing bytes in MANIFEST: " + path);
  }

  return manifest;
}

void SaveManifest(const std::string& data_dir, const Manifest& manifest) {
  fs::create_directories(data_dir);

  std::string blob;
  blob.append("MANI", 4);
  AppendU64(blob, manifest.next_sst_id);

  AppendI32(blob, static_cast<std::int32_t>(manifest.k));
  AppendI32(blob, static_cast<std::int32_t>(manifest.n));
  AppendU32(blob, static_cast<std::uint32_t>(manifest.compaction_counters.size()));
  for (int counter : manifest.compaction_counters) {
    AppendI32(blob, static_cast<std::int32_t>(counter));
  }

  AppendU32(blob, static_cast<std::uint32_t>(manifest.sst_entries.size()));
  for (const auto& entry : manifest.sst_entries) {
    AppendU32(blob, static_cast<std::uint32_t>(entry.rel_path.size()));
    blob.append(entry.rel_path);
    AppendU32(blob, entry.level);
  }

  const std::string path = ManifestPath(data_dir);
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("failed to write MANIFEST: " + tmp_path);
    }
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    if (!out.good()) {
      throw std::runtime_error("MANIFEST write failed: " + tmp_path);
    }

    out.flush();
    if (out.rdbuf()->pubsync() != 0) {
      throw std::runtime_error("MANIFEST sync failed: " + tmp_path);
    }
  }

  std::error_code ec;
  fs::rename(tmp_path, path, ec);
  if (ec) {
    throw std::runtime_error("failed to install MANIFEST: " + path);
  }
}

}  // namespace lsm