#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Articles on the SD card (layout: docs/paperboy-protocol.md, "Article store").
namespace phonesync::store {

inline constexpr char kDir[] = "/Articles";
inline constexpr char kIndexPath[] = "/Articles/index.tsv";
inline constexpr char kReadPath[] = "/Articles/read.txt";
inline constexpr char kIncomingPath[] = "/Articles/incoming.tmp";
inline constexpr size_t kIdHexLen = 16;

struct Entry {
  char id[kIdHexLen + 1];
  uint32_t time;
  uint32_t addedTime = 0;
  bool read;
  std::string source;
  std::string title;
  std::string author;
};

// A rebuild-only snapshot: two streaming passes, then O(log N) lookup.
// No strings are retained. At most 16 bytes/entry, released after rebuilding.
class TimeSnapshot {
 public:
  bool load(size_t maxEntries);
  bool lookup(const char* idHex, uint32_t& added, uint32_t& published) const;

 private:
  struct Stamp {
    uint64_t id;
    uint32_t added;
    uint32_t published;
  };
  std::unique_ptr<Stamp[]> stamps;
  size_t count = 0;
};

bool ensureDir();
// "/Articles/<id>.epub"
std::string pathFor(const char* idHex);
// The id of an article path, or false when the path is not one.
bool idOfPath(const char* path, char idHex[kIdHexLen + 1]);

// Calls visit for every index line, oldest first; visit returns false to stop.
// The strings are valid only during the call.
using Visitor = bool (*)(void* ctx, const char* idHex, uint32_t time, const char* source, const char* title,
                         const char* author);
bool forEach(Visitor visit, void* ctx);

bool contains(const char* idHex);
// One record at a time: no article-count-sized metadata cache.
bool lookup(const char* idHex, Entry& out);
void formatSubtitle(const Entry& entry, std::string& out);
void formatDate(uint32_t timestamp, std::string& out);
bool append(const char* idHex, uint32_t time, const char* source, size_t sourceLen, const char* title, size_t titleLen,
            const char* author, size_t authorLen, uint32_t addedTime = 0);
// Removes the article file and its index line. True when it is gone (also when it never existed).
bool remove(const char* idHex);

// Ids that have been opened, as 64-bit values (the 8 id bytes, big-endian).
bool loadReadIds(std::vector<uint64_t>& out);
uint64_t idValue(const char* idHex);
bool markRead(const char* idHex);
// Forgets that an article was read: one that is sent again starts unread.
bool clearRead(const char* idHex);

// The newest maxEntries articles, newest first.
bool loadNewest(std::vector<Entry>& out, size_t maxEntries);

}  // namespace phonesync::store
