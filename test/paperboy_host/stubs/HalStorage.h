#pragma once
#include <fcntl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
inline std::map<std::string, std::string> files;
inline bool shortWrite = false;
inline int readCount = 0, failReadAt = 0;
class HalFile {
  std::string path;
  size_t at = 0;
  bool valid = false;

 public:
  HalFile() = default;
  HalFile(std::string p, int flags) : path(p), valid(true) {
    if (flags & O_TRUNC) files[path].clear();
    if (flags & O_APPEND) at = files[path].size();
  }
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
    if (shortWrite) {
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
  void close() { valid = false; }
};
struct FakeStorage {
  bool exists(const char* p) { return files.count(p); }
  bool mkdir(const char* p) {
    files[p] = "";
    return true;
  }
  HalFile open(const char* p, int f = O_RDONLY) {
    if (!exists(p) && !(f & O_CREAT)) return {};
    return HalFile(p, f);
  }
  bool remove(const char* p) {
    files.erase(p);
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
