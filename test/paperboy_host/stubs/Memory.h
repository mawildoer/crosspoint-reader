#pragma once
#include <memory>
#include <new>
inline bool failAllocation = false;
template <typename T>
auto makeUniqueNoThrow(size_t count) {
  if (failAllocation) {
    failAllocation = false;
    return std::unique_ptr<T>();
  }
  using Element = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Element[count]());
}
