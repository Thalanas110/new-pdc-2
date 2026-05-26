#pragma once

#include "services/swarm/torrent_types.hpp"

#include <filesystem>
#include <optional>
#include <string>

class ManifestService {
 public:
  std::optional<TorrentManifest> build_manifest(const std::filesystem::path& file_path,
                                                const std::string& display_name,
                                                const std::string& publisher_node_id) const;
  std::string manifest_json(const TorrentManifest& manifest) const;
};
