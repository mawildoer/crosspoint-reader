#include <HalStorage.h>
#include <LibraryFormat.h>
#include <LibraryTouchStore.h>

#include <cassert>
namespace library {
bool markLibraryIndexDirty() { return true; }
}  // namespace library
namespace {
uint32_t timestamp(const char* path) {
  uint32_t seconds;
  assert(library::touches::readTimestamp(path, seconds));
  return seconds;
}
}  // namespace
void checkTouches() {
  using namespace library::touches;
  files.clear();
  failReadAt = 0;
  files["/older.epub"] = "original EPUB with saved progress";
  files["/newer.epub"] = "another EPUB";
  const char* paths[] = {"/newer.epub", "/older.epub"};
  assert(initialize(paths, 2));
  // Simulate an existing card with checked PCT1 records and a pending old phone nonce.
  for (auto& [path, bytes] : files) {
    if (!path.starts_with("/.crosspoint/library-touches/") || bytes.size() != 20) continue;
    uint32_t legacy[4] = {0x31544350, 0, 0, 0};
    memcpy(&legacy[1], bytes.data() + 4, 8);
    if (legacy[1] == 1) legacy[2] = 8;
    legacy[3] = static_cast<uint32_t>(library::clixPathHash(reinterpret_cast<const char*>(legacy), 12));
    bytes.assign(reinterpret_cast<const char*>(legacy), sizeof(legacy));
  }
  assert(timestamp(paths[1]) == 0);
  assert(record(paths[1], 8, 1800000000));
  assert(timestamp(paths[1]) == 0);
  uint32_t old, fresh, revisionBefore;
  assert(lookup(paths[1], old) && old == 1);
  assert(lookup(paths[0], fresh) && fresh == 2);
  assert(record(paths[1], 10, 1800000001));
  assert(timestamp(paths[1]) == 1800000001);
  assert(lookup(paths[1], old) && old == 3);
  assert(record(paths[0], 0, 1800000002));
  assert(timestamp(paths[0]) == 1800000002);
  assert(lookup(paths[0], fresh) && fresh == 4);
  assert(record(paths[1], 10));
  assert(lookup(paths[1], old) && old == 3);  // Lost ACK must not undo a later open.
  assert(record(paths[1], 11, 1800000003));
  assert(lookup(paths[1], old) && old == 5);
  assert(initialize(paths, 2));
  assert(lookup(paths[1], old) && old == 5);
  assert(record(paths[1], 0, 1800000060));
  assert(record(paths[1], 11, 1800000120));
  assert(timestamp(paths[1]) == 1800000060);
  assert(lookup(paths[1], old) && old == 6);
  assert(files["/older.epub"] == "original EPUB with saved progress");
  assert(revision(revisionBefore));
  shortWrite = true;
  assert(!record(paths[0], 12));
  assert(lookup(paths[0], fresh) && fresh == 4);
  uint32_t after;
  assert(revision(after) && after == revisionBefore);
  failRenameAt = renameCount + 2;
  assert(!record(paths[0], 12));
  failRenameAt = 0;
  assert(record(paths[0], 12));
  assert(lookup(paths[0], fresh) && fresh > old);
  // A failed final publication can leave a valid temp plus a backup.
  failRenameAt = renameCount + 4;  // Counter publishes; book primary->backup succeeds, temp->primary fails.
  assert(!record(paths[1], 21));
  failRenameAt = 0;
  assert(record(paths[1], 21));  // Recovery must secure the acknowledged record at its primary path.
  uint32_t acknowledged;
  assert(lookup(paths[1], acknowledged));
  shortWriteAt = writeCount + 2;  // Next counter succeeds; next book-record write is short.
  assert(!record(paths[1]));
  shortWriteAt = 0;
  assert(lookup(paths[1], after) && after == acknowledged);
  assert(record(paths[1], 21));
  assert(lookup(paths[1], after) && after == acknowledged);
  assert(move(paths[0], "/renamed.epub"));
  uint32_t renamed;
  assert(lookup("/renamed.epub", renamed) && renamed == fresh);
  assert(lookup(paths[0], after) && after == 0);
  library::touches::remove("/renamed.epub");
  assert(lookup("/renamed.epub", after) && after == 0);
  failReadAt = readCount + 1;
  assert(!record(paths[1], 20));
  failReadAt = 0;
  // Shards bound the directory populations for a 4096-book filename corpus.
  files.clear();
  assert(initialize(nullptr, 0));
  for (unsigned i = 0; i < 4096; ++i) {
    char book[40];
    snprintf(book, sizeof(book), "/books/book%04u.epub", i);
    files[book] = "book";
    assert(record(book));
  }
  std::map<std::string, unsigned> buckets;
  unsigned total = 0;
  const std::string root = "/.crosspoint/library-touches/";
  for (const auto& [path, value] : files) {
    if (!path.starts_with(root)) continue;
    const std::string suffix = path.substr(root.size());
    if (suffix.size() != 18 || suffix[1] != '/' || suffix[3] != '/') continue;
    ++buckets[suffix.substr(0, 3)];
    ++total;
  }
  assert(total == 4096 && buckets.size() <= 256);
  for (const auto& [bucket, count] : buckets) assert(count <= 128);
}
