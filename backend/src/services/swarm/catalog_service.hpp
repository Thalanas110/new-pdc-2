#pragma once

#include "models/app_state.hpp"
#include "services/swarm/torrent_types.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class CatalogService {
 public:
  explicit CatalogService(AppState& state);

  void note_remote_manifest_no_seeder(const TorrentManifest& manifest);
  void publish_local_manifest(const TorrentManifest& manifest,
                              const transfer::PeerEndpoint& local_peer,
                              const std::string& local_status = "seeding");
  void note_remote_manifest(const TorrentManifest& manifest,
                            const std::vector<transfer::PeerEndpoint>& seeder_peers);
  void set_local_status(const std::string& torrent_id, const std::string& local_status);
  void mark_local_completion(const TorrentManifest& manifest, const transfer::PeerEndpoint& local_peer);
  std::optional<TorrentManifest> find_manifest(const std::string& torrent_id) const;
  std::vector<TorrentManifest> manifests_snapshot() const;
  std::vector<transfer::PeerEndpoint> seeders_for(const std::string& torrent_id) const;
  std::vector<TorrentLibraryEntry> library_snapshot() const;
  std::string library_json() const;

 private:
  void sync_library_entry_locked(const std::string& torrent_id);

  AppState& state_;
  mutable std::mutex mutex_;
  std::map<std::string, TorrentManifest> manifests_;
  std::map<std::string, std::map<std::string, transfer::PeerEndpoint>> seeders_;
  std::map<std::string, std::string> local_status_;
};
