#include "LibraryFormat.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

namespace library {
uint32_t fatTimestampToUnix(const uint32_t packed) {
  if (packed == 0) return 0;
  tm date{};
  date.tm_year = static_cast<int>((packed >> 25) & 127) + 80;
  date.tm_mon = static_cast<int>((packed >> 21) & 15) - 1;
  date.tm_mday = (packed >> 16) & 31;
  date.tm_hour = (packed >> 11) & 31;
  date.tm_min = (packed >> 5) & 63;
  date.tm_sec = (packed & 31) * 2;
  date.tm_isdst = -1;
  if (date.tm_mon < 0 || date.tm_mon > 11 || date.tm_mday == 0 || date.tm_hour > 23 || date.tm_min > 59 ||
      date.tm_sec > 59)
    return 0;
  const tm original = date;
  const time_t epoch = mktime(&date);
  if (epoch < 0 || static_cast<uint64_t>(epoch) > std::numeric_limits<uint32_t>::max() ||
      date.tm_year != original.tm_year || date.tm_mon != original.tm_mon || date.tm_mday != original.tm_mday)
    return 0;
  return static_cast<uint32_t>(epoch);
}

namespace {
uint32_t timeContextTag() {
  const char* zone = std::getenv("TZ");
  uint32_t hash = 2166136261u;
  if (zone)
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(zone); *p; ++p) hash = (hash ^ *p) * 16777619u;
  return hash;
}
}  // namespace

void setTimeContext(ClixHeader& header) {
  const uint32_t tag = timeContextTag();
  memcpy(header.reserved, &tag, sizeof(tag));
}

bool timeContextMatches(const ClixHeader& header) {
  uint32_t tag;
  memcpy(&tag, header.reserved, sizeof(tag));
  return (header.flags & CLIX_FLAG_PUBLICATION_ORDER) && tag == timeContextTag();
}

const char* clixValidityName(const ClixValidity v) {
  switch (v) {
    case ClixValidity::Ok:
      return "ok";
    case ClixValidity::BadMagic:
      return "bad magic";
    case ClixValidity::UnknownFormatVersion:
      return "unknown format version";
    case ClixValidity::StaleFoldVersion:
      return "stale fold version";
    case ClixValidity::StaleTimeContext:
      return "stale timestamp timezone";
    case ClixValidity::SizeMismatch:
      return "size mismatch (truncated?)";
    case ClixValidity::CountOutOfRange:
      return "book count out of range";
    case ClixValidity::SectionsInconsistent:
      return "section offsets inconsistent";
  }
  return "unknown";
}

}  // namespace library
