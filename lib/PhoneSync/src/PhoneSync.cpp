#include "PhoneSync.h"

#if defined(PHONESYNC_BLE) && PHONESYNC_BLE

#include <HalMemory.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <NimBLEDevice.h>
#include <esp_attr.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "ArticleStore.h"
#include "Protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

namespace phonesync {
namespace {

using namespace proto;

constexpr char kServiceUuid[] = "8E400001-7B2A-4B1F-9C5D-5A1EF0C0DE01";
constexpr char kRxUuid[] = "8E400002-7B2A-4B1F-9C5D-5A1EF0C0DE01";
constexpr char kTxUuid[] = "8E400003-7B2A-4B1F-9C5D-5A1EF0C0DE01";
constexpr char kDeviceName[] = "Paperboy X3";

// Frames wait here between the Bluetooth task (which receives them) and the main loop (which owns
// the SD card). Sized for the flow-control window plus the per-frame header at the smallest MTU.
constexpr size_t kQueueBytes = 12288;
constexpr size_t kMaxFrame = 512;
// The stack took about 50 KB in every X3 log of the page turner (RadioPolicy.h); the queue and
// the reader's 32 KiB block come on top.
constexpr size_t kStartFreeBytes = 49152 + kQueueBytes + 32768;
constexpr size_t kStartLargestBlock = 32768;
constexpr uint32_t kIdleOffMs = 60u * 1000u;
constexpr uint32_t kByeLingerMs = 400;
constexpr uint32_t kSleepConnectMs = 8000;
constexpr uint32_t kSleepSessionMs = 90u * 1000u;
constexpr uint32_t kSleepQuietMs = 15u * 1000u;

Host host{};
bool begun = false;
bool radioOn = false;
bool idleStopped = false;
Where lastWhere = Where::Elsewhere;
uint32_t radioUpAtMs = 0;
uint32_t lastLinkMs = 0;
uint32_t byeAtMs = 0;
uint32_t lastFrameMs = 0;
uint32_t generation = 1;
uint16_t receivedThisBoot = 0;

std::atomic<bool> linked{false};
std::atomic<bool> linkDropped{false};
std::atomic<uint16_t> connHandle{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint16_t> mtu{23};
std::atomic<uint32_t> droppedFrames{0};
std::atomic<int> bondsAtConnect{0};
// A new device may pair while the Articles screen is in front (someone is holding the reader).
std::atomic<bool> pairingOpen{false};

MessageBufferHandle_t queue = nullptr;
NimBLECharacteristic* txChar = nullptr;

// One transfer in progress.
struct Incoming {
  bool active = false;
  HalFile file;
  char idHex[store::kIdHexLen + 1] = {};
  uint32_t size = 0;
  uint32_t expectedCrc = 0;
  uint32_t crc = 0;
  uint32_t received = 0;
  uint32_t acked = 0;
  uint32_t time = 0;
  bool failed = false;
  std::string source;
  std::string title;
  std::string author;
};
Incoming incoming;

uint32_t nowMs() { return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS); }

void send(const uint8_t* frame, const size_t len) {
  if (!txChar || !linked.load()) return;
  // A notification can fail while the stack is out of buffers; they come back within milliseconds.
  for (int attempt = 0; attempt < 20; attempt++) {
    if (txChar->notify(frame, len, connHandle.load())) return;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  LOG_ERR("SYNC", "notify failed, type 0x%02x", frame[0]);
}

void sendStatus(const uint8_t type, const uint8_t status) {
  const uint8_t frame[2] = {type, status};
  send(frame, sizeof(frame));
}

void sendProgress() {
  uint8_t frame[5] = {PutProgress};
  writeU32(frame + 1, incoming.received);
  send(frame, sizeof(frame));
  incoming.acked = incoming.received;
}

void abandonIncoming() {
  if (!incoming.active) return;
  incoming.file.close();
  incoming.active = false;
  Storage.remove(store::kIncomingPath);
}

// Counting free clusters took 5.9 s on a 16 GB card (X3, 15/11/2026), and it froze the buttons and
// the screen for that long after every wake while it ran on the main loop after a session
// (reported 18/11/2026). It now runs only inside the sync-before-sleep window, where nobody is
// waiting, and only when the card's contents changed. The figure is kept in RTC memory, which
// outlives deep sleep, and reported in the next session.
constexpr uint32_t kFreeUnknown = 0xFFFFFFFFu;
constexpr uint32_t kFreeMemoMagic = 0x50424631u;
struct FreeMemo {
  uint32_t magic;
  uint32_t freeMb;
};
RTC_NOINIT_ATTR FreeMemo freeMemo;
bool freeSpaceStale = false;

uint32_t knownFreeMb() { return freeMemo.magic == kFreeMemoMagic ? freeMemo.freeMb : kFreeUnknown; }

void measureFreeSpace() {
  uint64_t used = 0;
  const uint64_t total = Storage.sdTotalBytes();
  if (total > 0 && Storage.sdUsedBytes(used) && used <= total) {
    freeMemo.freeMb = static_cast<uint32_t>((total - used) / (1024u * 1024u));
    freeMemo.magic = kFreeMemoMagic;
    freeSpaceStale = false;
  }
}

void onHello(const uint8_t* payload, const size_t len) {
  if (len < 1 || payload[0] != kVersion) LOG_INF("SYNC", "phone speaks protocol %u", len ? payload[0] : 0);
  uint8_t frame[64] = {HelloAck, kVersion};
  frame[2] = host.batteryPercent ? host.batteryPercent() : 0;
  writeU32(frame + 3, knownFreeMb());
  const char* version = host.firmwareVersion ? host.firmwareVersion : "";
  const size_t versionLen = std::min(strlen(version), sizeof(frame) - 7);
  memcpy(frame + 7, version, versionLen);
  send(frame, 7 + versionLen);
}

void onList() {
  std::vector<uint64_t> readIds;
  store::loadReadIds(readIds);
  struct Ctx {
    const std::vector<uint64_t>* readIds;
    uint8_t frame[kMaxFrame];
    size_t used;
    size_t limit;
    uint16_t total;
  };
  auto ctx = makeUniqueNoThrow<Ctx>();
  if (!ctx) {
    sendStatus(ListEnd, 0);
    return;
  }
  ctx->readIds = &readIds;
  ctx->frame[0] = ListItems;
  ctx->used = 1;
  ctx->limit = std::min<size_t>(mtu.load() - 3, kMaxFrame);
  ctx->total = 0;
  store::forEach(
      [](void* c, const char* idHex, uint32_t, const char*, const char*, const char*) {
        auto* ctx = static_cast<Ctx*>(c);
        if (ctx->used + kIdBytes + 1 > ctx->limit) {
          send(ctx->frame, ctx->used);
          ctx->used = 1;
        }
        if (!hexToId(idHex, ctx->frame + ctx->used)) return true;
        const uint64_t value = store::idValue(idHex);
        const bool read = std::find(ctx->readIds->begin(), ctx->readIds->end(), value) != ctx->readIds->end();
        ctx->frame[ctx->used + kIdBytes] = read ? 1 : 0;
        ctx->used += kIdBytes + 1;
        ctx->total++;
        return true;
      },
      ctx.get());
  if (ctx->used > 1) send(ctx->frame, ctx->used);
  uint8_t end[3] = {ListEnd};
  writeU16(end + 1, ctx->total);
  send(end, sizeof(end));
}

void onPutBegin(const uint8_t* payload, const size_t len) {
  abandonIncoming();
  PutBeginFields fields{};
  if (!parsePutBegin(payload, len, fields) || fields.size == 0 || fields.size > kMaxArticleBytes) {
    sendStatus(PutReady, BadRequest);
    return;
  }
  idToHex(fields.id, incoming.idHex);
  if (store::contains(incoming.idHex) && Storage.exists(store::pathFor(incoming.idHex).c_str())) {
    sendStatus(PutReady, Exists);
    return;
  }
  if (!store::ensureDir()) {
    sendStatus(PutReady, StorageError);
    return;
  }
  incoming.file = Storage.open(store::kIncomingPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!incoming.file) {
    sendStatus(PutReady, StorageError);
    return;
  }
  incoming.active = true;
  incoming.failed = false;
  incoming.size = fields.size;
  incoming.expectedCrc = fields.crc;
  incoming.crc = 0;
  incoming.received = 0;
  incoming.acked = 0;
  incoming.time = fields.time;
  incoming.source.assign(fields.source, fields.sourceLen);
  incoming.title.assign(fields.title, fields.titleLen);
  incoming.author.assign(fields.author, fields.authorLen);
  LOG_INF("SYNC", "receiving %s (%lu B)", incoming.idHex, static_cast<unsigned long>(incoming.size));
  sendStatus(PutReady, Ok);
}

void onPutData(const uint8_t* payload, const size_t len) {
  if (!incoming.active) return;
  if (len > incoming.size - incoming.received) {
    abandonIncoming();
    sendStatus(PutResult, BadRequest);
    return;
  }
  if (!incoming.failed && incoming.file.write(payload, len) != len) incoming.failed = true;
  incoming.crc = crc32(incoming.crc, payload, len);
  incoming.received += len;
  if (incoming.received - incoming.acked >= kProgressStep || incoming.received == incoming.size) sendProgress();
}

void onPutEnd() {
  if (!incoming.active) {
    sendStatus(PutResult, BadRequest);
    return;
  }
  incoming.file.close();
  incoming.active = false;
  uint8_t status = Ok;
  if (incoming.failed) {
    status = StorageError;
  } else if (incoming.received != incoming.size || incoming.crc != incoming.expectedCrc) {
    status = ChecksumMismatch;
  } else {
    // An article that arrives again replaces the old copy and its index line, and is unread.
    const std::string path = store::pathFor(incoming.idHex);
    if (!store::remove(incoming.idHex) || !store::clearRead(incoming.idHex) ||
        !Storage.rename(store::kIncomingPath, path.c_str())) {
      status = StorageError;
    } else if (!store::append(incoming.idHex, incoming.time, incoming.source.data(), incoming.source.size(),
                              incoming.title.data(), incoming.title.size(), incoming.author.data(),
                              incoming.author.size())) {
      Storage.remove(path.c_str());
      status = StorageError;
    }
  }
  if (status == Ok) {
    receivedThisBoot++;
    generation++;
    library::markLibraryIndexDirty();
    freeSpaceStale = true;
  } else {
    Storage.remove(store::kIncomingPath);
    LOG_ERR("SYNC", "%s failed: status %u, %lu of %lu B, dropped frames %lu", incoming.idHex, status,
            static_cast<unsigned long>(incoming.received), static_cast<unsigned long>(incoming.size),
            static_cast<unsigned long>(droppedFrames.load()));
  }
  sendStatus(PutResult, status);
}

void onDelete(const uint8_t* payload, const size_t len) {
  if (len != kIdBytes) {
    sendStatus(DeleteResult, BadRequest);
    return;
  }
  char idHex[store::kIdHexLen + 1];
  idToHex(payload, idHex);
  const bool ok = store::remove(idHex);
  if (ok) {
    generation++;
    library::markLibraryIndexDirty();
    freeSpaceStale = true;
  }
  sendStatus(DeleteResult, ok ? Ok : StorageError);
}

void handleFrame(const uint8_t* frame, const size_t len) {
  const uint8_t* payload = frame + 1;
  const size_t payloadLen = len - 1;
  switch (frame[0]) {
    case Hello:
      onHello(payload, payloadLen);
      break;
    case List:
      onList();
      break;
    case PutBegin:
      onPutBegin(payload, payloadLen);
      break;
    case PutData:
      onPutData(payload, payloadLen);
      break;
    case PutEnd:
      onPutEnd();
      break;
    case Delete:
      onDelete(payload, payloadLen);
      break;
    case Bye:
      byeAtMs = nowMs() | 1u;
      break;
    default:
      LOG_DBG("SYNC", "unknown frame 0x%02x", frame[0]);
      break;
  }
}

// --- Bluetooth callbacks: these run on the Bluetooth task and only pass things on. ---

class ServerEvents final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    connHandle.store(info.getConnHandle());
    mtu.store(23);
    bondsAtConnect.store(NimBLEDevice::getNumBonds());
    linked.store(true);
    LOG_INF("SYNC", "phone connected (bonds %d)", NimBLEDevice::getNumBonds());
    // 15 to 30 ms: a transfer is short, so favour speed over the battery.
    server->updateConnParams(info.getConnHandle(), 12, 24, 0, 400);
    // Ask for an encrypted link now. An iPhone that has not paired did not start pairing on its own
    // after a refused write (16/11/2026); a request from the reader brings up its pairing prompt,
    // and a paired phone just turns encryption on.
    NimBLEDevice::startSecurity(info.getConnHandle());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    LOG_INF("SYNC", "phone disconnected (reason 0x%x)", reason);
    linked.store(false);
    linkDropped.store(true);
    connHandle.store(BLE_HS_CONN_HANDLE_NONE);
  }
  void onMTUChange(const uint16_t value, NimBLEConnInfo&) override { mtu.store(value); }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    LOG_INF("SYNC", "security: encrypted %d bonded %d", info.isEncrypted(), info.isBonded());
    // The first device pairs anywhere. After that a new one is turned away unless the Articles
    // screen is open, so a stranger nearby cannot pair with a reader lying on a table.
    if (info.isBonded() && bondsAtConnect.load() >= 1 && NimBLEDevice::getNumBonds() > bondsAtConnect.load() &&
        !pairingOpen.load()) {
      LOG_INF("SYNC", "new device refused: open Articles on the reader to pair it");
      NimBLEDevice::deleteBond(info.getIdAddress());
      NimBLEDevice::getServer()->disconnect(info.getConnHandle());
    }
  }
};

class RxEvents final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    if (!info.isEncrypted() || !queue) {
      LOG_INF("SYNC", "write refused: link not encrypted");
      return;
    }
    const NimBLEAttValue value = characteristic->getValue();
    if (value.size() == 0 || value.size() > kMaxFrame) return;
    if (xMessageBufferSend(queue, value.data(), value.size(), 0) != value.size()) droppedFrames.fetch_add(1);
  }
};

ServerEvents serverEvents;
RxEvents rxEvents;

void advertise() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->start();
}

bool startRadio() {
  const auto heap = HalMemory::getInternalHeap();
  if (heap.freeBytes < kStartFreeBytes || heap.largestBlockBytes < kStartLargestBlock) {
    LOG_INF("SYNC", "radio refused: free %u largest %u", static_cast<unsigned>(heap.freeBytes),
            static_cast<unsigned>(heap.largestBlockBytes));
    return false;
  }
  if (host.holdFullSpeed) host.holdFullSpeed(true);
  queue = xMessageBufferCreate(kQueueBytes);
  if (!queue || !NimBLEDevice::init(kDeviceName)) {
    LOG_ERR("SYNC", "radio start failed");
    if (queue) vMessageBufferDelete(queue);
    queue = nullptr;
    if (host.holdFullSpeed) host.holdFullSpeed(false);
    return false;
  }
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverEvents, false);
  server->advertiseOnDisconnect(true);
  NimBLEService* service = server->createService(kServiceUuid);
  NimBLECharacteristic* rx = service->createCharacteristic(
      kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC, kMaxFrame);
  rx->setCallbacks(&rxEvents);
  txChar = service->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC, kMaxFrame);
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->enableScanResponse(true);
  advertising->setName(kDeviceName);
  // 20 to 30 ms: the radio is up for a short window, and a fast advertiser is found quickly by a
  // phone whose app is in the background.
  advertising->setMinInterval(32);
  advertising->setMaxInterval(48);
  advertise();

  radioOn = true;
  radioUpAtMs = lastLinkMs = nowMs();
  byeAtMs = 0;
  const auto after = HalMemory::getInternalHeap();
  LOG_INF("SYNC", "radio up: free %u -> %u, largest %u", static_cast<unsigned>(heap.freeBytes),
          static_cast<unsigned>(after.freeBytes), static_cast<unsigned>(after.largestBlockBytes));
  return true;
}

void stopRadio() {
  if (!radioOn) return;
  abandonIncoming();
  NimBLEDevice::deinit(true);
  txChar = nullptr;
  linked.store(false);
  linkDropped.store(false);
  connHandle.store(BLE_HS_CONN_HANDLE_NONE);
  if (queue) vMessageBufferDelete(queue);
  queue = nullptr;
  radioOn = false;
  if (host.holdFullSpeed) host.holdFullSpeed(false);
  const auto heap = HalMemory::getInternalHeap();
  LOG_INF("SYNC", "radio down: free %u largest %u", static_cast<unsigned>(heap.freeBytes),
          static_cast<unsigned>(heap.largestBlockBytes));
}

// Handles every waiting frame. True when at least one was handled.
bool pump() {
  if (!radioOn) return false;
  if (linkDropped.exchange(false)) {
    abandonIncoming();
    // A phone that said BYE and left has finished: byeAtMs stays, and the radio stops.
    if (queue && byeAtMs == 0) xMessageBufferReset(queue);
  }
  static uint8_t frame[kMaxFrame];
  bool any = false;
  while (queue) {
    const size_t len = xMessageBufferReceive(queue, frame, sizeof(frame), 0);
    if (len == 0) break;
    handleFrame(frame, len);
    any = true;
  }
  const uint32_t now = nowMs();
  if (any) lastFrameMs = now;
  if (linked.load()) lastLinkMs = now;
  return any;
}

bool byeLingerOver() { return byeAtMs != 0 && nowMs() - byeAtMs >= kByeLingerMs; }

}  // namespace

void begin(const Host& h) {
  host = h;
  begun = true;
}

void tick(const Where where, const bool storageBusy, const bool wifiOn) {
  if (!begun) return;
  const bool syncScreen = where == Where::Home || where == Where::Articles || where == Where::Library;
  const bool wasSyncScreen = lastWhere == Where::Home || lastWhere == Where::Articles || lastWhere == Where::Library;
  // Coming back to Home or Articles asks again; so does moving between them.
  if (syncScreen && (!wasSyncScreen || where != lastWhere)) idleStopped = false;
  lastWhere = where;
  pairingOpen.store(where == Where::Articles);

  const bool allowed = syncScreen && !storageBusy && !wifiOn;
  if (!radioOn) {
    if (allowed && !idleStopped && !startRadio()) idleStopped = true;  // refused: wait for the next visit
    return;
  }
  if (!allowed) {
    stopRadio();
    return;
  }
  pump();
  if (byeLingerOver()) {
    byeAtMs = 0;
    // On Articles the link stays: an article loaded on the phone a moment later arrives at once.
    // The phone keeps the connection and starts another session when it has something.
    if ((where != Where::Articles && where != Where::Library) || !linked.load()) {
      // Elsewhere the phone is done. Stay off until the next visit, or it would reconnect in a loop.
      stopRadio();
      idleStopped = true;
      return;
    }
  }
  if (!linked.load() && nowMs() - lastLinkMs >= kIdleOffMs) {
    stopRadio();
    idleStopped = true;
  }
}

bool stop(uint32_t) {
  stopRadio();
  return true;
}

void syncBeforeSleep() {
  if (!begun) return;
  // Without a paired phone nothing can arrive: do not spend the battery on it.
  if (!Storage.exists(store::kDir)) return;
  if (!radioOn && !startRadio()) return;
  const uint32_t start = nowMs();
  lastFrameMs = start;
  while (true) {
    pump();
    const uint32_t now = nowMs();
    if (byeLingerOver()) break;
    if (host.buttonPressed && now - start > 500 && host.buttonPressed()) break;
    if (!linked.load() && now - start >= kSleepConnectMs) break;
    if (linked.load() && now - lastFrameMs >= kSleepQuietMs) break;
    if (now - start >= kSleepSessionMs) break;
    // The window outlasts the task watchdog when the main loop is subscribed to it.
    if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  stopRadio();
  // The sleep screen is up and nothing is waiting on the loop: the slow count can run here.
  if (freeSpaceStale || knownFreeMb() == kFreeUnknown) measureFreeSpace();
}

void forgetPhone() {
  const bool wasOn = radioOn;
  if (!radioOn && !startRadio()) return;
  NimBLEDevice::deleteAllBonds();
  if (!wasOn) stopRadio();
  idleStopped = false;
}

Status status() { return {true, radioOn, linked.load(), incoming.active, receivedThisBoot}; }
uint32_t storeGeneration() { return generation; }

}  // namespace phonesync

#else

namespace phonesync {
void begin(const Host&) {}
void tick(Where, bool, bool) {}
bool stop(uint32_t) { return true; }
void syncBeforeSleep() {}
void forgetPhone() {}
Status status() { return {false, false, false, false, 0}; }
uint32_t storeGeneration() { return 0; }
}  // namespace phonesync

#endif
