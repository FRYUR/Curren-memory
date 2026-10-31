#include "curren/curren.hpp"

#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace curren {

Store::Store(Table&& table, Log&& log) noexcept
    : table_(std::move(table)), log_(std::move(log)) {}

std::optional<Store> Store::open(const std::filesystem::path& path,
                                 Options options) noexcept {
  try {
    Table table(options.initial_capacity);
    if (table.capacity() == 0) {
      return std::nullopt;
    }

    auto log = Log::open(path, options.sync_on_write);
    if (!log.has_value() || !log->replay(table)) {
      return std::nullopt;
    }
    return Store(std::move(table), std::move(*log));
  } catch (const std::bad_alloc&) {
    return std::nullopt;
  }
}

bool Store::put(std::span<const std::byte> key,
                std::span<const std::byte> value) noexcept {
  if (!healthy() ||
      key.size() > std::numeric_limits<std::uint32_t>::max() ||
      value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  if (!log_.append_put(key, value)) {
    if (!log_.healthy()) {
      healthy_ = false;
    }
    return false;
  }
  if (!table_.put(key, value)) {
    healthy_ = false;
    return false;
  }
  return true;
}

std::optional<std::span<const std::byte>> Store::get(
    std::span<const std::byte> key) const noexcept {
  return table_.get(key);
}

bool Store::del(std::span<const std::byte> key) noexcept {
  if (!healthy() || key.size() > std::numeric_limits<std::uint32_t>::max() ||
      !table_.get(key).has_value()) {
    return false;
  }
  if (!log_.append_del(key)) {
    if (!log_.healthy()) {
      healthy_ = false;
    }
    return false;
  }
  if (!table_.del(key)) {
    healthy_ = false;
    return false;
  }
  return true;
}

bool Store::compact() {
  if (!healthy() || !log_.compact(table_)) {
    healthy_ = false;
    return false;
  }
  table_.clear();
  if (!log_.replay(table_)) {
    healthy_ = false;
    return false;
  }
  return true;
}

void Store::sync() noexcept {
  if (!healthy() || !log_.sync()) {
    healthy_ = false;
  }
}

}  // namespace curren
