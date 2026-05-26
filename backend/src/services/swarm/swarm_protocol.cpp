#include "services/swarm/swarm_protocol.hpp"

#include <sstream>

namespace {

std::string encode_frame(const std::string& message_type, const std::string& payload) {
  std::ostringstream out;
  out << "SWARM/1\n" << message_type << '\n' << payload;
  return out.str();
}

}  // namespace

std::string SwarmProtocol::encode_hello(const std::string& node_id, const std::string& host, int port) {
  std::ostringstream payload;
  payload << "NODE " << node_id << '\n' << "HOST " << host << '\n' << "PORT " << port << '\n';
  return encode_frame("HELLO", payload.str());
}

std::string SwarmProtocol::encode_manifest_request(const std::string& torrent_id) {
  return encode_frame("MANIFEST_REQUEST", "TORRENT " + torrent_id + '\n');
}

std::string SwarmProtocol::encode_piece_request(const std::string& torrent_id, std::uint64_t piece_index) {
  std::ostringstream payload;
  payload << "TORRENT " << torrent_id << '\n' << "PIECE " << piece_index << '\n';
  return encode_frame("PIECE_REQUEST", payload.str());
}
