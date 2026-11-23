#pragma once

#include <cstddef>
#include <cstdint>

// Phone sync: the device is a Bluetooth LE peripheral; the Paperboy phone app connects and
// pushes articles (small EPUBs) that are stored under /Articles. Protocol: DESIGN.md.
namespace phonesync {

// What the module may ask of the firmware.
struct Host {
  uint8_t (*batteryPercent)();
  // The radio needs a steady clock while it is up.
  void (*holdFullSpeed)(bool hold);
  // A button was pressed: ends the sync-before-sleep window early.
  bool (*buttonPressed)();
  const char* firmwareVersion;
};

// The radio runs on reading lists, not in the reader, which needs the heap.
enum class Where : uint8_t { Home, Articles, Library, Reader, Elsewhere };

struct Status {
  bool compiledIn;
  bool radioOn;
  bool linked;
  bool receiving;
  uint16_t receivedThisBoot;  // articles stored since boot
};

void begin(const Host& host);
// Once per main-loop pass. Starts and stops the radio and does the session's storage work.
void tick(Where where, bool storageBusy, bool wifiOn);
// Stops the radio now (before the reader opens, before WiFi starts). False if it did not stop in time.
bool stop(uint32_t timeoutMs);
// Blocks: keeps the radio up for a short window before deep sleep so the phone can deliver.
void syncBeforeSleep();
// Removes every bond, so another phone can pair.
void forgetPhone();
Status status();
// Changes whenever the article store changes; list screens reload on a new value.
uint32_t storeGeneration();

}  // namespace phonesync
