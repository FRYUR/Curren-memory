#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace curren {

class Arena {
 public:
  explicit Arena(std::size_t chunk_size = 64 * 1024) noexcept;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&&) noexcept = default;
  Arena& operator=(Arena&&) noexcept = default;

  std::byte* alloc(std::size_t n,
                   std::size_t align = alignof(std::max_align_t)) noexcept;
  void reset() noexcept;

  std::size_t bytes_reserved() const noexcept { return bytes_reserved_; }
  std::size_t chunk_count() const noexcept { return chunks_.size(); }

 private:
  struct Chunk {
    std::unique_ptr<std::byte[]> data;
    std::size_t capacity;
    std::size_t used;
  };

  std::size_t chunk_size_;
  std::size_t bytes_reserved_ = 0;
  std::vector<Chunk> chunks_;
};

}  // namespace curren
