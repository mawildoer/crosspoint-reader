#include <HalStorage.h>
#include <LibraryTouchStore.h>

#include <cassert>
namespace library {
bool markLibraryIndexDirty() { return true; }
}  // namespace library
void checkTouches() {
  using namespace library::touches;
  files.clear();
  failReadAt = 0;
  files["/older.epub"] = "original EPUB with saved progress";
  files["/newer.epub"] = "another EPUB";
  const char* paths[] = {"/newer.epub", "/older.epub"};
  assert(initialize(paths, 2));
  uint32_t old, fresh, revisionBefore;
  assert(lookup(paths[1], old) && old == 1);
  assert(lookup(paths[0], fresh) && fresh == 2);
  assert(record(paths[1], 10));
  assert(lookup(paths[1], old) && old == 3);
  assert(record(paths[0]));
  assert(lookup(paths[0], fresh) && fresh == 4);
  assert(record(paths[1], 10));
  assert(lookup(paths[1], old) && old == 3);  // Lost ACK must not undo a later open.
  assert(record(paths[1], 11));
  assert(lookup(paths[1], old) && old == 5);
  assert(initialize(paths, 2));
  assert(lookup(paths[1], old) && old == 5);
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
