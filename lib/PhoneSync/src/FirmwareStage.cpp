#include "FirmwareStage.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstring>

#include "Protocol.h"

namespace phonesync::firmware_stage {
namespace {
constexpr uint32_t MAGIC = 0x31465250;  // PRF1
constexpr char TEMP[] = "/.crosspoint/paperboy-firmware.id.tmp";
constexpr char BACKUP[] = "/.crosspoint/paperboy-firmware.id.bak";
struct Header {
  uint32_t magic;
  Identity identity;
  uint32_t offset;
  uint32_t prefixCrc;
  uint32_t check;
};
static_assert(sizeof(Header) == 56);
enum class Read { Missing, Valid, Invalid, Error };

uint32_t checksum(const Header& header) {
  return proto::crc32(0, reinterpret_cast<const uint8_t*>(&header), sizeof(header) - sizeof(header.check));
}
Read readOne(const char* path, Header& header) {
  HalFile file = Storage.open(path);
  if (!file) return Storage.exists(path) ? Read::Error : Read::Missing;
  if (file.size() != sizeof(header)) return Read::Invalid;
  if (file.read(&header, sizeof(header)) != sizeof(header)) return Read::Error;
  return header.magic == MAGIC && header.offset <= header.identity.size && header.check == checksum(header)
             ? Read::Valid
             : Read::Invalid;
}
Read read(Header& header) {
  const auto primary = readOne(kIdentityPath, header);
  if (primary == Read::Valid || primary == Read::Error) return primary;
  for (const char* candidate : {TEMP, BACKUP}) {
    const auto found = readOne(candidate, header);
    if (found == Read::Error) return found;
    if (found != Read::Valid) continue;
    if (Storage.exists(kIdentityPath) && !Storage.remove(kIdentityPath)) return Read::Error;
    return Storage.rename(candidate, kIdentityPath) ? Read::Valid : Read::Error;
  }
  return primary;
}
bool write(Header header) {
  header.check = checksum(header);
  HalFile file = Storage.open(TEMP, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  const bool written = file.write(&header, sizeof(header)) == sizeof(header);
  const bool closed = file.close();
  if (!written || !closed) {
    Storage.remove(TEMP);
    return false;
  }
  if (Storage.exists(kIdentityPath)) {
    if (Storage.exists(BACKUP) && !Storage.remove(BACKUP)) return false;
    if (!Storage.rename(kIdentityPath, BACKUP)) return false;
  }
  return Storage.rename(TEMP, kIdentityPath);
}
bool reset(const Identity& identity) {
  if (!discard()) return false;
  if (!Storage.exists("/.crosspoint") && !Storage.mkdir("/.crosspoint")) return false;
  {
    HalFile file = Storage.open(kImagePath, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file || !file.close()) return false;
  }
  return write({MAGIC, identity, 0, 0, 0});
}
}  // namespace

bool hasIdentity() { return Storage.exists(kIdentityPath) || Storage.exists(TEMP) || Storage.exists(BACKUP); }

bool discard() {
  for (const char* path : {kImagePath, kIdentityPath, TEMP, BACKUP}) {
    if (Storage.exists(path) && !Storage.remove(path)) return false;
  }
  return true;
}

bool prepare(const Identity& identity, uint32_t& offset, uint32_t& prefixCrc) {
  offset = prefixCrc = 0;
  Header header{};
  const auto found = read(header);
  if (found == Read::Error) return false;
  if (found != Read::Valid || memcmp(&header.identity, &identity, sizeof(identity)) != 0 || !Storage.exists(kImagePath))
    return reset(identity);
  HalFile file = Storage.open(kImagePath, O_WRONLY);
  if (!file) return false;
  if (file.size() < header.offset || file.size() > identity.size) {
    file.close();
    return reset(identity);
  }
  // Power loss before checkpoint publication leaves a tail that the phone has not been promised.
  if (file.size() > header.offset && !file.truncate(header.offset)) return false;
  if (!file.close()) return false;
  offset = header.offset;
  prefixCrc = header.prefixCrc;
  return true;
}

bool checkpoint(const uint32_t offset, const uint32_t prefixCrc) {
  Header header{};
  if (read(header) != Read::Valid || offset > header.identity.size) return false;
  {
    HalFile file = Storage.open(kImagePath);
    if (!file || file.size() != offset) return false;
  }
  header.offset = offset;
  header.prefixCrc = prefixCrc;
  return write(header);
}

bool verify(const uint32_t size, const uint32_t expectedCrc, void (*yield)()) {
  HalFile file = Storage.open(kImagePath);
  if (!file || file.size() != size) return false;
  uint8_t bytes[128];
  uint32_t offset = 0, crc = 0;
  while (offset < size) {
    const size_t count = std::min<size_t>(sizeof(bytes), size - offset);
    if (file.read(bytes, count) != count) return false;
    crc = proto::crc32(crc, bytes, count);
    offset += count;
    if (yield && offset % 8192 == 0) yield();
  }
  return crc == expectedCrc;
}
}  // namespace phonesync::firmware_stage
