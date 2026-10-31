#include "curren/log.hpp"
#include "curren/curren.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/types.h>
#include <unistd.h>

using namespace std::string_literals;

namespace {

std::span<const std::byte> bytes(const std::string& value) {
  return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

std::filesystem::path test_path(const char* suffix) {
  return std::filesystem::temp_directory_path() /
         (std::string("curren-memory-") + std::to_string(::getpid()) + "-" +
          suffix + ".log");
}

void append_put(curren::Log& log, curren::Table& table,
                const std::string& key, const std::string& value) {
  assert(log.append_put(bytes(key), bytes(value)));
  assert(table.put(bytes(key), bytes(value)));
}

}  // namespace

int main() {
  const std::string key = "a\0b"s;
  const std::string value = "value\0bytes"s;

  const auto roundtrip_path = test_path("roundtrip");
  std::filesystem::remove(roundtrip_path);
  {
    auto log = curren::Log::open(roundtrip_path);
    assert(log.has_value());
    curren::Table source;
    append_put(*log, source, key, value);
    assert(log->append_del(bytes(key)));
    assert(source.del(bytes(key)));
    assert(log->sync());
  }
  {
    auto log = curren::Log::open(roundtrip_path);
    assert(log.has_value());
    curren::Table recovered;
    assert(log->replay(recovered));
    assert(recovered.size() == 0);
    assert(!recovered.get(bytes(key)).has_value());
  }
  std::filesystem::remove(roundtrip_path);

  const auto tail_path = test_path("tail");
  std::filesystem::remove(tail_path);
  std::uintmax_t valid_size = 0;
  {
    auto log = curren::Log::open(tail_path);
    assert(log.has_value());
    curren::Table source;
    append_put(*log, source, "keep", "value");
    valid_size = std::filesystem::file_size(tail_path);
  }
  {
    std::ofstream tail(tail_path, std::ios::binary | std::ios::app);
    const char truncated[] = {1, 2, 3, 4, 5};
    tail.write(truncated, sizeof(truncated));
  }
  {
    auto log = curren::Log::open(tail_path);
    assert(log.has_value());
    curren::Table recovered;
    assert(log->replay(recovered));
    assert(recovered.get(bytes("keep")).has_value());
    assert(std::filesystem::file_size(tail_path) == valid_size);
  }
  std::filesystem::remove(tail_path);

  const auto crc_path = test_path("crc");
  std::filesystem::remove(crc_path);
  {
    auto log = curren::Log::open(crc_path);
    assert(log.has_value());
    curren::Table source;
    append_put(*log, source, "good", "first");
    append_put(*log, source, "bad", "second");
  }
  {
    std::fstream file(crc_path,
                      std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(-1, std::ios::end);
    char last = 0;
    file.read(&last, 1);
    last ^= 0x7f;
    file.seekp(-1, std::ios::end);
    file.write(&last, 1);
  }
  {
    auto log = curren::Log::open(crc_path);
    assert(log.has_value());
    curren::Table recovered;
    assert(log->replay(recovered));
    assert(recovered.get(bytes("good")).has_value());
    assert(!recovered.get(bytes("bad")).has_value());
  }
  std::filesystem::remove(crc_path);

  const auto compact_path = test_path("compact");
  std::filesystem::remove(compact_path);
  {
    auto log = curren::Log::open(compact_path);
    assert(log.has_value());
    curren::Table live;
    append_put(*log, live, "key", std::string(4096, 'x'));
    append_put(*log, live, "key", "small");
    const auto old_size = std::filesystem::file_size(compact_path);
    assert(log->compact(live));
    assert(std::filesystem::file_size(compact_path) < old_size);
    live.clear();
    assert(log->replay(live));
    assert(live.size() == 1);
    assert(live.get(bytes("key")).has_value());
    assert((*live.get(bytes("key"))).size() == 5);
  }
  std::filesystem::remove(compact_path);

  const auto store_path = test_path("store-api");
  std::filesystem::remove(store_path);
  {
    auto store = curren::Store::open(store_path, {.sync_on_write = true});
    assert(store.has_value());
    const std::span<const std::byte> empty;
    assert(store->put(empty, empty));
    assert(store->get(empty).has_value());
    assert(store->get(empty)->empty());
    assert(store->put(bytes(key), bytes(value)));
    assert(store->size() == 2);
    assert(store->compact());
    assert(store->del(empty));
    store->sync();
    assert(store->healthy());
  }
  {
    auto store = curren::Store::open(store_path);
    assert(store.has_value());
    assert(store->size() == 1);
    assert(store->get(bytes(key)).has_value());
    assert((*store->get(bytes(key))).size() == value.size());
  }
  std::filesystem::remove(store_path);
}
