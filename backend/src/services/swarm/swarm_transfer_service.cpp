#include "services/swarm/swarm_transfer_service.hpp"

#include "core/transfer_core.hpp"
#include "services/net-io/net_io.hpp"
#include "services/swarm/swarm_protocol.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kSocketChunkSize = 64 * 1024;
constexpr std::size_t kMaxInFlightPerPeer = 2;

std::string sanitize_field(std::string value) {
  for (char& ch : value) {
    if (ch == '\r' || ch == '\n') {
      ch = ' ';
    }
  }
  return value;
}

std::optional<int> parse_int(const std::string& value) {
  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<std::uint64_t> parse_u64(const std::string& value) {
  std::uint64_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

bool read_prefixed_value(socket_t client, const std::string& prefix, std::string& value_out) {
  std::string line;
  if (!netio::read_line(client, line) || line.rfind(prefix, 0) != 0) {
    return false;
  }
  value_out = line.substr(prefix.size());
  return true;
}

void append_manifest_lines(std::ostringstream& out, const TorrentManifest& manifest) {
  out << "TORRENT " << sanitize_field(manifest.torrent_id) << '\n';
  out << "NAME " << sanitize_field(manifest.display_name) << '\n';
  out << "PUBLISHER " << sanitize_field(manifest.publisher_node_id) << '\n';
  out << "FILE_SIZE " << manifest.file_size << '\n';
  out << "PIECE_SIZE " << manifest.piece_size << '\n';
  out << "PIECE_COUNT " << manifest.piece_count << '\n';
  out << "HASH_COUNT " << manifest.piece_hashes.size() << '\n';
  for (const auto piece_hash : manifest.piece_hashes) {
    out << "HASH " << piece_hash << '\n';
  }
  out << "CREATED_AT " << sanitize_field(manifest.created_at) << '\n';
  out << "END\n";
}

std::optional<TorrentManifest> read_manifest_block(socket_t client) {
  TorrentManifest manifest;
  std::size_t expected_hashes = 0;

  while (true) {
    std::string line;
    if (!netio::read_line(client, line)) {
      return std::nullopt;
    }

    if (line == "END") {
      if (manifest.torrent_id.empty() || manifest.piece_count != manifest.piece_hashes.size() ||
          expected_hashes != manifest.piece_hashes.size()) {
        return std::nullopt;
      }
      return manifest;
    }

    if (line.rfind("TORRENT ", 0) == 0) {
      manifest.torrent_id = line.substr(8);
      continue;
    }
    if (line.rfind("NAME ", 0) == 0) {
      manifest.display_name = line.substr(5);
      continue;
    }
    if (line.rfind("PUBLISHER ", 0) == 0) {
      manifest.publisher_node_id = line.substr(10);
      continue;
    }
    if (line.rfind("FILE_SIZE ", 0) == 0) {
      const auto parsed = parse_u64(line.substr(10));
      if (!parsed) {
        return std::nullopt;
      }
      manifest.file_size = *parsed;
      continue;
    }
    if (line.rfind("PIECE_SIZE ", 0) == 0) {
      const auto parsed = parse_u64(line.substr(11));
      if (!parsed) {
        return std::nullopt;
      }
      manifest.piece_size = *parsed;
      continue;
    }
    if (line.rfind("PIECE_COUNT ", 0) == 0) {
      const auto parsed = parse_u64(line.substr(12));
      if (!parsed) {
        return std::nullopt;
      }
      manifest.piece_count = *parsed;
      continue;
    }
    if (line.rfind("HASH_COUNT ", 0) == 0) {
      const auto parsed = parse_u64(line.substr(11));
      if (!parsed) {
        return std::nullopt;
      }
      expected_hashes = static_cast<std::size_t>(*parsed);
      manifest.piece_hashes.clear();
      manifest.piece_hashes.reserve(expected_hashes);
      continue;
    }
    if (line.rfind("HASH ", 0) == 0) {
      const auto parsed = parse_u64(line.substr(5));
      if (!parsed) {
        return std::nullopt;
      }
      manifest.piece_hashes.push_back(*parsed);
      continue;
    }
    if (line.rfind("CREATED_AT ", 0) == 0) {
      manifest.created_at = line.substr(11);
      continue;
    }

    return std::nullopt;
  }
}

std::string bitfield_string(const std::vector<bool>& bitfield) {
  std::string encoded;
  encoded.reserve(bitfield.size());
  for (const bool present : bitfield) {
    encoded.push_back(present ? '1' : '0');
  }
  return encoded;
}

std::optional<std::vector<bool>> parse_bitfield_string(const std::string& encoded, std::size_t expected_size) {
  if (encoded.size() != expected_size) {
    return std::nullopt;
  }

  std::vector<bool> bitfield;
  bitfield.reserve(expected_size);
  for (const char ch : encoded) {
    if (ch == '1') {
      bitfield.push_back(true);
      continue;
    }
    if (ch == '0') {
      bitfield.push_back(false);
      continue;
    }
    return std::nullopt;
  }
  return bitfield;
}

std::string peer_key(const transfer::PeerEndpoint& peer) {
  return transfer::peer_endpoint_key(peer.host, peer.port);
}

bool send_http_json_response(socket_t client,
                             int status,
                             const std::string& status_text,
                             const std::string& body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n";
  response << "Content-Type: application/json\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "Connection: close\r\n";
  response << "Access-Control-Allow-Origin: *\r\n";
  response << "Access-Control-Allow-Headers: Content-Type, X-File-Name\r\n";
  response << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n\r\n";
  response << body;
  return netio::send_text(client, response.str());
}

bool same_endpoint(const transfer::PeerEndpoint& lhs, const transfer::PeerEndpoint& rhs) {
  return lhs.host == rhs.host && lhs.port == rhs.port;
}

}  // namespace

SwarmTransferService::SwarmTransferService(AppState& state,
                                           CatalogService& catalog,
                                           ManifestService& manifest_service,
                                           PieceStoreService& piece_store)
    : state_(state), catalog_(catalog), manifest_service_(manifest_service), piece_store_(piece_store) {}

transfer::PeerEndpoint SwarmTransferService::local_endpoint() const {
  return transfer::PeerEndpoint{state_.advertised_host, state_.transfer_port};
}

void SwarmTransferService::update_download_session(const TorrentManifest& manifest,
                                                   const std::string& status,
                                                   std::uint64_t verified_pieces,
                                                   const std::vector<std::string>& active_peers) const {
  DownloadSessionRecord session;
  session.torrent_id = manifest.torrent_id;
  session.display_name = manifest.display_name;
  session.status = status;
  session.file_size = manifest.file_size;
  session.verified_pieces = verified_pieces;
  session.piece_count = manifest.piece_count;
  session.active_peers = active_peers;
  state_.replace_download_session(session);
}

void SwarmTransferService::transfer_listener() {
  const socket_t listener = netio::create_listener(state_.bind_host, state_.transfer_port);
  if (listener == invalid_socket) {
    return;
  }

  state_.listener_active = true;
  while (state_.running.load()) {
    sockaddr_in peer_address{};
    socket_length_t peer_size = sizeof(peer_address);
    const socket_t client = accept(listener, reinterpret_cast<sockaddr*>(&peer_address), &peer_size);
    if (client == invalid_socket) {
      continue;
    }

    char peer_buffer[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &peer_address.sin_addr, peer_buffer, sizeof(peer_buffer));
    std::thread(&SwarmTransferService::handle_swarm_client, this, client, std::string(peer_buffer)).detach();
  }

  state_.listener_active = false;
  close_socket(listener);
}

bool SwarmTransferService::send_hello_to_peer(const transfer::PeerEndpoint& peer) const {
  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    return false;
  }

  const std::string request =
      SwarmProtocol::encode_hello(state_.node_id, state_.advertised_host, state_.transfer_port) + "\n";
  if (!netio::send_text(connected, request)) {
    close_socket(connected);
    return false;
  }

  std::string magic;
  std::string response;
  const bool ok = netio::read_line(connected, magic) && netio::read_line(connected, response) && magic == "SWARM/1" &&
                  response == "OK";
  close_socket(connected);
  return ok;
}

bool SwarmTransferService::fetch_catalog_from_peer(const transfer::PeerEndpoint& peer) {
  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    return false;
  }

  const bool request_sent = netio::send_text(connected, "SWARM/1\nCATALOG_REQUEST\n\n");
  if (!request_sent) {
    close_socket(connected);
    return false;
  }

  std::string magic;
  std::string command;
  std::string count_line;
  if (!netio::read_line(connected, magic) || !netio::read_line(connected, command) ||
      !netio::read_line(connected, count_line) || magic != "SWARM/1" || command != "CATALOG_RESPONSE" ||
      count_line.rfind("COUNT ", 0) != 0) {
    close_socket(connected);
    return false;
  }

  const auto manifest_count = parse_u64(count_line.substr(6));
  if (!manifest_count) {
    close_socket(connected);
    return false;
  }

  for (std::uint64_t index = 0; index < *manifest_count; ++index) {
    const auto manifest = read_manifest_block(connected);
    if (!manifest) {
      close_socket(connected);
      return false;
    }
    catalog_.note_remote_manifest(*manifest, peer);
  }

  std::string done;
  const bool ok = netio::read_line(connected, done) && done == "DONE";
  close_socket(connected);
  return ok;
}

bool SwarmTransferService::fetch_peers_from_peer(const transfer::PeerEndpoint& peer) {
  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    return false;
  }

  const bool request_sent = netio::send_text(connected, "SWARM/1\nPEERS_REQUEST\n\n");
  if (!request_sent) {
    close_socket(connected);
    return false;
  }

  std::string magic;
  std::string command;
  std::string count_line;
  if (!netio::read_line(connected, magic) || !netio::read_line(connected, command) ||
      !netio::read_line(connected, count_line) || magic != "SWARM/1" || command != "PEERS_RESPONSE" ||
      count_line.rfind("COUNT ", 0) != 0) {
    close_socket(connected);
    return false;
  }

  const auto peer_count = parse_u64(count_line.substr(6));
  if (!peer_count) {
    close_socket(connected);
    return false;
  }

  for (std::uint64_t index = 0; index < *peer_count; ++index) {
    std::string node_id;
    std::string host;
    std::string port_line;
    std::string end;
    if (!read_prefixed_value(connected, "NODE ", node_id) || !read_prefixed_value(connected, "HOST ", host) ||
        !read_prefixed_value(connected, "PORT ", port_line) || !netio::read_line(connected, end) || end != "END") {
      close_socket(connected);
      return false;
    }

    const auto parsed_port = parse_int(port_line);
    if (!parsed_port) {
      close_socket(connected);
      return false;
    }

    const auto discovered = transfer::parse_peer_endpoint(host + ":" + std::to_string(*parsed_port), *parsed_port);
    if (!discovered || same_endpoint(*discovered, local_endpoint())) {
      continue;
    }

    SwarmPeerRecord record;
    record.node_id = node_id;
    record.host = discovered->host;
    record.port = discovered->port;
    record.source = "discovered";
    record.reachable = false;
    state_.upsert_swarm_peer(record);
  }

  std::string done;
  const bool ok = netio::read_line(connected, done) && done == "DONE";
  close_socket(connected);
  return ok;
}

bool SwarmTransferService::bootstrap_peer(const transfer::PeerEndpoint& peer) {
  SwarmPeerRecord record;
  record.node_id = "bootstrap-" + peer_key(peer);
  record.host = peer.host;
  record.port = peer.port;
  record.source = "bootstrap";
  record.reachable = false;
  state_.upsert_swarm_peer(record);

  bool any_ok = false;
  any_ok = send_hello_to_peer(peer) || any_ok;
  any_ok = fetch_peers_from_peer(peer) || any_ok;
  any_ok = fetch_catalog_from_peer(peer) || any_ok;
  record.reachable = any_ok;
  state_.upsert_swarm_peer(record);
  return any_ok;
}

bool SwarmTransferService::store_local_pieces(const TorrentManifest& manifest, const std::vector<char>& body) {
  std::size_t offset = 0;
  for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
    const std::size_t remaining = body.size() - offset;
    const std::size_t piece_bytes =
        static_cast<std::size_t>(std::min<std::uint64_t>(manifest.piece_size, static_cast<std::uint64_t>(remaining)));
    std::vector<char> piece(body.begin() + static_cast<std::ptrdiff_t>(offset),
                            body.begin() + static_cast<std::ptrdiff_t>(offset + piece_bytes));
    if (!piece_store_.store_piece(manifest, index, piece)) {
      return false;
    }
    offset += piece_bytes;
  }
  return offset == body.size();
}

std::optional<TorrentManifest> SwarmTransferService::publish_file(const std::string& file_name,
                                                                  const std::vector<char>& body) {
  const auto temp_root = state_.sent_dir / ".swarm-publish";
  std::error_code error;
  std::filesystem::create_directories(temp_root, error);
  if (error) {
    return std::nullopt;
  }

  const auto temp_path = temp_root / (transfer::make_transfer_id() + "-" + transfer::safe_file_name(file_name));
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return std::nullopt;
    }
    if (!body.empty()) {
      output.write(body.data(), static_cast<std::streamsize>(body.size()));
    }
    if (!output) {
      std::filesystem::remove(temp_path, error);
      return std::nullopt;
    }
  }

  const auto manifest = manifest_service_.build_manifest(temp_path, transfer::safe_file_name(file_name), state_.node_id);
  if (!manifest || !store_local_pieces(*manifest, body)) {
    std::filesystem::remove(temp_path, error);
    return std::nullopt;
  }

  const auto assembled = piece_store_.assemble_file(*manifest);
  std::filesystem::remove(temp_path, error);
  if (!assembled) {
    return std::nullopt;
  }

  catalog_.publish_local_manifest(*manifest, local_endpoint(), "seeding");
  announce_manifest_to_known_peers(*manifest);
  return manifest;
}

void SwarmTransferService::start_download(const TorrentManifest& manifest) {
  update_download_session(manifest, "discovering", 0, {});
  catalog_.set_local_status(manifest.torrent_id, "downloading");
  std::thread(&SwarmTransferService::run_download, this, manifest).detach();
}

void SwarmTransferService::start_download_by_id(const std::string& torrent_id) {
  const auto manifest = catalog_.find_manifest(torrent_id);
  if (!manifest) {
    return;
  }
  start_download(*manifest);
}

bool SwarmTransferService::announce_manifest_to_peer(const transfer::PeerEndpoint& peer,
                                                     const TorrentManifest& manifest) const {
  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    return false;
  }

  std::ostringstream request;
  request << "SWARM/1\nMANIFEST_PUSH\n";
  request << "HOST " << sanitize_field(state_.advertised_host) << '\n';
  request << "PORT " << state_.transfer_port << '\n';
  append_manifest_lines(request, manifest);

  if (!netio::send_text(connected, request.str())) {
    close_socket(connected);
    return false;
  }

  std::string magic;
  std::string response;
  const bool ok = netio::read_line(connected, magic) && netio::read_line(connected, response) && magic == "SWARM/1" &&
                  response == "OK";
  close_socket(connected);
  return ok;
}

void SwarmTransferService::announce_manifest_to_known_peers(const TorrentManifest& manifest,
                                                            const std::string& exclude_peer_key) {
  const auto peers = state_.swarm_peers_snapshot();
  for (const auto& peer_record : peers) {
    const transfer::PeerEndpoint peer{peer_record.host, peer_record.port};
    const auto key = peer_key(peer);
    if (key == exclude_peer_key || same_endpoint(peer, local_endpoint())) {
      continue;
    }
    (void)announce_manifest_to_peer(peer, manifest);
  }
}

std::optional<std::vector<bool>> SwarmTransferService::request_bitfield(const transfer::PeerEndpoint& peer,
                                                                        const TorrentManifest& manifest) const {
  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    return std::nullopt;
  }

  const std::string request =
      "SWARM/1\nBITFIELD_REQUEST\nTORRENT " + sanitize_field(manifest.torrent_id) + "\n\n";
  if (!netio::send_text(connected, request)) {
    close_socket(connected);
    return std::nullopt;
  }

  std::string magic;
  std::string command;
  std::string torrent_line;
  std::string piece_count_line;
  std::string bits_line;
  const bool ok = netio::read_line(connected, magic) && netio::read_line(connected, command) &&
                  read_prefixed_value(connected, "TORRENT ", torrent_line) &&
                  read_prefixed_value(connected, "PIECE_COUNT ", piece_count_line) &&
                  read_prefixed_value(connected, "BITS ", bits_line) && magic == "SWARM/1" &&
                  command == "BITFIELD_RESPONSE" && torrent_line == manifest.torrent_id;
  close_socket(connected);
  if (!ok) {
    return std::nullopt;
  }

  const auto piece_count = parse_u64(piece_count_line);
  if (!piece_count || *piece_count != manifest.piece_count) {
    return std::nullopt;
  }

  return parse_bitfield_string(bits_line, static_cast<std::size_t>(manifest.piece_count));
}

std::optional<std::vector<char>> SwarmTransferService::request_piece(const transfer::PeerEndpoint& peer,
                                                                     const TorrentManifest& manifest,
                                                                     std::uint64_t piece_index) const {
  const socket_t connected = netio::connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    return std::nullopt;
  }

  const std::string request = SwarmProtocol::encode_piece_request(manifest.torrent_id, piece_index) + "\n";
  if (!netio::send_text(connected, request)) {
    close_socket(connected);
    return std::nullopt;
  }

  std::string magic;
  std::string command;
  std::string torrent_line;
  std::string piece_line;
  std::string bytes_line;
  std::string blank;
  if (!netio::read_line(connected, magic) || !netio::read_line(connected, command) ||
      !read_prefixed_value(connected, "TORRENT ", torrent_line) ||
      !read_prefixed_value(connected, "PIECE ", piece_line) || !read_prefixed_value(connected, "BYTES ", bytes_line) ||
      !netio::read_line(connected, blank) || magic != "SWARM/1" || command != "PIECE_RESPONSE" ||
      torrent_line != manifest.torrent_id) {
    close_socket(connected);
    return std::nullopt;
  }

  const auto parsed_piece = parse_u64(piece_line);
  const auto parsed_bytes = parse_u64(bytes_line);
  if (!parsed_piece || !parsed_bytes || *parsed_piece != piece_index) {
    close_socket(connected);
    return std::nullopt;
  }

  std::vector<char> bytes(static_cast<std::size_t>(*parsed_bytes));
  const bool ok = bytes.empty() || netio::recv_exact(connected, bytes.data(), bytes.size());
  close_socket(connected);
  if (!ok) {
    return std::nullopt;
  }

  return bytes;
}

bool SwarmTransferService::sync_receive_file(const TorrentManifest& manifest) const {
  const auto assembled = piece_store_.assemble_file(manifest);
  if (!assembled) {
    return false;
  }

  std::error_code error;
  std::filesystem::create_directories(state_.receive_dir, error);
  if (error) {
    return false;
  }

  const auto destination = state_.receive_dir / transfer::safe_file_name(manifest.display_name);
  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::copy_file(*assembled, destination, std::filesystem::copy_options::overwrite_existing, error);
  return !error;
}

void SwarmTransferService::run_download(const TorrentManifest& manifest) {
  std::vector<bool> have(static_cast<std::size_t>(manifest.piece_count), false);
  std::uint64_t verified = 0;
  for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
    if (piece_store_.has_piece(manifest, index)) {
      have[static_cast<std::size_t>(index)] = true;
      ++verified;
    }
  }

  auto build_missing = [&]() {
    std::vector<std::uint64_t> missing;
    for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
      if (!have[static_cast<std::size_t>(index)]) {
        missing.push_back(index);
      }
    }
    return missing;
  };

  std::map<std::string, int> penalties;
  std::vector<std::string> active_peers;

  auto missing = build_missing();
  update_download_session(manifest, missing.empty() ? "verifying" : "discovering", verified, active_peers);

  while (!missing.empty()) {
    const auto seeders = catalog_.seeders_for(manifest.torrent_id);
    std::vector<PeerPieceAvailability> availabilities;
    std::map<std::string, transfer::PeerEndpoint> endpoints;
    active_peers.clear();

    for (const auto& peer : seeders) {
      if (same_endpoint(peer, local_endpoint())) {
        continue;
      }

      const auto bitfield = request_bitfield(peer, manifest);
      if (!bitfield) {
        penalties[peer_key(peer)] += 1;
        continue;
      }

      const auto key = peer_key(peer);
      active_peers.push_back(key);
      endpoints[key] = peer;
      availabilities.push_back(PeerPieceAvailability{key, *bitfield, penalties[key]});
    }

    if (availabilities.empty()) {
      update_download_session(manifest, "failed", verified, active_peers);
      catalog_.set_local_status(manifest.torrent_id, "failed");
      return;
    }

    update_download_session(manifest, "downloading", verified, active_peers);
    const auto plan = scheduler_.plan_requests(missing, availabilities, kMaxInFlightPerPeer);

    std::size_t planned_pieces = 0;
    for (const auto& [peer_name, pieces] : plan) {
      (void)peer_name;
      planned_pieces += pieces.size();
    }
    if (planned_pieces == 0) {
      update_download_session(manifest, "failed", verified, active_peers);
      catalog_.set_local_status(manifest.torrent_id, "failed");
      return;
    }

    std::mutex state_mutex;
    std::atomic_bool made_progress = false;
    std::vector<std::thread> workers;
    workers.reserve(plan.size());

    for (const auto& [peer_name, pieces] : plan) {
      if (pieces.empty()) {
        continue;
      }

      const auto endpoint_found = endpoints.find(peer_name);
      if (endpoint_found == endpoints.end()) {
        continue;
      }

      workers.emplace_back([&, peer_name, pieces, endpoint = endpoint_found->second]() {
        for (const auto piece_index : pieces) {
          const auto bytes = request_piece(endpoint, manifest, piece_index);
          if (!bytes || !piece_store_.store_piece(manifest, piece_index, *bytes)) {
            std::lock_guard<std::mutex> lock(state_mutex);
            penalties[peer_name] += 1;
            continue;
          }

          std::lock_guard<std::mutex> lock(state_mutex);
          if (!have[static_cast<std::size_t>(piece_index)]) {
            have[static_cast<std::size_t>(piece_index)] = true;
            ++verified;
            made_progress = true;
            update_download_session(manifest, "downloading", verified, active_peers);
          }
        }
      });
    }

    for (auto& worker : workers) {
      worker.join();
    }

    missing = build_missing();
    if (!made_progress.load() && !missing.empty()) {
      update_download_session(manifest, "failed", verified, active_peers);
      catalog_.set_local_status(manifest.torrent_id, "failed");
      return;
    }
  }

  update_download_session(manifest, "verifying", verified, active_peers);
  if (!sync_receive_file(manifest)) {
    update_download_session(manifest, "failed", verified, active_peers);
    catalog_.set_local_status(manifest.torrent_id, "failed");
    return;
  }

  catalog_.mark_local_completion(manifest, local_endpoint());
  announce_manifest_to_known_peers(manifest);
  update_download_session(manifest, "seeding", manifest.piece_count, active_peers);
}

void SwarmTransferService::handle_swarm_client(socket_t client, const std::string& peer_host) {
  std::string magic;
  std::string command;
  if (!netio::read_line(client, magic) || !netio::read_line(client, command) || magic != "SWARM/1") {
    close_socket(client);
    return;
  }

  if (command == "HELLO") {
    std::string node_id;
    std::string host;
    std::string port_line;
    std::string blank;
    if (read_prefixed_value(client, "NODE ", node_id) && read_prefixed_value(client, "HOST ", host) &&
        read_prefixed_value(client, "PORT ", port_line) && netio::read_line(client, blank)) {
      const auto parsed_port = parse_int(port_line);
      const auto peer = parsed_port ? transfer::parse_peer_endpoint(host + ":" + std::to_string(*parsed_port), *parsed_port)
                                    : std::nullopt;
      if (peer) {
        SwarmPeerRecord record;
        record.node_id = node_id;
        record.host = peer->host;
        record.port = peer->port;
        record.source = "discovered";
        record.reachable = true;
        state_.upsert_swarm_peer(record);
      }
      (void)netio::send_text(client, "SWARM/1\nOK\n\n");
    }
    close_socket(client);
    return;
  }

  if (command == "CATALOG_REQUEST") {
    std::string blank;
    if (netio::read_line(client, blank)) {
      const auto manifests = catalog_.manifests_snapshot();
      std::ostringstream response;
      response << "SWARM/1\nCATALOG_RESPONSE\nCOUNT " << manifests.size() << '\n';
      for (const auto& manifest : manifests) {
        append_manifest_lines(response, manifest);
      }
      response << "DONE\n";
      (void)netio::send_text(client, response.str());
    }
    close_socket(client);
    return;
  }

  if (command == "PEERS_REQUEST") {
    std::string blank;
    if (netio::read_line(client, blank)) {
      const auto peers = state_.swarm_peers_snapshot();
      std::ostringstream response;
      response << "SWARM/1\nPEERS_RESPONSE\nCOUNT " << (peers.size() + 1) << '\n';
      response << "NODE " << sanitize_field(state_.node_id) << '\n';
      response << "HOST " << sanitize_field(state_.advertised_host) << '\n';
      response << "PORT " << state_.transfer_port << '\n';
      response << "END\n";
      for (const auto& peer : peers) {
        response << "NODE " << sanitize_field(peer.node_id) << '\n';
        response << "HOST " << sanitize_field(peer.host) << '\n';
        response << "PORT " << peer.port << '\n';
        response << "END\n";
      }
      response << "DONE\n";
      (void)netio::send_text(client, response.str());
    }
    close_socket(client);
    return;
  }

  if (command == "MANIFEST_PUSH") {
    std::string host;
    std::string port_line;
    if (!read_prefixed_value(client, "HOST ", host) || !read_prefixed_value(client, "PORT ", port_line)) {
      close_socket(client);
      return;
    }

    const auto manifest = read_manifest_block(client);
    const auto parsed_port = parse_int(port_line);
    const auto source_peer =
        parsed_port ? transfer::parse_peer_endpoint(host + ":" + std::to_string(*parsed_port), *parsed_port)
                    : std::nullopt;
    if (manifest && source_peer) {
      catalog_.note_remote_manifest(*manifest, *source_peer);
      SwarmPeerRecord record;
      record.node_id = manifest->publisher_node_id;
      record.host = source_peer->host;
      record.port = source_peer->port;
      record.source = "discovered";
      record.reachable = true;
      state_.upsert_swarm_peer(record);
      (void)netio::send_text(client, "SWARM/1\nOK\n\n");
    }
    close_socket(client);
    return;
  }

  if (command == "BITFIELD_REQUEST") {
    std::string torrent_id;
    std::string blank;
    if (!read_prefixed_value(client, "TORRENT ", torrent_id) || !netio::read_line(client, blank)) {
      close_socket(client);
      return;
    }

    const auto manifest = catalog_.find_manifest(torrent_id);
    if (!manifest) {
      close_socket(client);
      return;
    }

    std::vector<bool> bitfield(static_cast<std::size_t>(manifest->piece_count), false);
    for (std::uint64_t index = 0; index < manifest->piece_count; ++index) {
      bitfield[static_cast<std::size_t>(index)] = piece_store_.has_piece(*manifest, index);
    }

    std::ostringstream response;
    response << "SWARM/1\nBITFIELD_RESPONSE\n";
    response << "TORRENT " << sanitize_field(manifest->torrent_id) << '\n';
    response << "PIECE_COUNT " << manifest->piece_count << '\n';
    response << "BITS " << bitfield_string(bitfield) << '\n';
    (void)netio::send_text(client, response.str());
    close_socket(client);
    return;
  }

  if (command == "PIECE_REQUEST") {
    std::string torrent_id;
    std::string piece_line;
    std::string blank;
    if (!read_prefixed_value(client, "TORRENT ", torrent_id) || !read_prefixed_value(client, "PIECE ", piece_line) ||
        !netio::read_line(client, blank)) {
      close_socket(client);
      return;
    }

    const auto manifest = catalog_.find_manifest(torrent_id);
    const auto piece_index = parse_u64(piece_line);
    if (!manifest || !piece_index) {
      close_socket(client);
      return;
    }

    const auto piece = piece_store_.load_piece(*manifest, *piece_index);
    if (!piece) {
      close_socket(client);
      return;
    }

    std::ostringstream response;
    response << "SWARM/1\nPIECE_RESPONSE\n";
    response << "TORRENT " << sanitize_field(manifest->torrent_id) << '\n';
    response << "PIECE " << *piece_index << '\n';
    response << "BYTES " << piece->size() << "\n\n";
    if (!netio::send_text(client, response.str())) {
      close_socket(client);
      return;
    }
    if (!piece->empty()) {
      (void)netio::send_all(client, piece->data(), piece->size());
    }
    close_socket(client);
    return;
  }

  (void)peer_host;
  close_socket(client);
}

void SwarmTransferService::publish_from_http(socket_t client,
                                             const std::string& file_name,
                                             const std::vector<char>& body) {
  const auto manifest = publish_file(file_name, body);
  if (!manifest) {
    (void)send_http_json_response(client,
                                  500,
                                  "Internal Server Error",
                                  "{\"ok\":false,\"error\":\"Could not publish torrent\"}");
    return;
  }

  const std::string response = "{\"ok\":true,\"torrentId\":\"" + transfer::json_escape(manifest->torrent_id) + "\"}";
  (void)send_http_json_response(client, 200, "OK", response);
}

std::string SwarmTransferService::downloads_json() const {
  return state_.downloads_json();
}
