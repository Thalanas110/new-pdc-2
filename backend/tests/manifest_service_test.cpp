#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "services/swarm/manifest_service.hpp"

namespace {

std::uint64_t hash_piece(const std::vector<char>& bytes) {
  std::uint64_t hash = 14695981039346656037ull;
  for (char byte : bytes) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string hash_hex(std::uint64_t value) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int index = 15; index >= 0; --index) {
    out[static_cast<std::size_t>(index)] = kHex[value & 0xfu];
    value >>= 4u;
  }
  return out;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;

  const fs::path temp = fs::temp_directory_path() / "loopline-manifest-test.bin";
  std::ofstream output(temp, std::ios::binary);
  assert(output);

  const std::size_t piece_size = 256u * 1024u;
  const std::vector<char> first_piece(piece_size, 'A');
  const std::vector<char> second_piece(piece_size, 'B');
  const std::vector<char> third_piece(700000u - (piece_size * 2u), 'C');

  output.write(first_piece.data(), static_cast<std::streamsize>(first_piece.size()));
  output.write(second_piece.data(), static_cast<std::streamsize>(second_piece.size()));
  output.write(third_piece.data(), static_cast<std::streamsize>(third_piece.size()));
  output.close();

  ManifestService service;
  const auto manifest = service.build_manifest(temp, "demo.bin", "node-a");
  assert(manifest.has_value());
  assert(manifest->display_name == "demo.bin");
  assert(manifest->publisher_node_id == "node-a");
  assert(manifest->file_size == 700000u);
  assert(manifest->piece_size == piece_size);
  assert(manifest->piece_count == 3);
  assert(manifest->piece_hashes.size() == 3);
  assert(manifest->piece_hashes[0] == hash_piece(first_piece));
  assert(manifest->piece_hashes[1] == hash_piece(second_piece));
  assert(manifest->piece_hashes[2] == hash_piece(third_piece));
  assert(manifest->piece_hashes[0] != manifest->piece_hashes[1]);
  assert(!manifest->torrent_id.empty());
  assert(!manifest->created_at.empty());

  const std::string json = service.manifest_json(*manifest);
  assert(json.find("\"torrent_id\":\"" + manifest->torrent_id + "\"") != std::string::npos);
  assert(json.find("\"display_name\":\"demo.bin\"") != std::string::npos);
  assert(json.find("\"publisher_node_id\":\"node-a\"") != std::string::npos);
  assert(json.find("\"file_size\":700000") != std::string::npos);
  assert(json.find("\"piece_size\":262144") != std::string::npos);
  assert(json.find("\"piece_count\":3") != std::string::npos);
  assert(json.find("\"piece_hashes\":[\"" + hash_hex(manifest->piece_hashes[0]) + "\",\"" +
                   hash_hex(manifest->piece_hashes[1]) + "\",\"" +
                   hash_hex(manifest->piece_hashes[2]) + "\"]") != std::string::npos);
  assert(json.find("\"created_at\":\"" + manifest->created_at + "\"") != std::string::npos);

  const auto missing_manifest =
      service.build_manifest(temp.parent_path() / "loopline-manifest-missing.bin", "missing.bin", "node-a");
  assert(!missing_manifest.has_value());

  fs::remove(temp);
  return 0;
}
