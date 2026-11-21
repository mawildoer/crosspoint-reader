#include <HalStorage.h>

#include <cassert>
#include <iostream>

#include "ArticleStore.h"
#include "Protocol.h"
using namespace phonesync;
int main() {
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
  const auto* bytes = reinterpret_cast<const uint8_t*>("123456789");
  assert(proto::crc32(0, bytes, 9) == 0xcbf43926u);
  std::cout
      << "PASS: short append rollback; read-state lookup errors; index preservation on read failure; protocol CRC\n";
}
