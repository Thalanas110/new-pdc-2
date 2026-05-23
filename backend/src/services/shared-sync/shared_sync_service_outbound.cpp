#include "services/shared-sync/shared_sync_service.hpp"

#include "core/transfer_core.hpp"
#include "services/net-io/net_io.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

bool SharedSyncService::send_shared_delete_to_peer(const transfer::PeerEndpoint& peer, const std::string& file_name) {
  TransferRecord record;
  record.direction = "outgoing";
  record.file_name = file_name;
  record.status = "transferring";
  record.peer = transfer::peer_endpoint_key(peer.host, peer.port);
  record.size = 0;
  record.message = "Removing shared file on peer";
  const std::string id = state_.add_transfer(record);

  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    state_.update_transfer(id, 0, "failed", "Could not connect to sync peer");
    return false;
  }

  const std::string header = "LOOPLINE-SHARED/1\nDELETE\n" + file_name + "\n0\n" + state_.node_id + "\n" +
                             state_.advertised_host + "\n" + std::to_string(state_.transfer_port) + "\n\n";
  if (!netio::send_text(connected, header)) {
    close_socket(connected);
    state_.update_transfer(id, 0, "failed", "Could not send delete message");
    return false;
  }

  close_socket(connected);
  state_.update_transfer(id, 0, "complete", "Shared delete sent to " + record.peer);
  return true;
}

bool SharedSyncService::send_shared_file_to_peer(const transfer::PeerEndpoint& peer,
                                                 const std::string& file_name,
                                                 const std::filesystem::path& file_path,
                                                 const SharedFileSignature& signature,
                                                 bool force_probe) {
  if (!force_probe && state_.has_synced_shared_version(peer, file_name, signature)) {
    return true;
  }

  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    return false;
  }

  const std::uint64_t chunk_size = shared_chunk_size_;
  const std::uint64_t chunk_count = shared_chunk_count(signature.size, chunk_size);

  TransferRecord record;
  record.direction = "outgoing";
  record.file_name = file_name;
  record.status = "transferring";
  record.peer = transfer::peer_endpoint_key(peer.host, peer.port);
  record.size = signature.size;
  record.message = "Syncing shared file";
  const std::string id = state_.add_transfer(record);

  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    state_.update_transfer(id, 0, "failed", "Could not connect to sync peer");
    return false;
  }

  const std::string header = "LOOPLINE-SHARED/2\nPUT-CHUNKS\n" + file_name + "\n" +
                             std::to_string(signature.size) + "\n" + std::to_string(signature.hash) + "\n" +
                             std::to_string(chunk_size) + "\n" + std::to_string(chunk_count) + "\n" +
                             state_.node_id + "\n" + state_.advertised_host + "\n" +
                             std::to_string(state_.transfer_port) + "\n\n";
  if (!netio::send_text(connected, header)) {
    close_socket(connected);
    state_.update_transfer(id, 0, "failed", "Could not send shared header");
    return false;
  }

  const auto requested_chunks = read_missing_chunks_response(connected);
  if (!requested_chunks) {
    close_socket(connected);
    state_.update_transfer(id, 0, "failed", "Peer sent an invalid chunk request");
    return false;
  }

  std::uint64_t sent = 0;
  std::vector<char> buffer(static_cast<std::size_t>(chunk_size));
  for (const std::uint64_t chunk_index : *requested_chunks) {
    if (chunk_index >= chunk_count) {
      close_socket(connected);
      state_.update_transfer(id, sent, "failed", "Peer requested an invalid chunk index");
      return false;
    }

    const std::uint64_t chunk_bytes = shared_chunk_bytes(chunk_index, signature.size, chunk_size, chunk_count);
    if (chunk_bytes == 0) {
      close_socket(connected);
      state_.update_transfer(id, sent, "failed", "Calculated an invalid chunk size");
      return false;
    }

    input.clear();
    input.seekg(static_cast<std::streamoff>(chunk_index * chunk_size), std::ios::beg);
    if (!input) {
      close_socket(connected);
      state_.update_transfer(id, sent, "failed", "Could not seek source shared file");
      return false;
    }

    input.read(buffer.data(), static_cast<std::streamsize>(chunk_bytes));
    if (input.gcount() != static_cast<std::streamsize>(chunk_bytes)) {
      close_socket(connected);
      state_.update_transfer(id, sent, "failed", "Shared file changed while reading chunk");
      return false;
    }

    const std::string chunk_header =
        "CHUNK\n" + std::to_string(chunk_index) + "\n" + std::to_string(chunk_bytes) + "\n";
    if (!netio::send_text(connected, chunk_header) ||
        !netio::send_all(connected, buffer.data(), static_cast<std::size_t>(chunk_bytes))) {
      close_socket(connected);
      state_.update_transfer(id, sent, "failed", "Peer closed shared sync");
      return false;
    }

    sent += chunk_bytes;
    state_.update_transfer(id, sent, "transferring", "Shared chunked " + transfer::format_bytes(sent));
  }

  if (!netio::send_text(connected, "DONE\n\n")) {
    close_socket(connected);
    state_.update_transfer(id, sent, "failed", "Could not finalize shared sync");
    return false;
  }

  close_socket(connected);
  state_.mark_synced_shared_version(peer, file_name, signature);
  if (requested_chunks->empty()) {
    state_.update_transfer(id, signature.size, "complete", "Peer already has all shared chunks");
  } else {
    state_.update_transfer(id, sent, "complete", "Shared with " + record.peer);
  }
  return true;
}

std::map<std::string, SharedFileProbe> SharedSyncService::scan_shared_folder(const std::filesystem::path& directory) const {
  std::map<std::string, SharedFileProbe> files;
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return files;
  }

  std::filesystem::directory_iterator iterator(directory, error);
  const std::filesystem::directory_iterator end;
  while (!error && iterator != end) {
    const auto& entry = *iterator;
    if (entry.is_regular_file(error) && !error) {
      const std::string name = entry.path().filename().string();
      if (name.rfind(".loopline-tmp-", 0) != 0) {
        const auto size = entry.file_size(error);
        const auto modified = entry.last_write_time(error);
        if (!error) {
          files[name] = SharedFileProbe{
              static_cast<std::uint64_t>(size),
              static_cast<long long>(modified.time_since_epoch().count()),
          };
        }
      }
    }
    iterator.increment(error);
  }
  return files;
}

SharedSyncSummary SharedSyncService::sync_shared_folder_once() {
  SharedSyncSummary summary;
  const auto peers = state_.sync_peers_snapshot();
  const auto current_probe = scan_shared_folder(state_.shared_dir);

  summary.peers = peers.size();
  summary.files = current_probe.size();

  for (const auto& [name, probe] : current_probe) {
    (void)probe;
    const std::filesystem::path file_path = state_.shared_dir / name;
    const auto signature = shared_file_signature(file_path);
    if (!signature) {
      continue;
    }

    for (const auto& peer : peers) {
      ++summary.attempted;
      if (send_shared_file_to_peer(peer, name, file_path, *signature, true)) {
        ++summary.synced;
      }
    }
  }

  return summary;
}

std::string SharedSyncService::shared_sync_summary_json(const SharedSyncSummary& summary) {
  std::ostringstream out;
  out << "{\"ok\":true,";
  out << "\"peers\":" << summary.peers << ',';
  out << "\"files\":" << summary.files << ',';
  out << "\"attempted\":" << summary.attempted << ',';
  out << "\"synced\":" << summary.synced << '}';
  return out.str();
}

std::string SharedSyncService::sync_shared_folder_json() {
  return shared_sync_summary_json(sync_shared_folder_once());
}

void SharedSyncService::watch_shared_folder() {
  std::map<std::string, SharedFileProbe> last_probe;
  std::map<std::string, SharedFileSignature> known_versions;
  auto last_reconcile_at = std::chrono::steady_clock::now();
  constexpr auto reconcile_interval = std::chrono::seconds(20);

  while (state_.running.load()) {
    const auto current_probe = scan_shared_folder(state_.shared_dir);
    const auto peers = state_.sync_peers_snapshot();
    const auto now = std::chrono::steady_clock::now();
    const bool reconcile_due = (now - last_reconcile_at) >= reconcile_interval;

    for (const auto& known_version : known_versions) {
      const auto& name = known_version.first;
      if (current_probe.find(name) == current_probe.end()) {
        for (const auto& peer : peers) {
          send_shared_delete_to_peer(peer, name);
        }
      }
    }

    for (auto it = known_versions.begin(); it != known_versions.end();) {
      if (current_probe.find(it->first) == current_probe.end()) {
        it = known_versions.erase(it);
      } else {
        ++it;
      }
    }

    for (const auto& [name, probe] : current_probe) {
      const auto previous_probe = last_probe.find(name);
      if (previous_probe == last_probe.end() || !(previous_probe->second == probe)) {
        last_probe[name] = probe;
        continue;
      }

      const std::filesystem::path file_path = state_.shared_dir / name;
      const auto signature = shared_file_signature(file_path);
      if (!signature) {
        continue;
      }

      const auto known = known_versions.find(name);
      bool changed = false;
      if (known == known_versions.end() || !(known->second == *signature)) {
        known_versions[name] = *signature;
        changed = true;
      }

      if (changed || reconcile_due) {
        for (const auto& peer : peers) {
          send_shared_file_to_peer(peer, name, file_path, *signature, reconcile_due);
        }
      }
    }

    for (auto it = last_probe.begin(); it != last_probe.end();) {
      if (current_probe.find(it->first) == current_probe.end()) {
        it = last_probe.erase(it);
      } else {
        ++it;
      }
    }

    if (reconcile_due) {
      last_reconcile_at = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  }
}

