#include "lsm/manifest.h"

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

  std::uint32_t count = 0;
  if (!ReadU32(blob, pos, count)) {
    throw std::runtime_error("failed to parse MANIFEST: " + path);
  }

  manifest.sst_paths.clear();
  manifest.sst_paths.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::string sst_path;
    if (!ReadString(blob, pos, sst_path)) {
      throw std::runtime_error("failed to parse MANIFEST: " + path);
    }
    manifest.sst_paths.push_back(std::move(sst_path));
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
  AppendU32(blob, static_cast<std::uint32_t>(manifest.sst_paths.size()));

  for (const auto& sst_path : manifest.sst_paths) {
    AppendU32(blob, static_cast<std::uint32_t>(sst_path.size()));
    blob.append(sst_path);
  }

  const std::string path = ManifestPath(data_dir);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("failed to write MANIFEST: " + path);
  }
  out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  if (!out.good()) {
    throw std::runtime_error("MANIFEST write failed: " + path);
  }

  out.flush();
  if (out.rdbuf()->pubsync() != 0) {
    throw std::runtime_error("MANIFEST sync failed: " + path);
  }
}

}  // namespace lsm