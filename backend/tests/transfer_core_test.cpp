#include <cassert>
#include <iostream>
#include <optional>
#include <string>

#include "core/transfer_core.hpp"

int main() {
  using transfer::format_bytes;
  using transfer::content_type_for_file;
  using transfer::is_allowed_peer;
  using transfer::parse_peer_endpoint;
  using transfer::peer_endpoint_key;
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

  assert(content_type_for_file("photo.png") == "image/png");
  assert(content_type_for_file("paper.pdf") == "application/pdf");
  assert(content_type_for_file("notes.txt") == "text/plain; charset=utf-8");
  assert(content_type_for_file("clip.mp4") == "video/mp4");
  assert(content_type_for_file("voice.mp3") == "audio/mpeg");
  assert(content_type_for_file("archive.zip") == "application/octet-stream");

  const auto peer = parse_peer_endpoint("10.113.71.244:8788", 8788);
  assert(peer.has_value());
  assert(peer->host == "10.113.71.244");
  assert(peer->port == 8788);
  assert(peer_endpoint_key(peer->host, peer->port) == "10.113.71.244:8788");
  const auto fallback_peer = parse_peer_endpoint("192.168.43.25", 8788);
  assert(fallback_peer.has_value());
  assert(fallback_peer->port == 8788);
  assert(!parse_peer_endpoint("8.8.8.8:8788", 8788).has_value());
  assert(!parse_peer_endpoint("10.0.0.4:99999", 8788).has_value());

  std::cout << "transfer_core_test passed\n";
  return 0;
}

