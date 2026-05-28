#include "services/swarm/catalog_service.hpp"

#include "core/transfer_core.hpp"

CatalogService::CatalogService(AppState& state) : state_(state) {}

void CatalogService::sync_library_entry_locked(const std::string& torrent_id) {
  const auto manifest_found = manifests_.find(torrent_id);
  if (manifest_found == manifests_.end()) {
    return;
  }

  TorrentLibraryEntry entry;
  entry.torrent_id = manifest_found->second.torrent_id;
  entry.display_name = manifest_found->second.display_name;
  entry.file_size = manifest_found->second.file_size;
  entry.piece_count = manifest_found->second.piece_count;
  entry.seeder_count = seeders_[torrent_id].size();
  entry.leecher_count = 0;
  const auto status_found = local_status_.find(torrent_id);
  entry.local_status = status_found == local_status_.end() ? "available" : status_found->second;
  state_.replace_library_entry(entry);
}

void CatalogService::publish_local_manifest(const TorrentManifest& manifest,
                                            const transfer::PeerEndpoint& local_peer,
                                            const std::string& local_status) {
  std::lock_guard<std::mutex> lock(mutex_);
  manifests_[manifest.torrent_id] = manifest;
  seeders_[manifest.torrent_id][transfer::peer_endpoint_key(local_peer.host, local_peer.port)] = local_peer;
  local_status_[manifest.torrent_id] = local_status;
  sync_library_entry_locked(manifest.torrent_id);
}

void CatalogService::note_remote_manifest(const TorrentManifest& manifest,
                                          const std::vector<transfer::PeerEndpoint>& seeder_peers) {
  std::lock_guard<std::mutex> lock(mutex_);
  manifests_[manifest.torrent_id] = manifest;
  auto& known_seeders = seeders_[manifest.torrent_id];
  for (const auto& seeder_peer : seeder_peers) {
    known_seeders[transfer::peer_endpoint_key(seeder_peer.host, seeder_peer.port)] = seeder_peer;
  }
  if (local_status_.find(manifest.torrent_id) == local_status_.end()) {
    local_status_[manifest.torrent_id] = "available";
  }
  sync_library_entry_locked(manifest.torrent_id);
}

void CatalogService::set_local_status(const std::string& torrent_id, const std::string& local_status) {
  std::lock_guard<std::mutex> lock(mutex_);
  local_status_[torrent_id] = local_status;
  sync_library_entry_locked(torrent_id);
}

void CatalogService::mark_local_completion(const TorrentManifest& manifest,
                                           const transfer::PeerEndpoint& local_peer) {
  std::lock_guard<std::mutex> lock(mutex_);
  manifests_[manifest.torrent_id] = manifest;
  seeders_[manifest.torrent_id][transfer::peer_endpoint_key(local_peer.host, local_peer.port)] = local_peer;
  local_status_[manifest.torrent_id] = "seeding";
  sync_library_entry_locked(manifest.torrent_id);
}

std::optional<TorrentManifest> CatalogService::find_manifest(const std::string& torrent_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = manifests_.find(torrent_id);
  if (found == manifests_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::vector<TorrentManifest> CatalogService::manifests_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TorrentManifest> manifests;
  manifests.reserve(manifests_.size());
  for (const auto& [torrent_id, manifest] : manifests_) {
    (void)torrent_id;
    manifests.push_back(manifest);
  }
  return manifests;
}

std::vector<transfer::PeerEndpoint> CatalogService::seeders_for(const std::string& torrent_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<transfer::PeerEndpoint> peers;
  const auto found = seeders_.find(torrent_id);
  if (found == seeders_.end()) {
    return peers;
  }
  peers.reserve(found->second.size());
  for (const auto& [peer_key, peer] : found->second) {
    (void)peer_key;
    peers.push_back(peer);
  }
  return peers;
}

std::vector<TorrentLibraryEntry> CatalogService::library_snapshot() const {
  return state_.library_snapshot();
}

std::string CatalogService::library_json() const {
  return state_.library_json();
}
