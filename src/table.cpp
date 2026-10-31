#include "curren/table.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace curren {
namespace {

constexpr std::uint64_t kEmptyHash = 0;
constexpr std::uint64_t kTombstoneHash = 1;
constexpr std::size_t kMaxEntryLength =
    std::numeric_limits<std::uint32_t>::max();

bool same_bytes(std::span<const std::byte> lhs,
                std::span<const std::byte> rhs) noexcept {
  return lhs.size() == rhs.size() &&
         (lhs.empty() || std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0);
}

std::size_t normalized_capacity(std::size_t capacity) noexcept {
  capacity = std::max<std::size_t>(capacity, 2);
  std::size_t result = 1;
  while (result < capacity) {
    if (result > std::numeric_limits<std::size_t>::max() / 2) {
      return 0;
    }
    result *= 2;
  }
  return result;
}

}  // namespace

Table::Table(std::size_t initial_capacity, HashFunction hash) noexcept
    : hash_(hash == nullptr ? fnv1a64 : hash) {
  const std::size_t capacity = normalized_capacity(initial_capacity);
  if (capacity == 0) {
    return;
  }
  try {
    slots_.resize(capacity, Slot{kEmptyHash, nullptr});
  } catch (const std::bad_alloc&) {
    slots_.clear();
  } catch (const std::length_error&) {
    slots_.clear();
  }
}

std::uint64_t Table::fnv1a64(
    std::span<const std::byte> bytes) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const std::byte byte : bytes) {
    hash ^= std::to_integer<std::uint8_t>(byte);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::span<const std::byte> Table::key_bytes(const Entry* entry) noexcept {
  const auto* bytes = reinterpret_cast<const std::byte*>(entry + 1);
  return {bytes, entry->key_len};
}

std::span<const std::byte> Table::value_bytes(const Entry* entry) noexcept {
  const auto* bytes = reinterpret_cast<const std::byte*>(entry + 1);
  return {bytes + entry->key_len, entry->val_len};
}

std::size_t Table::entry_size(const Entry* entry) noexcept {
  return sizeof(Entry) + static_cast<std::size_t>(entry->key_len) +
         static_cast<std::size_t>(entry->val_len);
}

namespace {

void add_wasted(std::size_t& total, std::size_t amount) noexcept {
  const std::size_t max = std::numeric_limits<std::size_t>::max();
  total = amount > max - total ? max : total + amount;
}

}  // namespace

Table::SearchResult Table::search(std::span<const std::byte> key,
                                  std::uint64_t hash) const noexcept {
  if (slots_.empty()) {
    return {0, false};
  }

  const std::size_t mask = slots_.size() - 1;
  std::size_t index = static_cast<std::size_t>(hash) & mask;
  std::size_t first_tombstone = slots_.size();
  for (std::size_t probes = 0; probes < slots_.size(); ++probes) {
    const Slot& slot = slots_[index];
    if (slot.entry == nullptr) {
      if (slot.hash == kEmptyHash) {
        return {first_tombstone == slots_.size() ? index : first_tombstone,
                false};
      }
      if (first_tombstone == slots_.size()) {
        first_tombstone = index;
      }
    } else if (slot.hash == hash && same_bytes(key_bytes(slot.entry), key)) {
      return {index, true};
    }
    index = (index + 1) & mask;
  }
  return {first_tombstone, false};
}

bool Table::ensure_insert_capacity() noexcept {
  if (slots_.empty()) {
    return rehash(16);
  }
  const std::size_t limit = slots_.size() * 7 / 10;
  if (size_ + tombstones_ + 1 <= limit) {
    return true;
  }

  std::size_t target = slots_.size();
  if (size_ + 1 > limit) {
    if (target > std::numeric_limits<std::size_t>::max() / 2) {
      return false;
    }
    target *= 2;
  }
  return rehash(target);
}

bool Table::rehash(std::size_t capacity) noexcept {
  if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
    return false;
  }

  try {
    std::vector<Slot> fresh(capacity, Slot{kEmptyHash, nullptr});
    const std::size_t mask = capacity - 1;
    for (const Slot& old : slots_) {
      if (old.entry == nullptr) {
        continue;
      }
      std::size_t index = static_cast<std::size_t>(old.hash) & mask;
      while (fresh[index].entry != nullptr) {
        index = (index + 1) & mask;
      }
      fresh[index] = old;
    }
    slots_.swap(fresh);
    tombstones_ = 0;
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  } catch (const std::length_error&) {
    return false;
  }
}

bool Table::put(std::span<const std::byte> key,
                std::span<const std::byte> value) noexcept {
  const std::size_t max_size = std::numeric_limits<std::size_t>::max();
  if (key.size() > kMaxEntryLength || value.size() > kMaxEntryLength ||
      key.size() > max_size - sizeof(Entry) ||
      value.size() > max_size - sizeof(Entry) - key.size()) {
    return false;
  }

  const std::uint64_t hash = hash_(key);
  SearchResult result = search(key, hash);
  if (result.found) {
    Entry* entry = slots_[result.index].entry;
    if (value.size() <= entry->val_len) {
      auto* old_value = const_cast<std::byte*>(value_bytes(entry).data());
      if (!value.empty()) {
        std::memmove(old_value, value.data(), value.size());
      }
      entry->val_len = static_cast<std::uint32_t>(value.size());
      return true;
    }

    auto* memory = arena_.alloc(sizeof(Entry) + key.size() + value.size(),
                                alignof(Entry));
    if (memory == nullptr) {
      return false;
    }
    auto* replacement = reinterpret_cast<Entry*>(memory);
    *replacement = {static_cast<std::uint32_t>(key.size()),
                    static_cast<std::uint32_t>(value.size())};
    auto* replacement_bytes = reinterpret_cast<std::byte*>(replacement + 1);
    if (!key.empty()) {
      std::memcpy(replacement_bytes, key.data(), key.size());
    }
    if (!value.empty()) {
      std::memcpy(replacement_bytes + key.size(), value.data(), value.size());
    }
    add_wasted(wasted_bytes_, entry_size(entry));
    slots_[result.index].entry = replacement;
    return true;
  }

  if (!ensure_insert_capacity()) {
    return false;
  }
  result = search(key, hash);
  if (result.index == slots_.size()) {
    return false;
  }

  auto* memory =
      arena_.alloc(sizeof(Entry) + key.size() + value.size(), alignof(Entry));
  if (memory == nullptr) {
    return false;
  }
  auto* entry = reinterpret_cast<Entry*>(memory);
  *entry = {static_cast<std::uint32_t>(key.size()),
            static_cast<std::uint32_t>(value.size())};
  auto* bytes = reinterpret_cast<std::byte*>(entry + 1);
  if (!key.empty()) {
    std::memcpy(bytes, key.data(), key.size());
  }
  if (!value.empty()) {
    std::memcpy(bytes + key.size(), value.data(), value.size());
  }
  if (slots_[result.index].entry == nullptr &&
      slots_[result.index].hash == kTombstoneHash) {
    --tombstones_;
  }
  slots_[result.index] = {hash, entry};
  ++size_;
  return true;
}

std::optional<std::span<const std::byte>> Table::get(
    std::span<const std::byte> key) const noexcept {
  const SearchResult result = search(key, hash_(key));
  if (!result.found) {
    return std::nullopt;
  }
  return value_bytes(slots_[result.index].entry);
}

bool Table::del(std::span<const std::byte> key) noexcept {
  const SearchResult result = search(key, hash_(key));
  if (!result.found) {
    return false;
  }
  Entry* entry = slots_[result.index].entry;
  add_wasted(wasted_bytes_, entry_size(entry));
  slots_[result.index] = {kTombstoneHash, nullptr};
  --size_;
  ++tombstones_;
  return true;
}

bool Table::visit(VisitFunction visitor, void* context) const {
  if (visitor == nullptr) {
    return false;
  }
  for (const Slot& slot : slots_) {
    if (slot.entry != nullptr &&
        !visitor(context, key_bytes(slot.entry), value_bytes(slot.entry))) {
      return false;
    }
  }
  return true;
}

void Table::clear() noexcept {
  std::fill(slots_.begin(), slots_.end(), Slot{kEmptyHash, nullptr});
  arena_.reset();
  size_ = 0;
  tombstones_ = 0;
  wasted_bytes_ = 0;
}

}  // namespace curren
