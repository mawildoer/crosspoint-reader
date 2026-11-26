#pragma once
#include <cstdint>

namespace phonesync {
class FirmwareTransferState {
 public:
  enum class Stage : uint8_t { Idle, Receiving, Validated, Committed };
  bool busy() const { return stage != Stage::Idle; }
  bool validated() const { return stage == Stage::Validated; }
  bool committed() const { return stage == Stage::Committed; }
  void begin() { stage = Stage::Receiving; }
  void validate(bool ok) { stage = ok ? Stage::Validated : Stage::Idle; }
  void commit(uint32_t now) {
    stage = Stage::Committed;
    committedAt = now;
  }
  bool installDue(uint32_t now) const { return committed() && static_cast<uint32_t>(now - committedAt) >= 400; }
  void clear() { stage = Stage::Idle; }

 private:
  Stage stage = Stage::Idle;
  uint32_t committedAt = 0;
};
}  // namespace phonesync
