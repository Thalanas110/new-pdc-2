#include <cassert>
#include <iostream>
#include <string>

#include "transfer_core.hpp"

int main() {
  using transfer::format_bytes;
  using transfer::is_allowed_peer;
  using transfer::safe_file_name;

  assert(format_bytes(0) == "0 B");
  assert(format_bytes(1024) == "1 KB");
  assert(format_bytes(1536) == "1.5 KB");
  assert(format_bytes(5 * 1024 * 1024) == "5 MB");

  assert(safe_file_name("report.pdf") == "report.pdf");
  assert(safe_file_name("../secrets.txt") == "secrets.txt");
  assert(safe_file_name("C:\\tmp\\demo.bin") == "demo.bin");
  assert(safe_file_name("") == "download.bin");

  assert(is_allowed_peer("127.0.0.1", false));
  assert(!is_allowed_peer("192.168.1.42", false));
  assert(is_allowed_peer("192.168.1.42", true));
  assert(is_allowed_peer("10.0.0.8", true));
  assert(is_allowed_peer("172.20.4.9", true));
  assert(is_allowed_peer("172.20.10.2", true));
  assert(is_allowed_peer("192.168.43.25", true));
  assert(is_allowed_peer("192.168.137.18", true));
  assert(!is_allowed_peer("8.8.8.8", true));
  assert(!is_allowed_peer("0.0.0.0", true));

  std::cout << "transfer_core_test passed\n";
  return 0;
}
