#pragma once

#include "services/swarm/torrent_types.hpp"

#include <filesystem>
#include <optional>
#include <vector>

class PieceStoreService {
 public:
  explicit PieceStoreService(std::filesystem::path root);

  bool store_piece(const TorrentManifest& manifest, std::uint64_t piece_index, const std::vector<char>& bytes);
  bool has_piece(const TorrentManifest& manifest, std::uint64_t piece_index) const;
  std::vector<std::uint64_t> missing_pieces(const TorrentManifest& manifest) const;
  std::optional<std::filesystem::path> assemble_file(const TorrentManifest& manifest);

 private:
  std::filesystem::path piece_dir(const TorrentManifest& manifest) const;
  std::filesystem::path piece_path(const TorrentManifest& manifest, std::uint64_t piece_index) const;

  std::filesystem::path root_;
};
