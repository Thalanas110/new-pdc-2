#include <cassert>
#include <filesystem>
#include <vector>

#include "services/swarm/piece_store_service.hpp"

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
  manifest.piece_hashes = {14695981039346656037ull ^ 'A', 14695981039346656037ull ^ 'E'};

  PieceStoreService store(root);
  assert(store.store_piece(manifest, 0, {'A', 'B', 'C', 'D'}) == true);
  assert(store.has_piece(manifest, 0) == true);
  assert(store.has_piece(manifest, 1) == false);

  const auto missing = store.missing_pieces(manifest);
  assert(missing.size() == 1);
  assert(missing[0] == 1);

  fs::remove_all(root);
  return 0;
}
