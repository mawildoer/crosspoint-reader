#include <HalStorage.h>

#include <cassert>

#include "FirmwareStage.h"
#include "Protocol.h"

void checkFirmwareStage() {
  using namespace phonesync::firmware_stage;
  files.clear();
  readCount = writeCount = 0;
  failReadAt = shortWriteAt = 0;
  std::string image(10000, 'A');
  image[0] = static_cast<char>(0xe9);
  Identity identity{static_cast<uint32_t>(image.size()),
                    phonesync::proto::crc32(0, reinterpret_cast<const uint8_t*>(image.data()), image.size()),
                    {1}};
  uint32_t offset, crc;
  assert(prepare(identity, offset, crc) && offset == 0 && crc == 0);
  {
    HalFile file = Storage.open(kImagePath, O_WRONLY | O_APPEND);
    assert(file.write(image.data(), 3077) == 3077);
  }
  assert(checkpoint(3077, phonesync::proto::crc32(0, reinterpret_cast<const uint8_t*>(image.data()), 3077)));
  // A new session has no in-memory cursor; only the closed SD file and identity survive.
  assert(prepare(identity, offset, crc) && offset == 3077);
  assert(crc == phonesync::proto::crc32(0, reinterpret_cast<const uint8_t*>(image.data()), offset));
  {
    HalFile file = Storage.open(kImagePath, O_WRONLY | O_APPEND);
    assert(file.write(image.data() + offset, image.size() - offset) == image.size() - offset);
  }
  assert(checkpoint(image.size(), identity.crc));
  assert(prepare(identity, offset, crc) && offset == image.size() && crc == identity.crc);
  assert(verify(identity.size, identity.crc));
  files[kImagePath][100] = 'B';
  assert(prepare(identity, offset, crc) && offset == image.size());
  assert(!verify(identity.size, identity.crc));
  const auto saved = files[kImagePath];
  failReadAt = readCount + 1;
  assert(!prepare(identity, offset, crc) && files[kImagePath] == saved);
  failReadAt = 0;
  identity.sha256[1] = 2;
  assert(prepare(identity, offset, crc) && offset == 0 && files[kImagePath].empty());
  files[kImagePath] = image + "oversized";
  assert(prepare(identity, offset, crc) && offset == 0 && files[kImagePath].empty());
  files[kIdentityPath][0] ^= 1;
  files[kImagePath] = image.substr(0, 300);
  assert(prepare(identity, offset, crc) && offset == 0 && files[kImagePath].empty());
  assert(discard());
  shortWriteAt = writeCount + 1;
  assert(!prepare(identity, offset, crc));
  shortWriteAt = 0;
  assert(prepare(identity, offset, crc) && offset == 0);
  assert(discard() && !hasIdentity() && !Storage.exists(kImagePath));
  assert(prepare(identity, offset, crc));
  files[kImagePath] = image.substr(0, 500);
  assert(prepare(identity, offset, crc) && offset == 0 && files[kImagePath].empty());
  files[kImagePath] = image.substr(0, 500);
  const auto prefix = phonesync::proto::crc32(0, reinterpret_cast<const uint8_t*>(image.data()), 500);
  failRenameAt = renameCount + 2;
  assert(!checkpoint(500, prefix));
  failRenameAt = 0;
  assert(prepare(identity, offset, crc) && offset == 500 && crc == prefix);
  shortWriteAt = writeCount + 1;
  assert(!checkpoint(500, prefix));
  shortWriteAt = 0;
  assert(prepare(identity, offset, crc) && offset == 500 && crc == prefix);
}
