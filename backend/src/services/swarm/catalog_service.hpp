#pragma once

#include "models/app_state.hpp"
#include "services/swarm/torrent_types.hpp"

#include <string>
#include <vector>

class CatalogService {
 public:
  explicit CatalogService(AppState& state);

  void publish_local_manifest(const TorrentManifest& manifest);
  std::vector<TorrentLibraryEntry> library_snapshot() const;
  std::string library_json() const;

 private:
  AppState& state_;
};
