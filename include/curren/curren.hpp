#pragma once

#include "curren/log.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>

namespace curren {

struct Options {
  bool sync_on_write = false;
  std::size_t initial_capacity = 16;
};

class Store {
 public:
  static std::optional<Store> open(const std::filesystem::path& path,
                                   Options options = {}) noexcept;
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) noexcept = default;
  Store& operator=(Store&&) noexcept = default;

  bool put(std::span<const std::byte> key,
           std::span<const std::byte> value) noexcept;
  // Returned bytes remain valid until the next mutating call on this store.
  std::optional<std::span<const std::byte>> get(
      std::span<const std::byte> key) const noexcept;
  bool del(std::span<const std::byte> key) noexcept;
  std::size_t size() const noexcept { return table_.size(); }
  bool compact();
  void sync() noexcept;
  bool healthy() const noexcept { return healthy_ && log_.healthy(); }

 private:
  Store(Table&& table, Log&& log) noexcept;

  Table table_;
  Log log_;
  bool healthy_ = true;
};

}  // namespace curren
