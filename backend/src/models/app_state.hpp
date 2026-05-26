#pragma once

#include "core/transfer_core.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct TransferRecord {
  std::string id;
  std::string direction;
  std::string file_name;
  std::string status;
  std::string peer;
  std::string message;
  std::string started_at;
  std::string completed_at;
  std::uint64_t size = 0;
  std::uint64_t bytes_transferred = 0;
};

struct SharedFileProbe {
  std::uint64_t size = 0;
  long long modified_tick = 0;

  bool operator==(const SharedFileProbe& other) const {
    return size == other.size && modified_tick == other.modified_tick;
  }
};

struct SharedFileSignature {
  std::uint64_t size = 0;
  std::uint64_t hash = 0;

  bool operator==(const SharedFileSignature& other) const {
    return size == other.size && hash == other.hash;
  }
};

struct SharedSyncSummary {
  std::size_t peers = 0;
  std::size_t files = 0;
  std::size_t attempted = 0;
  std::size_t synced = 0;
};

struct SwarmPeerRecord {
  std::string node_id;
  std::string host;
  int port = 0;
  std::string source;
  std::string last_seen_at;
  bool reachable = false;
};

struct TorrentLibraryEntry {
  std::string torrent_id;
  std::string display_name;
  std::uint64_t file_size = 0;
  std::uint64_t piece_count = 0;
  std::size_t seeder_count = 0;
  std::size_t leecher_count = 0;
  std::string local_status = "available";
};

struct DownloadSessionRecord {
  std::string torrent_id;
  std::string display_name;
  std::string status;
  std::uint64_t file_size = 0;
  std::uint64_t verified_pieces = 0;
  std::uint64_t piece_count = 0;
  std::vector<std::string> active_peers;
};

class AppState {
 public:
  std::filesystem::path receive_dir;
  std::filesystem::path sent_dir;
  std::filesystem::path shared_dir;
  std::string node_id = transfer::make_transfer_id();
  std::string bind_host = "0.0.0.0";
  std::string advertised_host = "127.0.0.1";
  bool allow_remote_peers = true;
  std::atomic_bool listener_active = false;
  std::atomic_bool running = true;
  int http_port = 8787;
  int transfer_port = 8788;

  std::string status_json() const;
  std::string add_transfer(TransferRecord record);
  void update_transfer(const std::string& id,
                       std::uint64_t bytes_transferred,
                       const std::string& status,
                       const std::string& message);

  std::vector<transfer::PeerEndpoint> sync_peers_snapshot() const;
  std::vector<SwarmPeerRecord> swarm_peers_snapshot() const;
  std::vector<TorrentLibraryEntry> library_snapshot() const;
  std::vector<DownloadSessionRecord> download_sessions_snapshot() const;
  bool has_synced_shared_version(const transfer::PeerEndpoint& peer,
                                 const std::string& file_name,
                                 const SharedFileSignature& signature) const;
  void mark_synced_shared_version(const transfer::PeerEndpoint& peer,
                                  const std::string& file_name,
                                  const SharedFileSignature& signature);

  bool add_sync_peer(const transfer::PeerEndpoint& peer);
  bool remove_sync_peer(const transfer::PeerEndpoint& peer);
  void upsert_swarm_peer(const SwarmPeerRecord& peer);
  void replace_library_entry(const TorrentLibraryEntry& entry);
  void replace_download_session(const DownloadSessionRecord& session);
  std::string library_json() const;
  std::string downloads_json() const;
  std::optional<std::filesystem::path> directory_for_kind(const std::string& kind) const;

 private:
  static std::string make_json_transfer(const TransferRecord& record);
  static std::string synced_shared_key(const transfer::PeerEndpoint& peer, const std::string& file_name);

  mutable std::mutex mutex_;
  std::vector<TransferRecord> transfers_;
  std::vector<transfer::PeerEndpoint> sync_peers_;
  std::vector<SwarmPeerRecord> swarm_peers_;
  std::vector<TorrentLibraryEntry> library_;
  std::vector<DownloadSessionRecord> download_sessions_;
  std::map<std::string, SharedFileSignature> synced_shared_versions_;
};

