#include <HalStorage.h>

#include <cassert>
#include <iostream>

#include "ArticleStore.h"
#include "LibraryRefresh.h"
#include "Protocol.h"
using namespace phonesync;
int main() {
  LibraryRefresh refresh;
  refresh.reset(1);
  refresh.observe(1, 0);
  assert(!refresh.due(2000));
  refresh.observe(2, 100);
  assert(!refresh.due(100));
  assert(!refresh.due(1599));
  assert(refresh.due(1600));
  refresh.observe(3, 1500);
  assert(!refresh.due(1600));
  assert(refresh.due(3000));
  refresh.reset(3);
  assert(!refresh.due(4000));
  refresh.observe(4, UINT32_MAX - 1000);
  assert(!refresh.due(498));
  assert(refresh.due(499));
  files[store::kIndexPath] = "0123456789abcdef\t1\tsource\ttitle\tauthor\n";
  const auto original = files[store::kIndexPath];
  shortWrite = true;
  assert(!store::append("fedcba9876543210", 2, "source", 6, "a new title", 11, "author", 6));
  assert(files[store::kIndexPath] == original);
  files[store::kReadPath] = "0123456789abcdef\n";
  readCount = 0;
  failReadAt = 1;
  assert(!store::clearRead("0123456789abcdef"));
  assert(files[store::kReadPath] == "0123456789abcdef\n");
  failReadAt = 0;
  readCount = 0;
  std::string many = "0123456789abcdef\t1\tsource\ttitle\tauthor\n";
  for (int i = 0; i < 20; i++) many += "fedcba9876543210\t2\tsource\tother\tauthor\n";
  files[store::kIndexPath] = many;
  files[store::pathFor("0123456789abcdef")] = "epub";
  failReadAt = 3;
  assert(!store::remove("0123456789abcdef"));
  assert(files[store::kIndexPath] == many);
  failReadAt = 0;
  readCount = 0;
  files[store::kIndexPath] = "0123456789abcdef\t1794744000\tResearch Journal\tThe chart story\tAda Writer\n";
  store::Entry entry{};
  assert(store::lookup("0123456789abcdef", entry));
  assert(entry.title == "The chart story" && entry.author == "Ada Writer" && entry.source == "Research Journal");
  std::string detail;
  store::formatSubtitle(entry, detail);
  assert(detail == "Ada Writer · Research Journal");
  std::string date;
  store::formatDate(entry.time, date);
  assert(date == "2026-11-15");
  store::formatDate(0, date);
  assert(date.empty());
  assert(!store::lookup("fedcba9876543210", entry));
  store::formatSubtitle(entry, detail);
  assert(detail.empty());
  entry.source = "Journal";
  store::formatSubtitle(entry, detail);
  assert(detail == "Journal");
  readCount = 0;
  failReadAt = 1;
  assert(!store::lookup("0123456789abcdef", entry));
  failReadAt = 0;
  const auto* bytes = reinterpret_cast<const uint8_t*>("123456789");
  assert(proto::crc32(0, bytes, 9) == 0xcbf43926u);
  std::cout << "PASS: short append rollback; read-state lookup errors; index preservation on read failure; protocol "
               "CRC; article metadata and date formatting; library refresh debounce and clock wrap\n";
}
