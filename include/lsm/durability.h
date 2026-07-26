#pragma once

#include <string>

namespace lsm::durability {

// Forces a file's contents to stable storage.
//
// std::ostream::flush() and streambuf::pubsync() only push bytes out of the
// C++ userspace buffer into the kernel page cache. That survives a process
// crash but not a power loss, which is the case a WAL exists to handle. These
// helpers issue the real kernel barrier.
void SyncFd(int fd, const std::string& path_for_errors);
void SyncPath(const std::string& path);

// Forces a directory entry change (create, rename, unlink) to stable storage.
// Without this a file's bytes can be durable while the name pointing at them
// is not, so recovery sees a missing or half-installed file.
void SyncDir(const std::string& dir_path);

}  // namespace lsm::durability
