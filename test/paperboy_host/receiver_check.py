"""Compile the real main-loop frame handlers with storage/radio adapters, without ESP32/BLE."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
source = (root / "lib/PhoneSync/src/PhoneSync.cpp").read_text()

def extract(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

prefix = r'''
#include <HalStorage.h>
#include <Memory.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "ArticleStore.h"
#include "FirmwareStage.h"
#include "FirmwareTransferState.h"
#include "LibraryTouchStore.h"
#include "Protocol.h"
#define LOG_INF(...) do {} while(0)
#define LOG_ERR(...) do {} while(0)
#define LOG_DBG(...) do {} while(0)
using namespace phonesync;
using namespace phonesync::proto;
namespace library { bool markLibraryIndexDirty() { return true; } }
enum class TransferKind : uint8_t { Article, Firmware };
constexpr const char* kFirmwarePath = firmware_stage::kImagePath;
constexpr uint32_t kMaxFirmwareBytes = 0x640000;
FirmwareTransferState firmwareTransfer;
std::atomic<bool> firmwareAuthorized{true};
bool powerAllowed = true, freeSpaceStale = false;
uint32_t phoneTime = 0, phoneTimeAtMs = 0, generation = 0, byeAtMs = 0;
uint16_t receivedThisBoot = 0;
std::atomic<uint32_t> droppedFrames{0};
constexpr uint32_t MIN_LIBRARY_EPOCH = 1735689600;
uint32_t ticks = 100;
uint32_t rtcSeconds() { return ticks; }
int64_t wall = 0;
#define time(unused) wall
enum { ESP_RST_POWERON, ESP_RST_SW, ESP_RST_DEEPSLEEP };
int resetReason = ESP_RST_POWERON;
int esp_reset_reason() { return resetReason; }
uint32_t knownFreeMb() { return 0; }
bool begun = false;
uint32_t nowMs() { return 100; }
void esp_task_wdt_reset() {}
void vTaskDelay(int) {}
struct Host {
  const char* firmwareVersion = "test";
  uint8_t (*batteryPercent)() = nullptr;
  bool (*canUpdateFirmware)() = [] { return powerAllowed; };
  bool (*validateFirmware)(const char*) = [](const char*) { return true; };
  bool (*installFirmware)(const char*) = [](const char*) { return true; };
} host;
std::vector<std::vector<uint8_t>> replies;
void send(const uint8_t* bytes, size_t count) { replies.emplace_back(bytes, bytes + count); }
void sendStatus(uint8_t type, uint8_t status) { const uint8_t bytes[]{type,status}; send(bytes,2); }

void onList() {}
void onDelete(const uint8_t*,size_t) {}
'''
functions = ["uint32_t libraryEpoch(", "uint32_t libraryTime()", "void begin(const Host&", "void onHello(", "void sendProgress()", "void abandonIncoming()", "void onPutBegin(", "void onPutData(",
             "void onPutEnd()", "void clearFirmwareStage(", "void sendFirmwareCheckpoint(",
             "void sendFirmwareReady(", "void onFirmwareBegin(", "void onFirmwarePause()",
             "void onFirmwareEnd()", "void onFirmwareCommit()", "void handleFrame("]
checks = r'''
void command(uint8_t type,const std::vector<uint8_t>& payload={}) {
  std::vector<uint8_t> frame{type}; frame.insert(frame.end(),payload.begin(),payload.end());
  handleFrame(frame.data(),frame.size());
}
void expectStatus(uint8_t type,uint8_t status) { assert(replies.back()[0]==type&&replies.back()[1]==status); }
void article(uint8_t id) {
  const std::string text="an article after a paused firmware update";
  std::vector<uint8_t> begin(23); begin[0]=id;
  writeU32(begin.data()+8,text.size());
  writeU32(begin.data()+12,crc32(0,reinterpret_cast<const uint8_t*>(text.data()),text.size()));
  command(PutBegin,begin); expectStatus(PutReady,Ok);
  command(PutData,{text.begin(),text.end()});
  command(PutEnd); expectStatus(PutResult,Ok);
  char hex[17]; idToHex(begin.data(),hex); assert(files[store::pathFor(hex)]==text);
}
int main() {
  begin(host);
  assert(libraryTime() == 0);
  const uint32_t today = 1800000000;
  wall = today - 86400;
  assert(libraryTime() == wall);
  std::vector<uint8_t> hello(5); hello[0] = kVersion; writeU32(hello.data()+1,today);
  command(Hello, hello);
  assert(libraryTime() == today);
  resetReason = ESP_RST_DEEPSLEEP;
  ticks += 5400;
  begin(host);
  assert(libraryTime() == today + 5400);
  resetReason = ESP_RST_SW;
  ticks += 65;
  begin(host);
  assert(libraryTime() == today + 5465);
  const char* path = "/Articles/0100000000000000.epub";
  files[path] = "article";
  assert(library::touches::initialize(nullptr, 0));
  assert(library::touches::record(path, 0, libraryTime()));
  uint32_t touched;
  assert(library::touches::readTimestamp(path, touched) && touched == today + 5465);
  std::vector<uint8_t> bump(12); bump[0]=1; writeU32(bump.data()+8, 123);
  ++ticks; command(Touch,bump); expectStatus(TouchResult,Ok);
  assert(library::touches::readTimestamp(path, touched) && touched == today + 5466);
  ++ticks; command(Touch,bump); expectStatus(TouchResult,Ok);
  assert(library::touches::readTimestamp(path, touched) && touched == today + 5466);
  resetReason = ESP_RST_POWERON;
  begin(host);
  assert(libraryTime() == wall);
  files.erase(path);
  std::string image(10000,'A'); image[0]=char(0xe9);
  std::vector<uint8_t> begin(40);
  writeU32(begin.data(),image.size());
  writeU32(begin.data()+4,crc32(0,reinterpret_cast<const uint8_t*>(image.data()),image.size()));
  begin[8]=1;
  auto resume=[&] { command(FirmwareResume,begin); expectStatus(FirmwareResumeReady,Ok); return readU32(replies.back().data()+2); };
  auto feed=[&](size_t from,size_t end) {
    while(from<end) { auto stop=std::min(end,from+500); command(FirmwareData,{image.begin()+from,image.begin()+stop}); from=stop; }
  };
  assert(resume()==0); feed(0,3000);
  command(FirmwarePause); expectStatus(FirmwarePaused,Ok);
  assert(readU32(replies.back().data()+2)==3000&&!firmwareTransfer.busy());
  assert(resume()==3000); feed(3000,image.size());
  shortWriteAt=writeCount+1;
  command(FirmwareEnd); expectStatus(FirmwareStaged,StorageError);
  shortWriteAt=0;
  assert(firmware_stage::hasIdentity());
  assert(resume()==3000); feed(3000,image.size());
  failRenameAt=renameCount+2;
  command(FirmwareEnd); expectStatus(FirmwareStaged,StorageError);
  failRenameAt=0;
  assert(resume()==image.size());
  command(FirmwareEnd); expectStatus(FirmwareStaged,Ok); assert(firmwareTransfer.validated());
  // A failed phone confirmation-record write sends PAUSE after END, without COMMIT.
  command(FirmwarePause); expectStatus(FirmwarePaused,Ok); assert(!firmwareTransfer.busy());
  article(1);
  assert(resume()==image.size());
  command(FirmwareEnd); expectStatus(FirmwareStaged,Ok);
  powerAllowed=false;
  command(FirmwareCommit); expectStatus(FirmwareResult,LowBattery); assert(!firmwareTransfer.committed());
  command(FirmwarePause); expectStatus(FirmwarePaused,Ok); assert(!firmwareTransfer.busy());
  article(2);
  powerAllowed=true;
  assert(resume()==image.size());
  command(FirmwareEnd); expectStatus(FirmwareStaged,Ok);
  command(FirmwareCommit); expectStatus(FirmwareResult,Ok); assert(firmwareTransfer.committed());
  assert(!firmwareTransfer.pause());
  std::cout<<"PASS: real receiver END failures retain checkpoints; validated PAUSE allows immediate article upload; rejected commit releases for resume and later commit\n";
}
'''
incoming = extract("struct Incoming {") + ";\nIncoming incoming;\n"
clock = extract("struct PhoneClock {") + ";\nPhoneClock phoneClock{};\n"
output.write_text(prefix + clock + incoming + "\n".join(map(extract, functions)) + checks)
