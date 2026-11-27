#pragma once
#include <cstdint>

namespace phonesync::firmware_stage {
inline constexpr char kImagePath[] = "/.crosspoint/paperboy-firmware.bin";
inline constexpr char kIdentityPath[] = "/.crosspoint/paperboy-firmware.id";
struct Identity {
  uint32_t size;
  uint32_t crc;
  uint8_t sha256[32];
};
// Recover a durable checkpoint, discarding any uncheckpointed tail. The phone verifies the prefix CRC too.
bool prepare(const Identity& identity, uint32_t& offset, uint32_t& prefixCrc);
bool checkpoint(uint32_t offset, uint32_t prefixCrc);
bool verify(uint32_t size, uint32_t expectedCrc, void (*yield)() = nullptr);
bool discard();
bool hasIdentity();
}  // namespace phonesync::firmware_stage
