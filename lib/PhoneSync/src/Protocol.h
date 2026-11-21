#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Wire format of the phone sync protocol (docs/paperboy-protocol.md). Pure: built for the device and the host tests.
namespace phonesync::proto {

inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kIdBytes = 8;
inline constexpr uint32_t kProgressStep = 4096;
inline constexpr uint32_t kWindowBytes = 8192;
inline constexpr uint32_t kMaxArticleBytes = 8u * 1024u * 1024u;

enum Type : uint8_t {
  Hello = 0x01,
  List = 0x02,
  PutBegin = 0x10,
  PutData = 0x11,
  PutEnd = 0x12,
  Delete = 0x20,
  Bye = 0x30,
  HelloAck = 0x81,
  ListItems = 0x82,
  ListEnd = 0x83,
  PutReady = 0x90,
  PutProgress = 0x91,
  PutResult = 0x92,
  DeleteResult = 0xA0,
};

enum Result : uint8_t {
  Ok = 0,
  Busy = 1,
  NoSpace = 2,
  StorageError = 3,
  ChecksumMismatch = 4,
  BadRequest = 5,
  Exists = 6,
};

inline uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}
inline void writeU32(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
inline void writeU16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

// zlib CRC-32, continued from a previous value (start with 0).
inline uint32_t crc32(uint32_t crc, const uint8_t* data, size_t len) {
  static constexpr uint32_t table[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                         0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                         0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  crc = ~crc;
  while (len--) {
    crc ^= *data++;
    crc = table[crc & 0x0F] ^ (crc >> 4);
    crc = table[crc & 0x0F] ^ (crc >> 4);
  }
  return ~crc;
}

inline void idToHex(const uint8_t id[kIdBytes], char out[kIdBytes * 2 + 1]) {
  static constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < kIdBytes; i++) {
    out[i * 2] = digits[id[i] >> 4];
    out[i * 2 + 1] = digits[id[i] & 0x0F];
  }
  out[kIdBytes * 2] = '\0';
}

inline bool hexToId(const char* hex, uint8_t id[kIdBytes]) {
  auto nibble = [](const char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (size_t i = 0; i < kIdBytes; i++) {
    const int hi = nibble(hex[i * 2]);
    if (hi < 0) return false;
    const int lo = nibble(hex[i * 2 + 1]);
    if (lo < 0) return false;
    id[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

// PUT_BEGIN after its type byte. The strings point into the frame and are not terminated.
struct PutBeginFields {
  const uint8_t* id;
  uint32_t size;
  uint32_t crc;
  uint32_t time;
  const char* source;
  uint8_t sourceLen;
  const char* title;
  uint8_t titleLen;
  const char* author;
  uint8_t authorLen;
};

inline bool parsePutBegin(const uint8_t* p, size_t len, PutBeginFields& out) {
  if (len < kIdBytes + 12) return false;
  out.id = p;
  out.size = readU32(p + 8);
  out.crc = readU32(p + 12);
  out.time = readU32(p + 16);
  size_t at = 20;
  auto text = [&](const char*& s, uint8_t& n) {
    if (at >= len) return false;
    n = p[at++];
    if (at + n > len) return false;
    s = reinterpret_cast<const char*>(p + at);
    at += n;
    return true;
  };
  return text(out.source, out.sourceLen) && text(out.title, out.titleLen) && text(out.author, out.authorLen) &&
         at == len;
}

}  // namespace phonesync::proto
