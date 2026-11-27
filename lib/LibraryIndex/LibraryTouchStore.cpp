#include "LibraryTouchStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <limits>

#include "LibraryBuilder.h"
#include "LibraryFormat.h"

namespace library::touches {
namespace {
constexpr char ROOT[] = "/.crosspoint/library-touches";
constexpr char COUNTER[] = "/.crosspoint/library-touches/counter";
constexpr uint32_t LEGACY_MAGIC = 0x31544350;  // PCT1
constexpr uint32_t MAGIC = 0x32544350;         // PCT2
struct Record {
  uint32_t magic;
  uint32_t sequence;
  uint32_t nonce;
  uint32_t timestamp;
  uint32_t check;
};
static_assert(sizeof(Record) == 20);
enum class Read { Missing, Ok, Error };

uint32_t checksum(const Record& r, size_t bytes = offsetof(Record, check)) {
  return static_cast<uint32_t>(clixPathHash(reinterpret_cast<const char*>(&r), bytes));
}
void pathFor(const char* book, char (&path)[96]) {
  char hex[17];
  snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(clixPathHash(book, strlen(book))));
  snprintf(path, sizeof(path), "%s/%c/%c/%s", ROOT, hex[0], hex[1], hex + 2);
}
Read readOne(const char* path, Record& out) {
  HalFile file = Storage.open(path);
  if (!file) return Storage.exists(path) ? Read::Error : Read::Missing;
  const auto size = file.size();
  constexpr size_t legacySize = 16;
  if ((size != legacySize && size != sizeof(out)) || file.read(&out, size) != static_cast<int>(size))
    return Read::Error;
  if (size == legacySize) {
    // The last word in PCT1 was the checksum. It carried no wall-clock time.
    if (out.magic != LEGACY_MAGIC || out.timestamp != checksum(out, 12)) return Read::Error;
    out.timestamp = 0;
    return Read::Ok;
  }
  return out.magic == MAGIC && out.check == checksum(out) ? Read::Ok : Read::Error;
}
Read read(const char* path, Record& out) {
  const auto result = readOne(path, out);
  if (result != Read::Missing) return result;
  // A fully written temp is newer than the backup if power failed during rename.
  char alternate[104];
  snprintf(alternate, sizeof(alternate), "%s.tmp", path);
  const auto staged = readOne(alternate, out);
  if (staged == Read::Ok) return Storage.rename(alternate, path) ? Read::Ok : Read::Error;
  snprintf(alternate, sizeof(alternate), "%s.bak", path);
  const auto backup = readOne(alternate, out);
  if (backup == Read::Ok) return Storage.rename(alternate, path) ? Read::Ok : Read::Error;
  return backup == Read::Missing && staged == Read::Error ? Read::Error : backup;
}
bool write(const char* path, Record value) {
  char parent[96];
  const char* slash = strrchr(path, '/');
  snprintf(parent, sizeof(parent), "%.*s", static_cast<int>(slash - path), path);
  if (!Storage.exists(parent) && !Storage.mkdir(parent)) return false;
  value.magic = MAGIC;
  value.check = checksum(value);
  char tmp[104], bak[104];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  snprintf(bak, sizeof(bak), "%s.bak", path);
  HalFile file = Storage.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  const bool written = file.write(&value, sizeof(value)) == sizeof(value);
  const bool closed = file.close();
  if (!written || !closed) {
    Storage.remove(tmp);
    return false;
  }
  if (Storage.exists(path)) {
    if (Storage.exists(bak) && !Storage.remove(bak)) return false;
    if (!Storage.rename(path, bak)) return false;
  }
  return Storage.rename(tmp, path);
}
}  // namespace

bool initialize(const char* const* newestFirst, size_t count) {
  if (!Storage.exists(ROOT) && !Storage.mkdir(ROOT)) return false;
  Record counter{};
  const auto found = read(COUNTER, counter);
  if (found == Read::Ok) return true;
  if (found == Read::Error) return false;
  for (size_t i = count; i > 0; --i) {
    if (!Storage.exists(newestFirst[i - 1])) continue;
    char path[96];
    pathFor(newestFirst[i - 1], path);
    if (!write(path, {MAGIC, ++counter.sequence, 0, 0, 0})) return false;
  }
  return write(COUNTER, {MAGIC, counter.sequence, 0, 0, 0});
}

bool revision(uint32_t& sequence) {
  Record value{};
  if (read(COUNTER, value) != Read::Ok) return false;
  sequence = value.sequence;
  return true;
}
bool lookup(const char* book, uint32_t& sequence) {
  sequence = 0;
  char path[96];
  pathFor(book, path);
  Record value{};
  switch (read(path, value)) {
    case Read::Missing:
      return true;
    case Read::Error:
      return false;
    case Read::Ok:
      sequence = value.sequence;
      return true;
  }
  return false;
}

bool readTimestamp(const char* book, uint32_t& timestamp) {
  char path[96];
  pathFor(book, path);
  Record value{};
  // Rendering only reads the published record. Index reconciliation owns recovery.
  const auto result = readOne(path, value);
  timestamp = result == Read::Ok ? value.timestamp : 0;
  return result != Read::Error;
}

bool record(const char* book, const uint32_t nonce, const uint32_t timestamp) {
  if (!Storage.exists(book)) return false;
  Record counter{}, previous{};
  if (read(COUNTER, counter) != Read::Ok || counter.sequence == std::numeric_limits<uint32_t>::max()) return false;
  char path[96];
  pathFor(book, path);
  if (read(path, previous) == Read::Error || previous.sequence > counter.sequence) return false;
  if (nonce != 0 && nonce <= previous.nonce) {
    markLibraryIndexDirty();
    return true;
  }
  ++counter.sequence;
  // Reserve the sequence before publishing the book: interruption can leave a gap, never reuse it.
  if (!write(COUNTER, {MAGIC, counter.sequence, 0, 0, 0}) ||
      !write(path, {MAGIC, counter.sequence, nonce == 0 ? previous.nonce : nonce, timestamp, 0}))
    return false;
  markLibraryIndexDirty();
  return true;
}

bool canMove(const char* oldBook, const char* newBook) {
  if (strcmp(oldBook, newBook) == 0) return true;
  char oldPath[96], newPath[96];
  pathFor(oldBook, oldPath);
  pathFor(newBook, newPath);
  Record value{};
  return read(oldPath, value) != Read::Error && read(newPath, value) == Read::Missing;
}
bool move(const char* oldBook, const char* newBook) {
  if (strcmp(oldBook, newBook) == 0) return true;
  char oldPath[96], newPath[96];
  pathFor(oldBook, oldPath);
  pathFor(newBook, newPath);
  Record value{}, target{};
  const auto found = read(oldPath, value);
  if (found == Read::Missing) return true;
  if (found == Read::Error) return false;
  const auto destination = read(newPath, target);
  if (destination == Read::Error ||
      (destination == Read::Ok &&
       (target.sequence != value.sequence || target.nonce != value.nonce || target.timestamp != value.timestamp)))
    return false;
  if (destination == Read::Missing && !write(newPath, value)) return false;
  remove(oldBook);
  markLibraryIndexDirty();
  return true;
}
void remove(const char* book) {
  char path[96], alternate[104];
  pathFor(book, path);
  Storage.remove(path);
  for (const char* suffix : {".tmp", ".bak"}) {
    snprintf(alternate, sizeof(alternate), "%s%s", path, suffix);
    Storage.remove(alternate);
  }
}
}  // namespace library::touches
