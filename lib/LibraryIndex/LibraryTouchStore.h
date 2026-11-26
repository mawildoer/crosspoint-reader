#pragma once
#include <cstddef>
#include <cstdint>

namespace library::touches {
// Seed legacy recent reads once, oldest first internally. No history-sized RAM cache.
bool initialize(const char* const* newestFirst, size_t count);
bool revision(uint32_t& sequence);
bool lookup(const char* path, uint32_t& sequence);
// nonce=0 records an open; a repeated nonzero phone nonce is idempotent.
bool record(const char* path, uint32_t nonce = 0);
bool canMove(const char* oldPath, const char* newPath);
bool move(const char* oldPath, const char* newPath);
void remove(const char* path);
}  // namespace library::touches
