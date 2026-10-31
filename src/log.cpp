#include "curren/log.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <new>
#include <string>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace curren {
namespace {

// Little-endian record: [u32 magic=0x4e455255][u8 op][u32 key_len]
// [u32 val_len][key][value][u32 CRC-32/IEEE]. CRC covers preceding bytes.
constexpr std::uint32_t kMagic = 0x4e455255;  // "UREN" on disk.
constexpr std::uint8_t kPut = 1;
constexpr std::uint8_t kDel = 2;
constexpr std::size_t kHeaderSize = 13;
constexpr std::size_t kChecksumSize = 4;

void encode_u32(std::byte* output, std::uint32_t value) noexcept {
  for (unsigned int i = 0; i < 4; ++i) {
    output[i] = static_cast<std::byte>((value >> (i * 8)) & 0xffU);
  }
}

std::uint32_t decode_u32(const std::byte* input) noexcept {
  std::uint32_t value = 0;
  for (unsigned int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(input[i]))
             << (i * 8);
  }
  return value;
}

std::uint32_t crc_update(std::uint32_t crc,
                         std::span<const std::byte> bytes) noexcept {
  for (const std::byte byte : bytes) {
    crc ^= std::to_integer<std::uint8_t>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1) ^ (0xedb88320U & mask);
    }
  }
  return crc;
}

std::uint32_t record_crc(std::span<const std::byte> header,
                         std::span<const std::byte> payload) noexcept {
  std::uint32_t crc = crc_update(0xffffffffU, header);
  crc = crc_update(crc, payload);
  return ~crc;
}

bool write_all(int fd, const std::byte* data, std::size_t size) noexcept {
  while (size != 0) {
    const ssize_t written = ::write(fd, data, size);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return false;
    }
    const auto count = static_cast<std::size_t>(written);
    data += count;
    size -= count;
  }
  return true;
}

bool read_at(int fd, std::byte* data, std::size_t size,
             off_t offset) noexcept {
  while (size != 0) {
    const ssize_t count = ::pread(fd, data, size, offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return false;
    }
    const auto bytes_read = static_cast<std::size_t>(count);
    data += bytes_read;
    size -= bytes_read;
    offset += count;
  }
  return true;
}

bool write_record(int fd, std::uint8_t op, std::span<const std::byte> key,
                  std::span<const std::byte> value) noexcept {
  if (key.size() > std::numeric_limits<std::uint32_t>::max() ||
      value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  std::array<std::byte, kHeaderSize> header{};
  encode_u32(header.data(), kMagic);
  header[4] = static_cast<std::byte>(op);
  encode_u32(header.data() + 5, static_cast<std::uint32_t>(key.size()));
  encode_u32(header.data() + 9, static_cast<std::uint32_t>(value.size()));
  std::uint32_t crc = crc_update(0xffffffffU, header);
  crc = crc_update(crc, key);
  crc = ~crc_update(crc, value);
  std::array<std::byte, kChecksumSize> checksum{};
  encode_u32(checksum.data(), crc);
  return write_all(fd, header.data(), header.size()) &&
         write_all(fd, key.data(), key.size()) &&
         write_all(fd, value.data(), value.size()) &&
         write_all(fd, checksum.data(), checksum.size());
}

bool apply_record(void* context, std::uint8_t op,
                  std::span<const std::byte> key,
                  std::span<const std::byte> value) noexcept {
  auto& table = *static_cast<Table*>(context);
  if (op == kPut) {
    return table.put(key, value);
  }
  if (op == kDel && value.empty()) {
    table.del(key);
    return true;
  }
  return false;
}

struct CompactContext {
  int fd;
  bool ok;
};

bool compact_entry(void* context, std::span<const std::byte> key,
                   std::span<const std::byte> value) {
  auto& writer = *static_cast<CompactContext*>(context);
  writer.ok = write_record(writer.fd, kPut, key, value);
  return writer.ok;
}

bool truncate_to(int fd, off_t offset) noexcept {
  return ::ftruncate(fd, offset) == 0 && ::fsync(fd) == 0;
}

}  // namespace

Log::Log(int fd, std::filesystem::path path, bool sync_every_write) noexcept
    : fd_(fd), path_(std::move(path)), sync_every_write_(sync_every_write) {}

Log::Log(Log&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)),
      path_(std::move(other.path_)),
      sync_every_write_(other.sync_every_write_),
      failed_(other.failed_) {}

Log& Log::operator=(Log&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = std::exchange(other.fd_, -1);
    path_ = std::move(other.path_);
    sync_every_write_ = other.sync_every_write_;
    failed_ = other.failed_;
  }
  return *this;
}

Log::~Log() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

std::optional<Log> Log::open(const std::filesystem::path& path,
                             bool sync_every_write) noexcept {
  const int fd =
      ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (fd < 0) {
    return std::nullopt;
  }
  try {
    return Log(fd, path, sync_every_write);
  } catch (const std::bad_alloc&) {
    ::close(fd);
    return std::nullopt;
  } catch (const std::length_error&) {
    ::close(fd);
    return std::nullopt;
  }
}

bool Log::replay(Table& table) noexcept {
  if (fd_ < 0 || failed_) {
    return false;
  }

  struct stat file_info {};
  if (::fstat(fd_, &file_info) != 0 || file_info.st_size < 0) {
    failed_ = true;
    return false;
  }

  off_t offset = 0;
  while (offset < file_info.st_size) {
    const off_t remaining = file_info.st_size - offset;
    if (remaining < static_cast<off_t>(kHeaderSize)) {
      if (!truncate_to(fd_, offset)) {
        failed_ = true;
        return false;
      }
      return true;
    }

    std::array<std::byte, kHeaderSize> header{};
    if (!read_at(fd_, header.data(), header.size(), offset)) {
      failed_ = true;
      return false;
    }
    const std::uint32_t magic = decode_u32(header.data());
    const std::uint8_t op = std::to_integer<std::uint8_t>(header[4]);
    const std::uint32_t key_len = decode_u32(header.data() + 5);
    const std::uint32_t val_len = decode_u32(header.data() + 9);
    const std::uint64_t payload_size =
        static_cast<std::uint64_t>(key_len) + val_len;
    const std::uint64_t record_size =
        kHeaderSize + payload_size + kChecksumSize;
    if (magic != kMagic || (op != kPut && op != kDel) ||
        (op == kDel && val_len != 0) ||
        record_size > static_cast<std::uint64_t>(remaining)) {
      if (!truncate_to(fd_, offset)) {
        failed_ = true;
        return false;
      }
      return true;
    }

    try {
      std::vector<std::byte> payload(
          static_cast<std::size_t>(payload_size + kChecksumSize));
      if (!read_at(fd_, payload.data(), payload.size(),
                   offset + static_cast<off_t>(kHeaderSize))) {
        failed_ = true;
        return false;
      }
      const auto payload_bytes =
          std::span<const std::byte>(payload.data(),
                                     static_cast<std::size_t>(payload_size));
      const auto payload_length = static_cast<std::size_t>(payload_size);
      const std::uint32_t expected =
          decode_u32(payload.data() + payload_length);
      if (record_crc(header, payload_bytes) != expected) {
        if (!truncate_to(fd_, offset)) {
          failed_ = true;
          return false;
        }
        return true;
      }

      const auto key =
          std::span<const std::byte>(payload.data(),
                                     static_cast<std::size_t>(key_len));
      const auto value = std::span<const std::byte>(
          payload.data() + static_cast<std::size_t>(key_len),
          static_cast<std::size_t>(val_len));
      if (!apply_record(&table, op, key, value)) {
        failed_ = true;
        return false;
      }
    } catch (const std::bad_alloc&) {
      failed_ = true;
      return false;
    } catch (const std::length_error&) {
      failed_ = true;
      return false;
    }
    offset += static_cast<off_t>(record_size);
  }
  return true;
}

bool Log::append_record(std::uint8_t op, std::span<const std::byte> key,
                        std::span<const std::byte> value) noexcept {
  if (key.size() > std::numeric_limits<std::uint32_t>::max() ||
      value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  if (fd_ < 0 || failed_) {
    return false;
  }
  if (!write_record(fd_, op, key, value)) {
    failed_ = true;
    return false;
  }
  if (sync_every_write_ && !sync()) {
    return false;
  }
  return true;
}

bool Log::append_put(std::span<const std::byte> key,
                     std::span<const std::byte> value) noexcept {
  return append_record(kPut, key, value);
}

bool Log::append_del(std::span<const std::byte> key) noexcept {
  return append_record(kDel, key, {});
}

bool Log::compact(const Table& table) {
  if (fd_ < 0 || failed_) {
    return false;
  }
  std::filesystem::path temporary = path_;
  temporary += ".tmp";
  const int temporary_fd = ::open(
      temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (temporary_fd < 0) {
    return false;
  }

  CompactContext context{temporary_fd, true};
  const bool visited = table.visit(compact_entry, &context);
  bool ok = visited && context.ok && ::fsync(temporary_fd) == 0;
  if (::close(temporary_fd) != 0) {
    ok = false;
  }
  if (!ok) {
    ::unlink(temporary.c_str());
    return false;
  }
  if (::rename(temporary.c_str(), path_.c_str()) != 0) {
    ::unlink(temporary.c_str());
    return false;
  }

  const std::filesystem::path directory =
      path_.has_parent_path() ? path_.parent_path() : ".";
  const int directory_fd = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
  bool directory_synced = directory_fd >= 0 && ::fsync(directory_fd) == 0;
  if (directory_fd >= 0) {
    ::close(directory_fd);
  }

  const int replacement_fd =
      ::open(path_.c_str(), O_RDWR | O_APPEND | O_CLOEXEC);
  if (replacement_fd < 0) {
    failed_ = true;
    return false;
  }
  ::close(fd_);
  fd_ = replacement_fd;
  failed_ = !directory_synced;
  return !failed_;
}

bool Log::sync() noexcept {
  if (fd_ < 0 || failed_ || ::fsync(fd_) != 0) {
    failed_ = true;
    return false;
  }
  return true;
}

}  // namespace curren
