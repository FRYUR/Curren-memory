#include "curren/curren.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int hex_digit(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool parse_bytes(std::string_view input, std::vector<std::byte>& output) {
  output.clear();
  if (input.starts_with("text:")) {
    input.remove_prefix(5);
    for (const unsigned char byte : input) {
      output.push_back(static_cast<std::byte>(byte));
    }
    return true;
  }
  if (!input.starts_with("hex:")) {
    return false;
  }
  input.remove_prefix(4);
  if (input.size() % 2 != 0) {
    return false;
  }
  for (std::size_t i = 0; i < input.size(); i += 2) {
    const int high = hex_digit(input[i]);
    const int low = hex_digit(input[i + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    output.push_back(static_cast<std::byte>((high << 4) | low));
  }
  return true;
}

void print_hex(std::span<const std::byte> bytes) {
  for (const std::byte byte : bytes) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned int>(std::to_integer<unsigned char>(byte));
  }
  std::cout << std::dec << '\n';
}

std::span<const std::byte> as_bytes(const std::vector<std::byte>& bytes) {
  return {bytes.data(), bytes.size()};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: curren-cli <log-file>\n";
    return 2;
  }

  auto store = curren::Store::open(argv[1]);
  if (!store.has_value()) {
    std::cerr << "could not open or replay store\n";
    return 1;
  }

  std::cout << "Commands: put <hex:...|text:...> <hex:...|text:...>, "
               "get <key>, del <key>, compact, quit\n";
  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command.empty()) {
      continue;
    }
    if (command == "quit") {
      break;
    }
    if (command == "compact") {
      std::cout << (store->compact() ? "ok\n" : "compact failed\n");
      continue;
    }

    std::string key_token;
    input >> key_token;
    std::vector<std::byte> key;
    if (!parse_bytes(key_token, key)) {
      std::cout << "invalid key; use hex: or text:\n";
      continue;
    }

    if (command == "put") {
      std::string value_token;
      input >> value_token;
      std::vector<std::byte> value;
      if (!parse_bytes(value_token, value)) {
        std::cout << "invalid value; use hex: or text:\n";
        continue;
      }
      std::cout << (store->put(as_bytes(key), as_bytes(value)) ? "ok\n"
                                                               : "put failed\n");
    } else if (command == "get") {
      const auto value = store->get(as_bytes(key));
      if (!value.has_value()) {
        std::cout << "not found\n";
      } else {
        print_hex(*value);
      }
    } else if (command == "del") {
      std::cout << (store->del(as_bytes(key)) ? "ok\n" : "not found or failed\n");
    } else {
      std::cout << "unknown command\n";
    }
    if (!store->healthy()) {
      std::cerr << "store is unhealthy; stop and reopen it\n";
      break;
    }
  }
  store->sync();
  return store->healthy() ? 0 : 1;
}
