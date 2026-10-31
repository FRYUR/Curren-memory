#include "curren/arena.hpp"

#include <cassert>
#include <cstdint>

int main() {
  curren::Arena arena(32);
  auto* aligned = arena.alloc(3, 64);
  assert(aligned != nullptr);
  assert(reinterpret_cast<std::uintptr_t>(aligned) % 64 == 0);

  for (int i = 0; i < 3; ++i) {
    auto* bytes = arena.alloc(48);
    assert(bytes != nullptr);
    bytes[0] = static_cast<std::byte>(i);
  }
  assert(arena.chunk_count() >= 4);
  assert(aligned[0] == std::byte{0});

  const auto reserved = arena.bytes_reserved();
  assert(reserved >= 32);
  arena.reset();
  assert(arena.chunk_count() == 0);
  assert(arena.bytes_reserved() == 0);
  assert(arena.alloc(8) != nullptr);
}
