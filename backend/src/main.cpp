#include "transfer_core.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

#ifdef _WIN32
using socket_t = SOCKET;
using socket_length_t = int;
constexpr socket_t invalid_socket = INVALID_SOCKET;
void close_socket(socket_t socket) { closesocket(socket); }
#else
using socket_t = int;
using socket_length_t = socklen_t;
constexpr socket_t invalid_socket = -1;
void close_socket(socket_t socket) { close(socket); }
#endif

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

struct AppState {
  std::mutex mutex;
  std::vector<TransferRecord> transfers;
  std::filesystem::path receive_dir;
  std::filesystem::path sent_dir;
  std::filesystem::path shared_dir;
  std::vector<transfer::PeerEndpoint> sync_peers;
  std::map<std::string, SharedFileSignature> synced_shared_versions;
  std::string node_id = transfer::make_transfer_id();
  std::string bind_host = "127.0.0.1";
  std::string advertised_host = "127.0.0.1";
  bool allow_remote_peers = false;
  std::atomic_bool listener_active = false;
  std::atomic_bool running = true;
  int http_port = 8787;
  int transfer_port = 8788;
};

struct HttpRequest {
  std::string method;
  std::string target;
  std::map<std::string, std::string> headers;
  std::vector<char> body;
};

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

std::string lower_copy(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

std::uint64_t fnv1a_update(std::uint64_t hash, const char* data, std::size_t size) {
  constexpr std::uint64_t prime = 1099511628211ull;
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= static_cast<unsigned char>(data[index]);
    hash *= prime;
  }
  return hash;
}

std::optional<std::uint64_t> hash_file(const std::filesystem::path& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  std::uint64_t hash = 14695981039346656037ull;
  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = input.gcount();
    if (read > 0) {
      hash = fnv1a_update(hash, buffer.data(), static_cast<std::size_t>(read));
    }
  }
  return hash;
}

std::optional<SharedFileSignature> shared_file_signature(const std::filesystem::path& file_path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(file_path, error);
  if (error) {
    return std::nullopt;
  }

  const auto hash = hash_file(file_path);
  if (!hash) {
    return std::nullopt;
  }

  return SharedFileSignature{static_cast<std::uint64_t>(size), *hash};
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

std::string env_value(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || std::strlen(value) == 0) {
    return fallback;
  }
  return value;
}

bool env_flag(const char* name, bool fallback) {
  const std::string value = lower_copy(env_value(name, fallback ? "1" : "0"));
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string header_value(const HttpRequest& request, const std::string& name, const std::string& fallback = "") {
  const auto found = request.headers.find(lower_copy(name));
  if (found == request.headers.end()) {
    return fallback;
  }
  return found->second;
}

bool send_all(socket_t socket, const char* data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(size - sent, 64 * 1024));
    const int result = send(socket, data + sent, chunk, 0);
    if (result <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

bool send_text(socket_t socket, const std::string& text) {
  return send_all(socket, text.data(), text.size());
}

std::string url_decode(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    if (input[index] == '%' && index + 2 < input.size()) {
      const std::string hex = input.substr(index + 1, 2);
      int value = 0;
      std::istringstream(hex) >> std::hex >> value;
      output.push_back(static_cast<char>(value));
      index += 2;
    } else if (input[index] == '+') {
      output.push_back(' ');
    } else {
      output.push_back(input[index]);
    }
  }
  return output;
}

std::string url_encode(const std::string& input) {
  std::ostringstream output;
  output << std::hex << std::uppercase;
  for (const unsigned char ch : input) {
    const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                      (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                      ch == '.' || ch == '~';
    if (safe) {
      output << static_cast<char>(ch);
    } else {
      output << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return output.str();
}

std::string query_value(const std::string& target, const std::string& key) {
  const auto question = target.find('?');
  if (question == std::string::npos) {
    return "";
  }

  const std::string query = target.substr(question + 1);
  std::size_t start = 0;
  while (start <= query.size()) {
    const auto amp = query.find('&', start);
    const std::string pair = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
    const auto equals = pair.find('=');
    const std::string name = url_decode(pair.substr(0, equals));
    const std::string value = equals == std::string::npos ? "" : url_decode(pair.substr(equals + 1));
    if (name == key) {
      return value;
    }
    if (amp == std::string::npos) {
      break;
    }
    start = amp + 1;
  }

  return "";
}

std::string path_only(const std::string& target) {
  const auto question = target.find('?');
  return question == std::string::npos ? target : target.substr(0, question);
}

std::string make_json_transfer(const TransferRecord& record) {
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

std::string status_json(AppState& state) {
  std::lock_guard<std::mutex> lock(state.mutex);
  std::ostringstream out;
  out << "{\"nodeId\":\"" << transfer::json_escape(state.node_id) << "\",";
  out << "\"host\":\"" << transfer::json_escape(state.advertised_host) << "\",";
  out << "\"bindHost\":\"" << transfer::json_escape(state.bind_host) << "\",";
  out << "\"advertisedHost\":\"" << transfer::json_escape(state.advertised_host) << "\",";
  out << "\"allowRemotePeers\":" << (state.allow_remote_peers ? "true" : "false") << ',';
  out << "\"httpPort\":" << state.http_port << ',';
  out << "\"transferPort\":" << state.transfer_port << ',';
  out << "\"receiveDir\":\"" << transfer::json_escape(state.receive_dir.string()) << "\",";
  out << "\"sentDir\":\"" << transfer::json_escape(state.sent_dir.string()) << "\",";
  out << "\"sharedDir\":\"" << transfer::json_escape(state.shared_dir.string()) << "\",";
  out << "\"listenerActive\":" << (state.listener_active.load() ? "true" : "false") << ',';
  out << "\"syncPeers\":[";
  for (std::size_t index = 0; index < state.sync_peers.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << "{\"host\":\"" << transfer::json_escape(state.sync_peers[index].host) << "\",";
    out << "\"port\":" << state.sync_peers[index].port << '}';
  }
  out << "],";
  out << "\"transfers\":[";
  for (std::size_t index = 0; index < state.transfers.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << make_json_transfer(state.transfers[index]);
  }
  out << "]}";
  return out.str();
}

std::string add_transfer(AppState& state, TransferRecord record) {
  std::lock_guard<std::mutex> lock(state.mutex);
  record.id = transfer::make_transfer_id();
  record.started_at = now_stamp();
  state.transfers.insert(state.transfers.begin(), record);
  if (state.transfers.size() > 12) {
    state.transfers.resize(12);
  }
  return state.transfers.front().id;
}

void update_transfer(AppState& state,
                     const std::string& id,
                     std::uint64_t bytes_transferred,
                     const std::string& status,
                     const std::string& message) {
  std::lock_guard<std::mutex> lock(state.mutex);
  for (TransferRecord& record : state.transfers) {
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

std::string synced_shared_key(const transfer::PeerEndpoint& peer, const std::string& file_name) {
  return transfer::peer_endpoint_key(peer.host, peer.port) + "|" + file_name;
}

std::vector<transfer::PeerEndpoint> sync_peers_snapshot(AppState& state) {
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.sync_peers;
}

bool has_synced_shared_version(AppState& state,
                               const transfer::PeerEndpoint& peer,
                               const std::string& file_name,
                               const SharedFileSignature& signature) {
  std::lock_guard<std::mutex> lock(state.mutex);
  const auto found = state.synced_shared_versions.find(synced_shared_key(peer, file_name));
  return found != state.synced_shared_versions.end() && found->second == signature;
}

void mark_synced_shared_version(AppState& state,
                                const transfer::PeerEndpoint& peer,
                                const std::string& file_name,
                                const SharedFileSignature& signature) {
  std::lock_guard<std::mutex> lock(state.mutex);
  state.synced_shared_versions[synced_shared_key(peer, file_name)] = signature;
}

bool add_sync_peer(AppState& state, const transfer::PeerEndpoint& peer) {
  std::lock_guard<std::mutex> lock(state.mutex);
  const auto key = transfer::peer_endpoint_key(peer.host, peer.port);
  const auto exists = std::find_if(state.sync_peers.begin(), state.sync_peers.end(), [&](const auto& existing) {
    return transfer::peer_endpoint_key(existing.host, existing.port) == key;
  });
  if (exists != state.sync_peers.end()) {
    return false;
  }
  state.sync_peers.push_back(peer);
  return true;
}

bool remove_sync_peer(AppState& state, const transfer::PeerEndpoint& peer) {
  std::lock_guard<std::mutex> lock(state.mutex);
  const auto key = transfer::peer_endpoint_key(peer.host, peer.port);
  const auto old_size = state.sync_peers.size();
  state.sync_peers.erase(std::remove_if(state.sync_peers.begin(), state.sync_peers.end(), [&](const auto& existing) {
                           return transfer::peer_endpoint_key(existing.host, existing.port) == key;
                         }),
                         state.sync_peers.end());
  return state.sync_peers.size() != old_size;
}

std::filesystem::path unique_received_path(const std::filesystem::path& directory, const std::string& file_name) {
  std::filesystem::path candidate = directory / file_name;
  if (!std::filesystem::exists(candidate)) {
    return candidate;
  }

  const std::filesystem::path stem = candidate.stem();
  const std::filesystem::path extension = candidate.extension();
  for (int index = 1; index < 1000; ++index) {
    std::ostringstream name;
    name << stem.string() << " (" << index << ")" << extension.string();
    candidate = directory / name.str();
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }

  return directory / (transfer::make_transfer_id() + "-" + file_name);
}

std::optional<std::filesystem::path> directory_for_kind(AppState& state, const std::string& kind) {
  if (kind == "received") {
    return state.receive_dir;
  }
  if (kind == "sent") {
    return state.sent_dir;
  }
  if (kind == "shared") {
    return state.shared_dir;
  }
  return std::nullopt;
}

std::string file_list_json(AppState& state, const std::string& kind) {
  const auto directory = directory_for_kind(state, kind);
  if (!directory) {
    return "{\"ok\":false,\"error\":\"Invalid file kind\"}";
  }

  std::filesystem::create_directories(*directory);
  std::vector<std::filesystem::directory_entry> entries;
  for (const auto& entry : std::filesystem::directory_iterator(*directory)) {
    if (entry.is_regular_file()) {
      entries.push_back(entry);
    }
  }

  std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
    return left.last_write_time() > right.last_write_time();
  });

  std::ostringstream out;
  out << "{\"ok\":true,\"kind\":\"" << transfer::json_escape(kind) << "\",\"files\":[";
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    const std::string name = entry.path().filename().string();
    if (index > 0) {
      out << ',';
    }
    out << "{\"kind\":\"" << transfer::json_escape(kind) << "\",";
    out << "\"name\":\"" << transfer::json_escape(name) << "\",";
    out << "\"size\":" << entry.file_size() << ',';
    out << "\"modifiedAt\":\"" << entry.last_write_time().time_since_epoch().count() << "\",";
    out << "\"contentType\":\"" << transfer::json_escape(transfer::content_type_for_file(name)) << "\",";
    out << "\"url\":\"/api/files/open?kind=" << transfer::json_escape(kind)
        << "&name=" << transfer::json_escape(url_encode(name)) << "\"}";
  }
  out << "]}";
  return out.str();
}

socket_t create_listener(const std::string& bind_host, int port) {
  const socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == invalid_socket) {
    return invalid_socket;
  }

  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  if (bind_host == "0.0.0.0") {
    address.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (bind_host == "localhost") {
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
  } else {
    address.sin_addr.s_addr = inet_addr(bind_host.c_str());
  }

  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close_socket(listener);
    return invalid_socket;
  }

  if (listen(listener, SOMAXCONN) != 0) {
    close_socket(listener);
    return invalid_socket;
  }

  return listener;
}

socket_t connect_to_peer(const std::string& host, int port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* results = nullptr;
  const std::string port_value = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_value.c_str(), &hints, &results) != 0) {
    return invalid_socket;
  }

  socket_t connected = invalid_socket;
  for (addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
    socket_t peer = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (peer == invalid_socket) {
      continue;
    }
    if (connect(peer, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
      connected = peer;
      break;
    }
    close_socket(peer);
  }

  freeaddrinfo(results);
  return connected;
}

bool send_shared_delete_to_peer(AppState& state, const transfer::PeerEndpoint& peer, const std::string& file_name) {
  TransferRecord record;
  record.direction = "outgoing";
  record.file_name = file_name;
  record.status = "transferring";
  record.peer = transfer::peer_endpoint_key(peer.host, peer.port);
  record.size = 0;
  record.message = "Removing shared file on peer";
  const std::string id = add_transfer(state, record);

  const socket_t connected = connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    update_transfer(state, id, 0, "failed", "Could not connect to sync peer");
    return false;
  }

  const std::string header = "LOOPLINE-SHARED/1\nDELETE\n" + file_name + "\n0\n" + state.node_id + "\n" +
                             state.advertised_host + "\n" + std::to_string(state.transfer_port) + "\n\n";
  if (!send_text(connected, header)) {
    close_socket(connected);
    update_transfer(state, id, 0, "failed", "Could not send delete message");
    return false;
  }

  close_socket(connected);
  update_transfer(state, id, 0, "complete", "Shared delete sent to " + record.peer);
  return true;
}

bool send_shared_file_to_peer(AppState& state,
                              const transfer::PeerEndpoint& peer,
                              const std::string& file_name,
                              const std::filesystem::path& file_path,
                              const SharedFileSignature& signature) {
  if (has_synced_shared_version(state, peer, file_name, signature)) {
    return true;
  }

  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    return false;
  }

  TransferRecord record;
  record.direction = "outgoing";
  record.file_name = file_name;
  record.status = "transferring";
  record.peer = transfer::peer_endpoint_key(peer.host, peer.port);
  record.size = signature.size;
  record.message = "Syncing shared file";
  const std::string id = add_transfer(state, record);

  const socket_t connected = connect_to_peer(peer.host, peer.port);
  if (connected == invalid_socket) {
    update_transfer(state, id, 0, "failed", "Could not connect to sync peer");
    return false;
  }

  const std::string header = "LOOPLINE-SHARED/1\nPUT\n" + file_name + "\n" + std::to_string(signature.size) +
                             "\n" + state.node_id + "\n" + state.advertised_host + "\n" +
                             std::to_string(state.transfer_port) + "\n\n";
  if (!send_text(connected, header)) {
    close_socket(connected);
    update_transfer(state, id, 0, "failed", "Could not send shared header");
    return false;
  }

  std::uint64_t sent = 0;
  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = input.gcount();
    if (read <= 0) {
      break;
    }
    if (!send_all(connected, buffer.data(), static_cast<std::size_t>(read))) {
      close_socket(connected);
      update_transfer(state, id, sent, "failed", "Peer closed shared sync");
      return false;
    }
    sent += static_cast<std::uint64_t>(read);
    update_transfer(state, id, sent, "transferring", "Shared " + transfer::format_bytes(sent));
  }

  if (sent != signature.size) {
    close_socket(connected);
    update_transfer(state, id, sent, "failed", "Shared file changed while sending");
    return false;
  }

  close_socket(connected);
  mark_synced_shared_version(state, peer, file_name, signature);
  update_transfer(state, id, sent, "complete", "Shared with " + record.peer);
  return true;
}

std::map<std::string, SharedFileProbe> scan_shared_folder(const std::filesystem::path& directory) {
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

void shared_folder_watcher(AppState& state) {
  std::map<std::string, SharedFileProbe> last_probe;
  std::map<std::string, SharedFileSignature> known_versions;

  while (state.running.load()) {
    const auto current_probe = scan_shared_folder(state.shared_dir);
    const auto peers = sync_peers_snapshot(state);

    for (const auto& known_version : known_versions) {
      const auto& name = known_version.first;
      if (current_probe.find(name) == current_probe.end()) {
        for (const auto& peer : peers) {
          send_shared_delete_to_peer(state, peer, name);
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

      const std::filesystem::path file_path = state.shared_dir / name;
      const auto signature = shared_file_signature(file_path);
      if (!signature) {
        continue;
      }

      const auto known = known_versions.find(name);
      if (known == known_versions.end() || !(known->second == *signature)) {
        known_versions[name] = *signature;
      }

      for (const auto& peer : peers) {
        send_shared_file_to_peer(state, peer, name, file_path, *signature);
      }
    }

    for (auto it = last_probe.begin(); it != last_probe.end();) {
      if (current_probe.find(it->first) == current_probe.end()) {
        it = last_probe.erase(it);
      } else {
        ++it;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  }
}

bool read_line(socket_t socket, std::string& line) {
  line.clear();
  char ch = '\0';
  while (line.size() < 4096) {
    const int received = recv(socket, &ch, 1, 0);
    if (received <= 0) {
      return false;
    }
    if (ch == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      return true;
    }
    line.push_back(ch);
  }
  return false;
}

std::optional<transfer::PeerEndpoint> source_peer_from_header(const std::string& host,
                                                              const std::string& port,
                                                              int fallback_port) {
  if (host.empty()) {
    return std::nullopt;
  }
  const std::string endpoint = host + ":" + (port.empty() ? std::to_string(fallback_port) : port);
  return transfer::parse_peer_endpoint(endpoint, fallback_port);
}

void handle_incoming_shared_delete(AppState& state,
                                   socket_t client,
                                   const std::string& file_name,
                                   const std::string& peer,
                                   const std::optional<transfer::PeerEndpoint>& source_peer) {
  const std::string safe_name = transfer::safe_file_name(file_name);
  TransferRecord record;
  record.direction = "incoming";
  record.file_name = safe_name;
  record.status = "transferring";
  record.peer = peer;
  record.size = 0;
  record.message = "Removing shared file";
  const std::string id = add_transfer(state, record);

  std::error_code error;
  std::filesystem::create_directories(state.shared_dir, error);
  const std::filesystem::path destination = state.shared_dir / safe_name;
  if (std::filesystem::exists(destination, error)) {
    std::filesystem::remove(destination, error);
  }

  if (error) {
    update_transfer(state, id, 0, "failed", "Could not remove shared file");
  } else {
    if (source_peer) {
      mark_synced_shared_version(state, *source_peer, safe_name, SharedFileSignature{0, 0});
    }
    update_transfer(state, id, 0, "complete", "Removed from shared folder");
  }
  close_socket(client);
}

void handle_incoming_shared_put(AppState& state,
                                socket_t client,
                                const std::string& file_name,
                                const std::string& size_line,
                                const std::string& peer,
                                const std::optional<transfer::PeerEndpoint>& source_peer) {
  const auto parsed_size = parse_int(size_line);
  if (!parsed_size || *parsed_size < 0) {
    close_socket(client);
    return;
  }

  const std::string safe_name = transfer::safe_file_name(file_name);
  const std::uint64_t total_size = static_cast<std::uint64_t>(*parsed_size);
  TransferRecord record;
  record.direction = "incoming";
  record.file_name = safe_name;
  record.status = "transferring";
  record.peer = peer;
  record.size = total_size;
  record.message = "Receiving shared file";
  const std::string id = add_transfer(state, record);

  std::error_code error;
  std::filesystem::create_directories(state.shared_dir, error);
  if (error) {
    update_transfer(state, id, 0, "failed", "Could not create shared folder");
    close_socket(client);
    return;
  }

  const std::filesystem::path destination = state.shared_dir / safe_name;
  const std::filesystem::path temp_path =
      state.shared_dir / (".loopline-tmp-" + transfer::make_transfer_id() + "-" + safe_name);
  std::filesystem::remove(temp_path, error);

  std::ofstream output(temp_path, std::ios::binary);
  if (!output) {
    update_transfer(state, id, 0, "failed", "Could not open shared file");
    close_socket(client);
    return;
  }

  std::uint64_t hash = 14695981039346656037ull;
  std::uint64_t received_total = 0;
  std::vector<char> buffer(64 * 1024);
  while (received_total < total_size) {
    const auto remaining = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), total_size - received_total));
    const int received = recv(client, buffer.data(), static_cast<int>(remaining), 0);
    if (received <= 0) {
      output.close();
      std::filesystem::remove(temp_path, error);
      update_transfer(state, id, received_total, "failed", "Connection closed early");
      close_socket(client);
      return;
    }
    output.write(buffer.data(), received);
    hash = fnv1a_update(hash, buffer.data(), static_cast<std::size_t>(received));
    received_total += static_cast<std::uint64_t>(received);
    update_transfer(state, id, received_total, "transferring",
                    "Shared " + transfer::format_bytes(received_total));
  }
  output.close();

  const SharedFileSignature received_signature{received_total, hash};
  const auto existing_signature = shared_file_signature(destination);
  if (existing_signature && *existing_signature == received_signature) {
    std::filesystem::remove(temp_path, error);
    if (source_peer) {
      mark_synced_shared_version(state, *source_peer, safe_name, received_signature);
    }
    update_transfer(state, id, received_total, "complete", "Shared file already current");
    close_socket(client);
    return;
  }

  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::rename(temp_path, destination, error);
  if (error) {
    std::filesystem::remove(temp_path, error);
    update_transfer(state, id, received_total, "failed", "Could not publish shared file");
    close_socket(client);
    return;
  }

  if (source_peer) {
    mark_synced_shared_version(state, *source_peer, safe_name, received_signature);
  }
  update_transfer(state, id, received_total, "complete", "Saved to shared folder");
  close_socket(client);
}

void handle_incoming_transfer(AppState& state, socket_t client, const std::string& peer) {
  std::string magic;
  if (!read_line(client, magic)) {
    close_socket(client);
    return;
  }

  if (magic == "LOOPLINE-SHARED/1") {
    std::string operation;
    std::string file_name;
    std::string size_line;
    std::string source_node;
    std::string source_host;
    std::string source_port;
    std::string blank;
    if (!read_line(client, operation) || !read_line(client, file_name) || !read_line(client, size_line) ||
        !read_line(client, source_node) || !read_line(client, source_host) || !read_line(client, source_port) ||
        !read_line(client, blank)) {
      close_socket(client);
      return;
    }

    (void)source_node;
    const auto source_peer = source_peer_from_header(source_host, source_port, state.transfer_port);
    if (operation == "DELETE") {
      handle_incoming_shared_delete(state, client, file_name, peer, source_peer);
      return;
    }
    if (operation == "PUT") {
      handle_incoming_shared_put(state, client, file_name, size_line, peer, source_peer);
      return;
    }

    close_socket(client);
    return;
  }

  std::string file_name;
  std::string size_line;
  std::string blank;
  if (!read_line(client, file_name) || !read_line(client, size_line) || !read_line(client, blank) ||
      magic != "LOOPLINE/1") {
    close_socket(client);
    return;
  }

  const auto parsed_size = parse_int(size_line);
  if (!parsed_size || *parsed_size < 0) {
    close_socket(client);
    return;
  }

  const std::string safe_name = transfer::safe_file_name(file_name);
  const std::uint64_t total_size = static_cast<std::uint64_t>(*parsed_size);
  TransferRecord record;
  record.direction = "incoming";
  record.file_name = safe_name;
  record.status = "transferring";
  record.peer = peer;
  record.size = total_size;
  record.message = "Receiving " + transfer::format_bytes(total_size);
  const std::string id = add_transfer(state, record);

  std::filesystem::create_directories(state.receive_dir);
  const std::filesystem::path destination = unique_received_path(state.receive_dir, safe_name);
  std::ofstream output(destination, std::ios::binary);
  if (!output) {
    update_transfer(state, id, 0, "failed", "Could not open receive file");
    close_socket(client);
    return;
  }

  std::uint64_t received_total = 0;
  std::vector<char> buffer(64 * 1024);
  while (received_total < total_size) {
    const auto remaining = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), total_size - received_total));
    const int received = recv(client, buffer.data(), static_cast<int>(remaining), 0);
    if (received <= 0) {
      update_transfer(state, id, received_total, "failed", "Connection closed early");
      close_socket(client);
      return;
    }
    output.write(buffer.data(), received);
    received_total += static_cast<std::uint64_t>(received);
    update_transfer(state, id, received_total, "transferring",
                    "Received " + transfer::format_bytes(received_total));
  }

  update_transfer(state, id, received_total, "complete", "Saved to " + destination.string());
  close_socket(client);
}

void transfer_listener(AppState& state) {
  const socket_t listener = create_listener(state.bind_host, state.transfer_port);
  if (listener == invalid_socket) {
    std::cerr << "Could not start transfer listener on " << state.bind_host << ':' << state.transfer_port << '\n';
    return;
  }

  state.listener_active = true;
  while (state.running.load()) {
    sockaddr_in peer_address{};
    socket_length_t peer_size = sizeof(peer_address);
    const socket_t client = accept(listener, reinterpret_cast<sockaddr*>(&peer_address), &peer_size);
    if (client == invalid_socket) {
      continue;
    }

    char peer_buffer[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &peer_address.sin_addr, peer_buffer, sizeof(peer_buffer));
    std::thread(handle_incoming_transfer, std::ref(state), client, std::string(peer_buffer)).detach();
  }

  close_socket(listener);
}

bool read_http_request(socket_t client, HttpRequest& request) {
  std::string raw;
  std::vector<char> buffer(4096);
  while (raw.find("\r\n\r\n") == std::string::npos) {
    const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (received <= 0) {
      return false;
    }
    raw.append(buffer.data(), static_cast<std::size_t>(received));
    if (raw.size() > 64 * 1024) {
      return false;
    }
  }

  const auto header_end = raw.find("\r\n\r\n");
  const std::string headers = raw.substr(0, header_end);
  std::istringstream stream(headers);
  std::string request_line;
  std::getline(stream, request_line);
  if (!request_line.empty() && request_line.back() == '\r') {
    request_line.pop_back();
  }
  std::istringstream request_line_stream(request_line);
  request_line_stream >> request.method >> request.target;
  if (request.method.empty() || request.target.empty()) {
    return false;
  }

  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string name = lower_copy(line.substr(0, colon));
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    request.headers[name] = value;
  }

  const std::string content_length_header = header_value(request, "content-length", "0");
  const auto content_length = parse_int(content_length_header).value_or(0);
  if (content_length < 0) {
    return false;
  }

  const std::string already_read = raw.substr(header_end + 4);
  request.body.assign(already_read.begin(), already_read.end());
  while (request.body.size() < static_cast<std::size_t>(content_length)) {
    const std::size_t remaining = static_cast<std::size_t>(content_length) - request.body.size();
    const int received = recv(client, buffer.data(), static_cast<int>(std::min<std::size_t>(buffer.size(), remaining)), 0);
    if (received <= 0) {
      return false;
    }
    request.body.insert(request.body.end(), buffer.begin(), buffer.begin() + received);
  }

  return true;
}

void send_response(socket_t client,
                   int status,
                   const std::string& status_text,
                   const std::string& content_type,
                   const std::string& body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n";
  response << "Content-Type: " << content_type << "\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "Connection: close\r\n";
  response << "Access-Control-Allow-Origin: *\r\n";
  response << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n";
  response << "Access-Control-Allow-Headers: Content-Type,X-File-Name,X-Peer-Host,X-Peer-Port\r\n\r\n";
  response << body;
  send_text(client, response.str());
}

void send_json(socket_t client, int status, const std::string& status_text, const std::string& body) {
  send_response(client, status, status_text, "application/json", body);
}

void send_file_inline(AppState& state, socket_t client, const std::string& kind, const std::string& raw_name) {
  const auto directory = directory_for_kind(state, kind);
  if (!directory) {
    send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Invalid file kind\"}");
    return;
  }

  const std::string file_name = transfer::safe_file_name(raw_name);
  const std::filesystem::path file_path = *directory / file_name;
  if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
    send_json(client, 404, "Not Found", "{\"ok\":false,\"error\":\"File not found\"}");
    return;
  }

  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    send_json(client, 500, "Internal Server Error", "{\"ok\":false,\"error\":\"Could not open file\"}");
    return;
  }

  const auto size = std::filesystem::file_size(file_path);
  std::ostringstream headers;
  headers << "HTTP/1.1 200 OK\r\n";
  headers << "Content-Type: " << transfer::content_type_for_file(file_name) << "\r\n";
  headers << "Content-Length: " << size << "\r\n";
  headers << "Content-Disposition: inline; filename=\"" << transfer::json_escape(file_name) << "\"\r\n";
  headers << "Connection: close\r\n";
  headers << "Access-Control-Allow-Origin: *\r\n\r\n";
  if (!send_text(client, headers.str())) {
    return;
  }

  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = input.gcount();
    if (read > 0 && !send_all(client, buffer.data(), static_cast<std::size_t>(read))) {
      return;
    }
  }
}

void send_file_to_peer(AppState& state,
                       socket_t client,
                       const std::string& host,
                       int port,
                       const std::string& file_name,
                       const std::vector<char>& body) {
  if (!transfer::is_allowed_peer(host, state.allow_remote_peers)) {
    send_json(client, 400, "Bad Request",
              "{\"ok\":false,\"error\":\"Only localhost and private LAN peers are allowed\"}");
    return;
  }
  add_sync_peer(state, transfer::PeerEndpoint{host, port});

  TransferRecord record;
  record.direction = "outgoing";
  record.file_name = file_name;
  record.status = "transferring";
  record.peer = host + ":" + std::to_string(port);
  record.size = static_cast<std::uint64_t>(body.size());
  record.message = "Opening peer socket";
  const std::string id = add_transfer(state, record);

  std::filesystem::create_directories(state.sent_dir);
  const std::filesystem::path sent_path = unique_received_path(state.sent_dir, file_name);
  std::ofstream sent_copy(sent_path, std::ios::binary);
  if (sent_copy) {
    sent_copy.write(body.data(), static_cast<std::streamsize>(body.size()));
  }

  const socket_t peer = connect_to_peer(host, port);
  if (peer == invalid_socket) {
    update_transfer(state, id, 0, "failed", "Could not connect to peer");
    send_json(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"Could not connect to peer\"}");
    return;
  }

  const std::string header =
      "LOOPLINE/1\n" + file_name + "\n" + std::to_string(body.size()) + "\n\n";
  if (!send_text(peer, header)) {
    update_transfer(state, id, 0, "failed", "Could not send transfer header");
    close_socket(peer);
    send_json(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"Could not send header\"}");
    return;
  }

  std::uint64_t sent = 0;
  while (sent < body.size()) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(64 * 1024, body.size() - static_cast<std::size_t>(sent)));
    const int result = send(peer, body.data() + sent, chunk, 0);
    if (result <= 0) {
      update_transfer(state, id, sent, "failed", "Peer closed the transfer");
      close_socket(peer);
      send_json(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"Peer closed the transfer\"}");
      return;
    }
    sent += static_cast<std::uint64_t>(result);
    update_transfer(state, id, sent, "transferring", "Sent " + transfer::format_bytes(sent));
  }

  close_socket(peer);
  update_transfer(state, id, sent, "complete", "Delivered to " + host + ":" + std::to_string(port));
  send_json(client, 200, "OK", "{\"ok\":true,\"id\":\"" + transfer::json_escape(id) + "\"}");
}

void handle_http_client(AppState& state, socket_t client) {
  HttpRequest request;
  if (!read_http_request(client, request)) {
    send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Malformed request\"}");
    close_socket(client);
    return;
  }

  const std::string route = path_only(request.target);
  if (request.method == "OPTIONS") {
    send_response(client, 204, "No Content", "text/plain", "");
  } else if (request.method == "GET" && route == "/api/health") {
    send_json(client, 200, "OK", "{\"ok\":true}");
  } else if (request.method == "GET" && route == "/api/status") {
    send_json(client, 200, "OK", status_json(state));
  } else if (request.method == "GET" && route == "/api/files") {
    const std::string kind = query_value(request.target, "kind").empty() ? "received" : query_value(request.target, "kind");
    if (!directory_for_kind(state, kind)) {
      send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Invalid file kind\"}");
    } else {
      send_json(client, 200, "OK", file_list_json(state, kind));
    }
  } else if (request.method == "GET" && route == "/api/files/open") {
    const std::string kind = query_value(request.target, "kind").empty() ? "received" : query_value(request.target, "kind");
    const std::string name = query_value(request.target, "name");
    send_file_inline(state, client, kind, name);
  } else if (request.method == "POST" && route == "/api/sync/peers") {
    const std::string host = query_value(request.target, "host");
    const int port = parse_int(query_value(request.target, "port")).value_or(state.transfer_port);
    const auto peer = transfer::parse_peer_endpoint(host + ":" + std::to_string(port), state.transfer_port);
    if (!peer || !transfer::is_allowed_peer(peer->host, state.allow_remote_peers)) {
      send_json(client, 400, "Bad Request",
                "{\"ok\":false,\"error\":\"Use localhost or a private LAN peer that this backend allows\"}");
    } else {
      add_sync_peer(state, *peer);
      send_json(client, 200, "OK", status_json(state));
    }
  } else if (request.method == "POST" && route == "/api/sync/peers/remove") {
    const std::string host = query_value(request.target, "host");
    const int port = parse_int(query_value(request.target, "port")).value_or(state.transfer_port);
    const auto peer = transfer::parse_peer_endpoint(host + ":" + std::to_string(port), state.transfer_port);
    if (!peer) {
      send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Invalid sync peer\"}");
    } else {
      remove_sync_peer(state, *peer);
      send_json(client, 200, "OK", status_json(state));
    }
  } else if (request.method == "POST" && route == "/api/receive/start") {
    const std::string port_value = query_value(request.target, "port");
    const int requested_port = port_value.empty() ? state.transfer_port : parse_int(port_value).value_or(state.transfer_port);
    if (requested_port != state.transfer_port) {
      send_json(client, 409, "Conflict", "{\"ok\":false,\"error\":\"Restart the backend with the requested transfer port\"}");
    } else if (state.listener_active.load()) {
      send_json(client, 200, "OK", "{\"ok\":true,\"message\":\"Receiver already active\"}");
    } else {
      send_json(client, 503, "Service Unavailable", "{\"ok\":false,\"error\":\"Receiver is not active\"}");
    }
  } else if (request.method == "POST" && route == "/api/send") {
    const std::string raw_file_name = url_decode(header_value(request, "x-file-name", "download.bin"));
    const std::string file_name = transfer::safe_file_name(raw_file_name);
    const std::string peer_host = header_value(request, "x-peer-host", "127.0.0.1");
    const int peer_port = parse_int(header_value(request, "x-peer-port", std::to_string(state.transfer_port)))
                              .value_or(state.transfer_port);
    send_file_to_peer(state, client, peer_host, peer_port, file_name, request.body);
  } else {
    send_json(client, 404, "Not Found", "{\"ok\":false,\"error\":\"Not found\"}");
  }

  close_socket(client);
}

void http_server(AppState& state) {
  const socket_t listener = create_listener(state.bind_host, state.http_port);
  if (listener == invalid_socket) {
    std::cerr << "Could not start HTTP server on " << state.bind_host << ':' << state.http_port << '\n';
    return;
  }

  std::cout << "HTTP API: http://" << state.advertised_host << ':' << state.http_port << '\n';
  while (state.running.load()) {
    sockaddr_in peer_address{};
    socket_length_t peer_size = sizeof(peer_address);
    const socket_t client = accept(listener, reinterpret_cast<sockaddr*>(&peer_address), &peer_size);
    if (client == invalid_socket) {
      continue;
    }
    std::thread(handle_http_client, std::ref(state), client).detach();
  }
  close_socket(listener);
}

int arg_value(int argc, char* argv[], const std::string& name, int fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (name == argv[index]) {
      return parse_int(argv[index + 1]).value_or(fallback);
    }
  }
  return fallback;
}

std::string arg_string(int argc, char* argv[], const std::string& name, const std::string& fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (name == argv[index]) {
      return argv[index + 1];
    }
  }
  return fallback;
}

void add_sync_peers_from_list(AppState& state, const std::string& raw_peers) {
  std::size_t start = 0;
  while (start <= raw_peers.size()) {
    std::size_t end = raw_peers.find(',', start);
    const auto semicolon = raw_peers.find(';', start);
    if (end == std::string::npos || (semicolon != std::string::npos && semicolon < end)) {
      end = semicolon;
    }

    const std::string value = raw_peers.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const auto peer = transfer::parse_peer_endpoint(value, state.transfer_port);
    if (peer && transfer::is_allowed_peer(peer->host, state.allow_remote_peers)) {
      add_sync_peer(state, *peer);
    }

    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "Could not initialize Winsock\n";
    return 1;
  }
#endif

  AppState state;
  state.http_port = arg_value(argc, argv, "--http", parse_int(env_value("P2P_HTTP_PORT", "8787")).value_or(8787));
  state.transfer_port =
      arg_value(argc, argv, "--transfer", parse_int(env_value("P2P_TRANSFER_PORT", "8788")).value_or(8788));
  state.bind_host = arg_string(argc, argv, "--bind", env_value("P2P_BIND_HOST", "127.0.0.1"));
  state.advertised_host = arg_string(argc, argv, "--advertise", env_value("P2P_ADVERTISED_HOST", state.bind_host));
  if (state.advertised_host == "0.0.0.0") {
    state.advertised_host = "127.0.0.1";
  }
  state.allow_remote_peers = env_flag("P2P_ALLOW_REMOTE", false);
  state.receive_dir = env_value("P2P_RECEIVE_DIR", (std::filesystem::current_path() / "backend" / "received").string());
  state.sent_dir = env_value("P2P_SENT_DIR", (std::filesystem::current_path() / "backend" / "sent").string());
  state.shared_dir = env_value("P2P_SHARED_DIR", (std::filesystem::current_path() / "shared").string());
  add_sync_peers_from_list(state, env_value("P2P_SYNC_PEERS", ""));
  std::filesystem::create_directories(state.receive_dir);
  std::filesystem::create_directories(state.sent_dir);
  std::filesystem::create_directories(state.shared_dir);

  std::thread receiver(transfer_listener, std::ref(state));
  receiver.detach();
  std::thread shared_watcher(shared_folder_watcher, std::ref(state));
  shared_watcher.detach();

  std::cout << "Loopline P2P receiver: " << state.advertised_host << ':' << state.transfer_port << '\n';
  std::cout << "Bind host: " << state.bind_host
            << " | remote peers: " << (state.allow_remote_peers ? "enabled" : "disabled") << '\n';
  std::cout << "Received files: " << state.receive_dir.string() << '\n';
  std::cout << "Sent files: " << state.sent_dir.string() << '\n';
  std::cout << "Shared folder: " << state.shared_dir.string() << '\n';
  http_server(state);

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
