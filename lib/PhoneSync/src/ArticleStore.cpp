#include "ArticleStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace phonesync::store {
namespace {

constexpr size_t kMaxLine = 600;
constexpr size_t kMaxField = 160;

// Reads the file line by line into a fixed buffer; over-long lines are cut.
template <typename OnLine>
bool readLines(const char* path, OnLine&& onLine) {
  if (!Storage.exists(path)) return true;
  HalFile file = Storage.open(path, O_RDONLY);
  if (!file) return false;
  char line[kMaxLine + 1];
  size_t used = 0;
  char chunk[256];
  bool keepGoing = true;
  while (keepGoing) {
    const int got = file.read(chunk, sizeof(chunk));
    if (got < 0) {
      file.close();
      return false;
    }
    if (got == 0) break;
    for (int i = 0; i < got && keepGoing; i++) {
      const char c = chunk[i];
      if (c == '\n') {
        line[used] = '\0';
        if (used > 0) keepGoing = onLine(line);
        used = 0;
      } else if (c != '\r' && used < kMaxLine) {
        line[used++] = c;
      }
    }
  }
  if (keepGoing && used > 0) {
    line[used] = '\0';
    onLine(line);
  }
  file.close();
  return true;
}

// Splits "id\ttime\tsource\ttitle\tauthor" in place.
bool splitIndexLine(char* line, char*& id, uint32_t& time, char*& source, char*& title, char*& author) {
  char* fields[5] = {line, nullptr, nullptr, nullptr, nullptr};
  int count = 1;
  for (char* p = line; *p && count < 5; p++) {
    if (*p == '\t') {
      *p = '\0';
      fields[count++] = p + 1;
    }
  }
  if (count < 4 || strlen(fields[0]) != kIdHexLen) return false;
  id = fields[0];
  time = static_cast<uint32_t>(strtoul(fields[1], nullptr, 10));
  source = fields[2];
  title = fields[3];
  author = fields[4] ? fields[4] : fields[3] + strlen(fields[3]);
  return true;
}

// A field must not carry the separators of the index.
void appendField(std::string& out, const char* text, size_t len) {
  len = std::min(len, kMaxField);
  for (size_t i = 0; i < len; i++) {
    const char c = text[i];
    out += (c == '\t' || c == '\n' || c == '\r' || c == '\0') ? ' ' : c;
  }
}

}  // namespace

bool ensureDir() { return Storage.exists(kDir) || Storage.mkdir(kDir); }

std::string pathFor(const char* idHex) {
  std::string path = kDir;
  path += '/';
  path += idHex;
  path += ".epub";
  return path;
}

bool idOfPath(const char* path, char idHex[kIdHexLen + 1]) {
  const size_t dirLen = sizeof(kDir) - 1;
  if (strncmp(path, kDir, dirLen) != 0 || path[dirLen] != '/') return false;
  const char* name = path + dirLen + 1;
  if (strlen(name) != kIdHexLen + 5 || strcmp(name + kIdHexLen, ".epub") != 0) return false;
  memcpy(idHex, name, kIdHexLen);
  idHex[kIdHexLen] = '\0';
  return true;
}

bool forEach(const Visitor visit, void* ctx) {
  return readLines(kIndexPath, [&](char* line) {
    char *id, *source, *title, *author;
    uint32_t time;
    if (!splitIndexLine(line, id, time, source, title, author)) return true;
    return visit(ctx, id, time, source, title, author);
  });
}

bool contains(const char* idHex) {
  struct Ctx {
    const char* want;
    bool found;
  } ctx{idHex, false};
  forEach(
      [](void* c, const char* id, uint32_t, const char*, const char*, const char*) {
        auto* ctx = static_cast<Ctx*>(c);
        if (strcmp(id, ctx->want) != 0) return true;
        ctx->found = true;
        return false;
      },
      &ctx);
  return ctx.found;
}

bool append(const char* idHex, const uint32_t time, const char* source, const size_t sourceLen, const char* title,
            const size_t titleLen, const char* author, const size_t authorLen) {
  std::string line;
  line.reserve(kIdHexLen + 16 + sourceLen + titleLen + authorLen);
  line += idHex;
  line += '\t';
  char number[12];
  snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(time));
  line += number;
  line += '\t';
  appendField(line, source, sourceLen);
  line += '\t';
  appendField(line, title, titleLen);
  line += '\t';
  appendField(line, author, authorLen);
  line += '\n';
  HalFile file = Storage.open(kIndexPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return false;
  const uint64_t originalSize = file.size();
  const bool ok = file.write(line.data(), line.size()) == line.size();
  if (!ok && !file.truncate(originalSize)) LOG_ERR("SYNC", "Failed to roll back article index append");
  file.close();
  return ok;
}

bool remove(const char* idHex) {
  const std::string path = pathFor(idHex);
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) return false;
  if (!contains(idHex)) return true;

  // Copy every other line to a temp file, then move it over the index.
  static constexpr char kTmp[] = "/Articles/index.tmp";
  HalFile out = Storage.open(kTmp, O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) return false;
  bool ok = true;
  const bool readOk = readLines(kIndexPath, [&](char* line) {
    if (strncmp(line, idHex, kIdHexLen) == 0 && line[kIdHexLen] == '\t') return true;
    const size_t len = strlen(line);
    line[len] = '\n';  // the terminator's place; readLines rewrites it for the next line
    ok = out.write(line, len + 1) == len + 1;
    return ok;
  });
  out.close();
  if (!ok || !readOk) {
    Storage.remove(kTmp);
    return false;
  }
  return Storage.replaceFile(kTmp, kIndexPath);
}

uint64_t idValue(const char* idHex) { return strtoull(idHex, nullptr, 16); }

bool loadReadIds(std::vector<uint64_t>& out) {
  out.clear();
  return readLines(kReadPath, [&](char* line) {
    if (strlen(line) == kIdHexLen) out.push_back(idValue(line));
    return true;
  });
}

bool markRead(const char* idHex) {
  bool already = false;
  readLines(kReadPath, [&](char* line) {
    already = strcmp(line, idHex) == 0;
    return !already;
  });
  if (already) return true;
  HalFile file = Storage.open(kReadPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return false;
  char line[kIdHexLen + 1];
  memcpy(line, idHex, kIdHexLen);
  line[kIdHexLen] = '\n';
  const bool ok = file.write(line, sizeof(line)) == sizeof(line);
  file.close();
  return ok;
}

bool clearRead(const char* idHex) {
  bool listed = false;
  if (!readLines(kReadPath, [&](char* line) {
        listed = strcmp(line, idHex) == 0;
        return !listed;
      }))
    return false;
  if (!listed) return true;
  static constexpr char kTmp[] = "/Articles/read.tmp";
  HalFile out = Storage.open(kTmp, O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) return false;
  bool ok = true;
  const bool readOk = readLines(kReadPath, [&](char* line) {
    if (strcmp(line, idHex) == 0) return true;
    const size_t len = strlen(line);
    line[len] = '\n';  // the terminator's place; readLines rewrites it for the next line
    ok = out.write(line, len + 1) == len + 1;
    return ok;
  });
  out.close();
  if (!ok || !readOk) {
    Storage.remove(kTmp);
    return false;
  }
  return Storage.replaceFile(kTmp, kReadPath);
}

bool loadNewest(std::vector<Entry>& out, const size_t maxEntries) {
  out.clear();
  std::vector<uint64_t> readIds;
  loadReadIds(readIds);

  struct Ctx {
    std::vector<Entry>* out;
    size_t max;
  } ctx{&out, maxEntries};
  const bool ok = forEach(
      [](void* c, const char* id, uint32_t time, const char* source, const char* title, const char* author) {
        auto* ctx = static_cast<Ctx*>(c);
        // Keep the newest `max`: drop the oldest kept entry when a newer one arrives.
        if (ctx->out->size() >= ctx->max) {
          auto oldest = std::min_element(ctx->out->begin(), ctx->out->end(),
                                         [](const Entry& a, const Entry& b) { return a.time < b.time; });
          if (oldest->time >= time) return true;
          ctx->out->erase(oldest);
        }
        Entry entry{};
        memcpy(entry.id, id, kIdHexLen + 1);
        entry.time = time;
        entry.source = source;
        entry.title = title;
        entry.author = author;
        ctx->out->push_back(std::move(entry));
        return true;
      },
      &ctx);
  for (auto& entry : out) {
    entry.read = std::find(readIds.begin(), readIds.end(), idValue(entry.id)) != readIds.end();
  }
  std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.time > b.time; });
  return ok;
}

}  // namespace phonesync::store
