#pragma once

#include <cstdint>
#include <string>

class SwarmProtocol {
 public:
  static std::string encode_hello(const std::string& node_id, const std::string& host, int port);
  static std::string encode_manifest_request(const std::string& torrent_id);
  static std::string encode_piece_request(const std::string& torrent_id, std::uint64_t piece_index);
};
