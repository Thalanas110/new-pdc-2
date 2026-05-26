#include "services/swarm/piece_store_service.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

PieceStoreService::PieceStoreService(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path PieceStoreService::piece_dir(const TorrentManifest& manifest) const {
  return root_ / "pieces" / manifest.torrent_id;
}

std::filesystem::path PieceStoreService::piece_path(const TorrentManifest& manifest, std::uint64_t piece_index) const {
  std::ostringstream name;
  name << "piece-" << std::setw(6) << std::setfill('0') << piece_index << ".bin";
  return piece_dir(manifest) / name.str();
}

bool PieceStoreService::store_piece(const TorrentManifest& manifest,
                                    std::uint64_t piece_index,
                                    const std::vector<char>& bytes) {
  std::filesystem::create_directories(piece_dir(manifest));
  std::ofstream output(piece_path(manifest, piece_index), std::ios::binary);
  if (!output) {
    return false;
  }

  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

bool PieceStoreService::has_piece(const TorrentManifest& manifest, std::uint64_t piece_index) const {
  return std::filesystem::exists(piece_path(manifest, piece_index));
}

std::vector<std::uint64_t> PieceStoreService::missing_pieces(const TorrentManifest& manifest) const {
  std::vector<std::uint64_t> missing;
  for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
    if (!has_piece(manifest, index)) {
      missing.push_back(index);
    }
  }
  return missing;
}

std::optional<std::filesystem::path> PieceStoreService::assemble_file(const TorrentManifest&) {
  return std::nullopt;
}
