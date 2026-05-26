#include "services/swarm/catalog_service.hpp"

CatalogService::CatalogService(AppState& state) : state_(state) {}

void CatalogService::publish_local_manifest(const TorrentManifest& manifest) {
  TorrentLibraryEntry entry;
  entry.torrent_id = manifest.torrent_id;
  entry.display_name = manifest.display_name;
  entry.file_size = manifest.file_size;
  entry.piece_count = manifest.piece_count;
  entry.seeder_count = 1;
  entry.leecher_count = 0;
  entry.local_status = "available";
  state_.replace_library_entry(entry);
}

std::vector<TorrentLibraryEntry> CatalogService::library_snapshot() const {
  return state_.library_snapshot();
}

std::string CatalogService::library_json() const {
  return state_.library_json();
}
