#include "curren/arena.hpp"

#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

namespace curren {

Arena::Arena(std::size_t chunk_size) noexcept
    : chunk_size_(chunk_size == 0 ? 1 : chunk_size) {}

std::byte* Arena::alloc(std::size_t n, std::size_t align) noexcept {
  if (align == 0 || (align & (align - 1)) != 0) {
    return nullptr;
  }

  const std::size_t requested = n == 0 ? 1 : n;
  for (auto& chunk : chunks_) {
    void* current = chunk.data.get() + chunk.used;
    std::size_t space = chunk.capacity - chunk.used;
    void* result = std::align(align, requested, current, space);
    if (result != nullptr) {
      const auto* begin = chunk.data.get();
      const auto* end = static_cast<std::byte*>(result) + requested;
      chunk.used = static_cast<std::size_t>(end - begin);
      return static_cast<std::byte*>(result);
    }
  }

  if (requested > std::numeric_limits<std::size_t>::max() - (align - 1)) {
    return nullptr;
  }
  const std::size_t needed = requested + align - 1;
  const std::size_t capacity = needed > chunk_size_ ? needed : chunk_size_;
  if (capacity > std::numeric_limits<std::size_t>::max() - bytes_reserved_) {
    return nullptr;
  }

  try {
    Chunk chunk{std::make_unique<std::byte[]>(capacity), capacity, 0};
    void* current = chunk.data.get();
    std::size_t space = chunk.capacity;
    void* result = std::align(align, requested, current, space);
    if (result == nullptr) {
      return nullptr;
    }
    const auto* begin = chunk.data.get();
    const auto* end = static_cast<std::byte*>(result) + requested;
    chunk.used = static_cast<std::size_t>(end - begin);
    std::byte* allocation = static_cast<std::byte*>(result);
    chunks_.push_back(std::move(chunk));
    bytes_reserved_ += capacity;
    return allocation;
  } catch (const std::bad_alloc&) {
    return nullptr;
  } catch (const std::length_error&) {
    return nullptr;
  }
}

void Arena::reset() noexcept {
  chunks_.clear();
  bytes_reserved_ = 0;
}

}  // namespace curren
