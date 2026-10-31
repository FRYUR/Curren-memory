#pragma once

#include "curren/table.hpp"

#include <filesystem>
#include <optional>
#include <span>

namespace curren {

class Log {
 public:
  static std::optional<Log> open(const std::filesystem::path& path,
                                 bool sync_every_write = false) noexcept;
  Log(const Log&) = delete;
  Log& operator=(const Log&) = delete;
  Log(Log&& other) noexcept;
  Log& operator=(Log&& other) noexcept;
  ~Log();

  bool replay(Table& table) noexcept;
  bool append_put(std::span<const std::byte> key,
                  std::span<const std::byte> value) noexcept;
  bool append_del(std::span<const std::byte> key) noexcept;
  bool compact(const Table& table);
  bool sync() noexcept;
  bool healthy() const noexcept { return !failed_; }

 private:
  Log(int fd, std::filesystem::path path, bool sync_every_write) noexcept;
  bool append_record(std::uint8_t op, std::span<const std::byte> key,
                     std::span<const std::byte> value) noexcept;

  int fd_ = -1;
  std::filesystem::path path_;
  bool sync_every_write_ = false;
  bool failed_ = false;
};

}  // namespace curren
