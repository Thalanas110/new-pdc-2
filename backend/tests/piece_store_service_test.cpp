#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <string>
#include <vector>

#include "services/swarm/piece_store_service.hpp"

namespace {

std::uint64_t fnv1a(const std::vector<char>& bytes) {
  std::uint64_t hash = 14695981039346656037ull;
  for (char byte : bytes) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::vector<char> bytes_of(std::string_view text) {
  return std::vector<char>(text.begin(), text.end());
}

}  // namespace

int main() {
  namespace fs = std::filesystem;

  const fs::path root = fs::temp_directory_path() / "loopline-piece-store-test";
  fs::remove_all(root);

  TorrentManifest manifest;
  manifest.torrent_id = "torrent-a";
  manifest.display_name = "demo.bin";
  manifest.file_size = 8;
  manifest.piece_size = 4;
  manifest.piece_count = 2;
  const auto piece0 = bytes_of("ABCD");
  const auto piece1 = bytes_of("EFGH");
  manifest.piece_hashes = {fnv1a(piece0), fnv1a(piece1)};

  PieceStoreService store(root);
  assert(store.store_piece(manifest, 0, piece0) == true);
  assert(store.has_piece(manifest, 0) == true);
  assert(store.has_piece(manifest, 1) == false);
  assert(store.has_piece(manifest, 2) == false);

  const auto missing = store.missing_pieces(manifest);
  assert(missing.size() == 1);
  assert(missing[0] == 1);

  const auto stored_root = root / "pieces";
  std::size_t stored_files = 0;
  fs::path stored_piece_path;
  for (const auto& entry : fs::recursive_directory_iterator(stored_root)) {
    if (entry.is_regular_file()) {
      ++stored_files;
      stored_piece_path = entry.path();
      assert(entry.path().filename() == "piece-000000.bin");
      assert(entry.path().string().find("..") == std::string::npos);
    }
  }
  assert(stored_files == 1);

  TorrentManifest malicious_manifest = manifest;
  malicious_manifest.torrent_id = "../../escape-me";
  const auto malicious_root = fs::temp_directory_path() / "loopline-piece-store-malicious";
  fs::remove_all(malicious_root);

  PieceStoreService malicious_store(malicious_root);
  assert(malicious_store.store_piece(malicious_manifest, 0, piece0) == true);
  std::size_t malicious_files = 0;
  for (const auto& entry : fs::recursive_directory_iterator(malicious_root / "pieces")) {
    if (entry.is_regular_file()) {
      ++malicious_files;
      assert(entry.path().string().find("escape-me") == std::string::npos);
    }
  }
  assert(malicious_files == 1);
  assert(!fs::exists(malicious_root / "escape-me"));

  {
    std::ofstream corrupt_file(stored_piece_path, std::ios::binary | std::ios::trunc);
    corrupt_file.write("ZZ", 2);
  }
  assert(store.has_piece(manifest, 0) == false);
  const auto missing_after_corrupt = store.missing_pieces(manifest);
  assert(missing_after_corrupt.size() == 2);
  assert(missing_after_corrupt[0] == 0);
  assert(missing_after_corrupt[1] == 1);

  fs::remove_all(root);
  fs::remove_all(malicious_root);
  return 0;
}
