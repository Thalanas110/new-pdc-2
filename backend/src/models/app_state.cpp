#include "models/app_state.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

std::string now_stamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#ifdef _WIN32
  localtime_s(&local_time, &time);
#else
  localtime_r(&time, &local_time);
#endif

  std::ostringstream out;
  out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

bool is_bootstrap_peer(const SwarmPeerRecord& peer) {
  return peer.source == "bootstrap";
}

}  // namespace

std::string AppState::make_json_transfer(const TransferRecord& record) {
  std::ostringstream out;
  out << "{\"id\":\"" << transfer::json_escape(record.id) << "\",";
  out << "\"direction\":\"" << transfer::json_escape(record.direction) << "\",";
  out << "\"fileName\":\"" << transfer::json_escape(record.file_name) << "\",";
  out << "\"status\":\"" << transfer::json_escape(record.status) << "\",";
  out << "\"peer\":\"" << transfer::json_escape(record.peer) << "\",";
  out << "\"size\":" << record.size << ',';
  out << "\"bytesTransferred\":" << record.bytes_transferred << ',';
  out << "\"message\":\"" << transfer::json_escape(record.message) << "\",";
  out << "\"startedAt\":\"" << transfer::json_escape(record.started_at) << "\",";
  out << "\"completedAt\":\"" << transfer::json_escape(record.completed_at) << "\"}";
  return out.str();
}

std::string make_json_swarm_peer(const SwarmPeerRecord& peer) {
  std::ostringstream out;
  out << "{\"nodeId\":\"" << transfer::json_escape(peer.node_id) << "\",";
  out << "\"host\":\"" << transfer::json_escape(peer.host) << "\",";
  out << "\"port\":" << peer.port << ',';
  out << "\"source\":\"" << transfer::json_escape(peer.source) << "\",";
  out << "\"lastSeenAt\":\"" << transfer::json_escape(peer.last_seen_at) << "\",";
  out << "\"reachable\":" << (peer.reachable ? "true" : "false") << '}';
  return out.str();
}

std::string make_json_library_entry(const TorrentLibraryEntry& entry) {
  std::ostringstream out;
  out << "{\"torrentId\":\"" << transfer::json_escape(entry.torrent_id) << "\",";
  out << "\"displayName\":\"" << transfer::json_escape(entry.display_name) << "\",";
  out << "\"fileSize\":" << entry.file_size << ',';
  out << "\"pieceCount\":" << entry.piece_count << ',';
  out << "\"seederCount\":" << entry.seeder_count << ',';
  out << "\"leecherCount\":" << entry.leecher_count << ',';
  out << "\"localStatus\":\"" << transfer::json_escape(entry.local_status) << "\"}";
  return out.str();
}

std::string make_json_download_session(const DownloadSessionRecord& session) {
  std::ostringstream out;
  out << "{\"torrentId\":\"" << transfer::json_escape(session.torrent_id) << "\",";
  out << "\"displayName\":\"" << transfer::json_escape(session.display_name) << "\",";
  out << "\"status\":\"" << transfer::json_escape(session.status) << "\",";
  out << "\"fileSize\":" << session.file_size << ',';
  out << "\"verifiedPieces\":" << session.verified_pieces << ',';
  out << "\"pieceCount\":" << session.piece_count << ',';
  out << "\"activePeers\":[";
  for (std::size_t index = 0; index < session.active_peers.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << '"' << transfer::json_escape(session.active_peers[index]) << '"';
  }
  out << "]}";
  return out.str();
}

std::string AppState::status_json() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ostringstream out;
  out << "{\"nodeId\":\"" << transfer::json_escape(node_id) << "\",";
  out << "\"host\":\"" << transfer::json_escape(advertised_host) << "\",";
  out << "\"bindHost\":\"" << transfer::json_escape(bind_host) << "\",";
  out << "\"advertisedHost\":\"" << transfer::json_escape(advertised_host) << "\",";
  out << "\"allowRemotePeers\":" << (allow_remote_peers ? "true" : "false") << ',';
  out << "\"httpPort\":" << http_port << ',';
  out << "\"transferPort\":" << transfer_port << ',';
  out << "\"receiveDir\":\"" << transfer::json_escape(receive_dir.string()) << "\",";
  out << "\"sentDir\":\"" << transfer::json_escape(sent_dir.string()) << "\",";
  out << "\"sharedDir\":\"" << transfer::json_escape(shared_dir.string()) << "\",";
  out << "\"listenerActive\":" << (listener_active.load() ? "true" : "false") << ',';
  out << "\"syncPeers\":[";
  for (std::size_t index = 0; index < sync_peers_.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << "{\"host\":\"" << transfer::json_escape(sync_peers_[index].host) << "\",";
    out << "\"port\":" << sync_peers_[index].port << '}';
  }
  out << "],";
  out << "\"transfers\":[";
  for (std::size_t index = 0; index < transfers_.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << make_json_transfer(transfers_[index]);
  }
  out << "]}";
  return out.str();
}

std::string AppState::add_transfer(TransferRecord record) {
  std::lock_guard<std::mutex> lock(mutex_);

  record.id = transfer::make_transfer_id();
  record.started_at = now_stamp();
  transfers_.insert(transfers_.begin(), record);
  if (transfers_.size() > 12) {
    transfers_.resize(12);
  }
  return transfers_.front().id;
}

void AppState::update_transfer(const std::string& id,
                               std::uint64_t bytes_transferred,
                               const std::string& status,
                               const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (TransferRecord& record : transfers_) {
    if (record.id == id) {
      record.bytes_transferred = bytes_transferred;
      record.status = status;
      record.message = message;
      if (status == "complete" || status == "failed") {
        record.completed_at = now_stamp();
      }
      return;
    }
  }
}

std::string AppState::synced_shared_key(const transfer::PeerEndpoint& peer, const std::string& file_name) {
  return transfer::peer_endpoint_key(peer.host, peer.port) + "|" + file_name;
}

std::vector<transfer::PeerEndpoint> AppState::sync_peers_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sync_peers_;
}

std::vector<SwarmPeerRecord> AppState::swarm_peers_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return swarm_peers_;
}

std::vector<TorrentLibraryEntry> AppState::library_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return library_;
}

std::vector<DownloadSessionRecord> AppState::download_sessions_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return download_sessions_;
}

bool AppState::has_synced_shared_version(const transfer::PeerEndpoint& peer,
                                         const std::string& file_name,
                                         const SharedFileSignature& signature) const {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto found = synced_shared_versions_.find(synced_shared_key(peer, file_name));
  return found != synced_shared_versions_.end() && found->second == signature;
}

void AppState::mark_synced_shared_version(const transfer::PeerEndpoint& peer,
                                          const std::string& file_name,
                                          const SharedFileSignature& signature) {
  std::lock_guard<std::mutex> lock(mutex_);
  synced_shared_versions_[synced_shared_key(peer, file_name)] = signature;
}

bool AppState::add_sync_peer(const transfer::PeerEndpoint& peer) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto key = transfer::peer_endpoint_key(peer.host, peer.port);
  const auto exists = std::find_if(sync_peers_.begin(), sync_peers_.end(), [&](const auto& existing) {
    return transfer::peer_endpoint_key(existing.host, existing.port) == key;
  });
  if (exists != sync_peers_.end()) {
    return false;
  }

  sync_peers_.push_back(peer);
  return true;
}

bool AppState::remove_sync_peer(const transfer::PeerEndpoint& peer) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto key = transfer::peer_endpoint_key(peer.host, peer.port);
  const auto old_size = sync_peers_.size();
  sync_peers_.erase(std::remove_if(sync_peers_.begin(), sync_peers_.end(), [&](const auto& existing) {
                     return transfer::peer_endpoint_key(existing.host, existing.port) == key;
                   }),
                   sync_peers_.end());
  return sync_peers_.size() != old_size;
}

void AppState::upsert_swarm_peer(const SwarmPeerRecord& peer) {
  std::lock_guard<std::mutex> lock(mutex_);

  SwarmPeerRecord updated = peer;
  updated.last_seen_at = now_stamp();

  const auto matches_peer = [&](const SwarmPeerRecord& existing) {
    if (!updated.node_id.empty() && existing.node_id == updated.node_id) {
      return true;
    }
    return existing.host == updated.host && existing.port == updated.port;
  };

  const auto found = std::find_if(swarm_peers_.begin(), swarm_peers_.end(), matches_peer);
  if (found != swarm_peers_.end()) {
    if (!is_bootstrap_peer(*found) && is_bootstrap_peer(updated)) {
      updated.node_id = found->node_id;
      updated.source = found->source;
    }
    *found = updated;
    return;
  }

  swarm_peers_.push_back(updated);
}

void AppState::replace_library_entry(const TorrentLibraryEntry& entry) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto found = std::find_if(library_.begin(), library_.end(), [&](const auto& existing) {
    return existing.torrent_id == entry.torrent_id;
  });
  if (found != library_.end()) {
    *found = entry;
    return;
  }

  library_.push_back(entry);
}

void AppState::replace_download_session(const DownloadSessionRecord& session) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto found = std::find_if(download_sessions_.begin(),
                                  download_sessions_.end(),
                                  [&](const auto& existing) {
                                    return existing.torrent_id == session.torrent_id;
                                  });
  if (found != download_sessions_.end()) {
    *found = session;
    return;
  }

  download_sessions_.push_back(session);
}

std::string AppState::library_json() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ostringstream out;
  out << '[';
  for (std::size_t index = 0; index < library_.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << make_json_library_entry(library_[index]);
  }
  out << ']';
  return out.str();
}

std::string AppState::downloads_json() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ostringstream out;
  out << '[';
  for (std::size_t index = 0; index < download_sessions_.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << make_json_download_session(download_sessions_[index]);
  }
  out << ']';
  return out.str();
}

std::optional<std::filesystem::path> AppState::directory_for_kind(const std::string& kind) const {
  if (kind == "received") {
    return receive_dir;
  }
  if (kind == "sent") {
    return sent_dir;
  }
  if (kind == "shared") {
    return shared_dir;
  }
  return std::nullopt;
}

