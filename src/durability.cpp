#include "lsm/durability.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace lsm::durability {
namespace {

std::string ErrnoMessage(const std::string& what, const std::string& path) {
  return what + " '" + path + "': " + std::strerror(errno);
}

}  // namespace

void SyncFd(int fd, const std::string& path_for_errors) {
#ifdef __APPLE__
  // On macOS fsync() only hands the data to the drive; it does not ask the
  // drive to flush its own volatile write cache, so a power loss can still
  // lose it. F_FULLFSYNC is the call that actually waits for the media.
  if (::fcntl(fd, F_FULLFSYNC, 0) != -1) {
    return;
  }
  // Not every filesystem implements it (notably some network mounts), in
  // which case fsync() is the best available barrier.
  if (errno != ENOTSUP && errno != EINVAL && errno != EOPNOTSUPP) {
    throw std::runtime_error(ErrnoMessage("F_FULLFSYNC failed on", path_for_errors));
  }
#endif
  if (::fsync(fd) == -1) {
    throw std::runtime_error(ErrnoMessage("fsync failed on", path_for_errors));
  }
}

void SyncPath(const std::string& path) {
  // fsync acts on the file, not on this particular descriptor, so syncing
  // through a freshly opened fd still flushes pages written through another.
  const int fd = ::open(path.c_str(), O_WRONLY);
  if (fd == -1) {
    throw std::runtime_error(ErrnoMessage("failed to open for sync", path));
  }
  try {
    SyncFd(fd, path);
  } catch (...) {
    ::close(fd);
    throw;
  }
  ::close(fd);
}

void SyncDir(const std::string& dir_path) {
  // A directory can only be opened read-only; fsync on that fd is what
  // persists the entry changes made inside it.
  const int fd = ::open(dir_path.c_str(), O_RDONLY);
  if (fd == -1) {
    throw std::runtime_error(ErrnoMessage("failed to open directory for sync", dir_path));
  }
  try {
    SyncFd(fd, dir_path);
  } catch (...) {
    ::close(fd);
    throw;
  }
  ::close(fd);
}

}  // namespace lsm::durability
