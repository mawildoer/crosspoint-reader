#pragma once
#include <fcntl.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
inline std::map<std::string, std::string> files;
inline bool shortWrite = false;
inline int writeCount = 0, shortWriteAt = 0;
inline int readCount = 0, failReadAt = 0;
inline int renameCount = 0, failRenameAt = 0;
inline std::map<std::string, int> openHandles;
class HalFile {
  std::string path;
  size_t at = 0;
  bool valid = false;

 public:
  HalFile() = default;
  HalFile(std::string p, int flags) : path(p), valid(true) {
    ++openHandles[path];
    if (flags & O_TRUNC) files[path].clear();
    if (flags & O_APPEND) at = files[path].size();
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this == &other) return *this;
    close();
    path = std::move(other.path);
    at = other.at;
    valid = other.valid;
    other.valid = false;
    return *this;
  }
  ~HalFile() { close(); }
  explicit operator bool() const { return valid; }
  int read(void* dst, size_t n) {
    if (++readCount == failReadAt) return -1;
    auto& s = files[path];
    n = std::min(n, s.size() - at);
    memcpy(dst, s.data() + at, n);
    at += n;
    return int(n);
  }
  size_t write(const void* src, size_t n) {
    if (++writeCount == shortWriteAt || shortWrite) {
      n /= 2;
      shortWrite = false;
    }
    auto& s = files[path];
    if (at + n > s.size()) s.resize(at + n);
    memcpy(s.data() + at, src, n);
    at += n;
    return n;
  }
  uint64_t size() { return files[path].size(); }
  bool truncate(uint64_t n) {
    files[path].resize(n);
    return true;
  }
  bool close() {
    if (valid) --openHandles[path];
    valid = false;
    return true;
  }
};
struct FakeStorage {
  bool exists(const char* p) { return files.count(p); }
  bool mkdir(const char* p) {
    const std::string path(p);
    for (size_t i = 1; i < path.size(); ++i)
      if (path[i] == '/') files.try_emplace(path.substr(0, i), "");
    files[p] = "";
    return true;
  }
  HalFile open(const char* p, int f = O_RDONLY) {
    if (!exists(p) && !(f & O_CREAT)) return {};
    return HalFile(p, f);
  }
  bool remove(const char* p) {
    assert(openHandles[p] == 0);
    files.erase(p);
    return true;
  }
  bool rename(const char* from, const char* to) {
    if (++renameCount == failRenameAt || !exists(from) || exists(to)) return false;
    assert(openHandles[from] == 0);
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool replaceFile(const char* a, const char* b) {
    if (!exists(a)) return false;
    files[b] = files[a];
    files.erase(a);
    return true;
  }
};
inline FakeStorage Storage;
