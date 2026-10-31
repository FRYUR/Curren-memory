#include "curren/table.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

std::uint64_t constant_hash(std::span<const std::byte>) noexcept { return 7; }

std::span<const std::byte> bytes(const char* data, std::size_t size) {
  return {reinterpret_cast<const std::byte*>(data), size};
}

}  // namespace

int main() {
  curren::Table table(4, constant_hash);
  const auto key_a = bytes("a", 1);
  const auto key_b = bytes("b", 1);
  const auto value_one = bytes("1", 1);
  const auto value_two = bytes("22", 2);

  assert(table.put(key_a, value_one));
  assert(table.put(key_b, value_one));
  assert(table.get(key_a).has_value());
  assert((*table.get(key_a))[0] == std::byte{'1'});
  assert(table.put(key_a, value_two));
  assert(table.wasted_bytes() > 0);
  assert((*table.get(key_a)).size() == 2);

  assert(table.del(key_a));
  assert(!table.get(key_a).has_value());
  assert(!table.del(key_a));
  assert(table.get(key_b).has_value());

  const std::byte binary_key[] = {std::byte{0}, std::byte{1}, std::byte{0}};
  const std::byte binary_value[] = {std::byte{9}, std::byte{0}};
  assert(table.put(binary_key, binary_value));
  assert(table.get(binary_key).has_value());
  assert((*table.get(binary_key))[0] == std::byte{9});

  const std::span<const std::byte> empty;
  assert(table.put(empty, empty));
  assert(table.get(empty).has_value());
  assert(table.get(empty)->empty());

  const std::size_t original_capacity = table.capacity();
  for (std::uint32_t i = 0; i < 100; ++i) {
    const std::byte key[] = {static_cast<std::byte>(i & 0xff),
                             static_cast<std::byte>((i >> 8) & 0xff)};
    assert(table.put(key, value_one));
  }
  assert(table.capacity() > original_capacity);
  assert(table.size() == 103);
  assert(table.get(key_b).has_value());
}
