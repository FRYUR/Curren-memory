#pragma once

#include "curren/arena.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace curren {

class Table {
 public:
  using HashFunction = std::uint64_t (*)(std::span<const std::byte>) noexcept;
  using VisitFunction = bool (*)(void*, std::span<const std::byte>,
                                 std::span<const std::byte>);

  explicit Table(std::size_t initial_capacity = 16,
                 HashFunction hash = fnv1a64) noexcept;
  Table(const Table&) = delete;
  Table& operator=(const Table&) = delete;
  Table(Table&&) noexcept = default;
  Table& operator=(Table&&) noexcept = default;

  bool put(std::span<const std::byte> key,
           std::span<const std::byte> value) noexcept;
  std::optional<std::span<const std::byte>> get(
      std::span<const std::byte> key) const noexcept;
  bool del(std::span<const std::byte> key) noexcept;
  bool visit(VisitFunction visitor, void* context) const;
  void clear() noexcept;

  std::size_t size() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return slots_.size(); }
  std::size_t wasted_bytes() const noexcept { return wasted_bytes_; }

  static std::uint64_t fnv1a64(
      std::span<const std::byte> bytes) noexcept;

 private:
  struct Entry {
    std::uint32_t key_len;
    std::uint32_t val_len;
  };

  struct Slot {
    std::uint64_t hash;
    Entry* entry;
  };

  struct SearchResult {
    std::size_t index;
    bool found;
  };

  SearchResult search(std::span<const std::byte> key,
                      std::uint64_t hash) const noexcept;
  bool ensure_insert_capacity() noexcept;
  bool rehash(std::size_t capacity) noexcept;
  static std::span<const std::byte> key_bytes(const Entry* entry) noexcept;
  static std::span<const std::byte> value_bytes(const Entry* entry) noexcept;
  static std::size_t entry_size(const Entry* entry) noexcept;

  Arena arena_;
  std::vector<Slot> slots_;
  HashFunction hash_;
  std::size_t size_ = 0;
  std::size_t tombstones_ = 0;
  std::size_t wasted_bytes_ = 0;
};

}  // namespace curren
