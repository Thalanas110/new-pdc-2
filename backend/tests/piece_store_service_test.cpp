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

  TorrentManifest manifest;
  manifest.torrent_id = "torrent-a";
  manifest.display_name = "demo.bin";
  manifest.file_size = 8;
  manifest.piece_size = 4;
  manifest.piece_count = 2;
  const auto piece0 = bytes_of("ABCD");
  const auto piece1 = bytes_of("EFGH");
  manifest.piece_hashes = {fnv1a(piece0), fnv1a(piece1)};

  const fs::path root = fs::temp_directory_path() / "loopline-piece-store-test";
  fs::remove_all(root);
  {
    PieceStoreService store(root);
    assert(store.store_piece(manifest, 0, piece0) == true);
    assert(store.store_piece(manifest, 1, piece1) == true);
    assert(store.has_piece(manifest, 0) == true);
    assert(store.has_piece(manifest, 1) == true);
    assert(store.has_piece(manifest, 2) == false);

    const auto missing = store.missing_pieces(manifest);
    assert(missing.empty());

    const auto assembled = store.assemble_file(manifest);
    assert(assembled.has_value());
    assert(fs::exists(*assembled));
    std::ifstream assembled_input(*assembled, std::ios::binary);
    std::string assembled_text((std::istreambuf_iterator<char>(assembled_input)), std::istreambuf_iterator<char>());
    assert(assembled_text == "ABCDEFGH");

    const auto stored_root = root / "pieces";
    std::size_t stored_files = 0;
    fs::path stored_piece_path;
    for (const auto& entry : fs::recursive_directory_iterator(stored_root)) {
      if (entry.is_regular_file()) {
        ++stored_files;
        if (entry.path().filename() == "piece-000000.bin") {
          stored_piece_path = entry.path();
        }
        assert(entry.path().filename() == "piece-000000.bin" || entry.path().filename() == "piece-000001.bin");
        assert(entry.path().string().find("..") == std::string::npos);
      }
    }
    assert(stored_files == 2);
    assert(!stored_piece_path.empty());

    {
      std::ofstream corrupt_file(stored_piece_path, std::ios::binary | std::ios::trunc);
      corrupt_file.write("ZZ", 2);
    }
    assert(store.has_piece(manifest, 0) == false);
    assert(!store.assemble_file(manifest).has_value());
    const auto missing_after_corrupt = store.missing_pieces(manifest);
    assert(missing_after_corrupt.size() == 1);
    assert(missing_after_corrupt[0] == 0);
  }

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
    const fs::path out_of_bounds_root = fs::temp_directory_path() / "loopline-piece-store-oob";
    fs::remove_all(out_of_bounds_root);
    PieceStoreService store(out_of_bounds_root);
    assert(store.store_piece(manifest, 2, piece0) == false);
    assert(!fs::exists(out_of_bounds_root / "pieces"));
    fs::remove_all(out_of_bounds_root);
  }

  {
    const fs::path wrong_size_root = fs::temp_directory_path() / "loopline-piece-store-wrong-size";
    fs::remove_all(wrong_size_root);
    PieceStoreService store(wrong_size_root);
    assert(store.store_piece(manifest, 0, bytes_of("ABC")) == false);
    assert(!fs::exists(wrong_size_root / "pieces"));
    fs::remove_all(wrong_size_root);
  }

  {
    const fs::path hash_mismatch_root = fs::temp_directory_path() / "loopline-piece-store-hash-mismatch";
    fs::remove_all(hash_mismatch_root);
    PieceStoreService store(hash_mismatch_root);
    auto corrupted = piece0;
    corrupted[0] = 'Z';
    assert(store.store_piece(manifest, 0, corrupted) == false);
    assert(!fs::exists(hash_mismatch_root / "pieces"));
    fs::remove_all(hash_mismatch_root);
  }

  fs::remove_all(root);
  fs::remove_all(malicious_root);
  return 0;
}
