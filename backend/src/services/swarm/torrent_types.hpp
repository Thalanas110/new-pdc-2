#pragma once

#include "core/transfer_core.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct TorrentManifest {
  std::string torrent_id;
  std::string display_name;
  std::string publisher_node_id;
  std::uint64_t file_size = 0;
  std::uint64_t piece_size = 256 * 1024;
  std::uint64_t piece_count = 0;
  std::vector<std::uint64_t> piece_hashes;
  std::string created_at;
};

struct CatalogManifestEntry {
  TorrentManifest manifest;
  std::vector<transfer::PeerEndpoint> seeders;
};
