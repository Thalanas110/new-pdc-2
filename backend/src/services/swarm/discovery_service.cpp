#include "services/swarm/discovery_service.hpp"

#include "core/transfer_core.hpp"
#include "shared/net_socket.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr auto kDiscoveryAnnounceInterval = std::chrono::seconds(2);
constexpr auto kUnicastSweepInterval = std::chrono::seconds(12);
constexpr auto kDiscoveryProbeCooldown = std::chrono::seconds(8);

struct PeerAnnouncement {
  std::string node_id;
  std::string host;
  int port = 0;
};

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

std::string discovery_message(const AppState& state) {
  std::ostringstream out;
  out << "DISCOVER/1\n";
  out << "NODE " << state.node_id << '\n';
  out << "HOST " << state.advertised_host << '\n';
  out << "PORT " << state.transfer_port << '\n';
  return out.str();
}

std::optional<PeerAnnouncement> parse_announcement(const std::string& message) {
  std::istringstream input(message);
  std::string magic;
  std::string node_line;
  std::string host_line;
  std::string port_line;
  if (!std::getline(input, magic) || !std::getline(input, node_line) || !std::getline(input, host_line) ||
      !std::getline(input, port_line)) {
    return std::nullopt;
  }

  if (!magic.empty() && magic.back() == '\r') {
    magic.pop_back();
  }
  if (!node_line.empty() && node_line.back() == '\r') {
    node_line.pop_back();
  }
  if (!host_line.empty() && host_line.back() == '\r') {
    host_line.pop_back();
  }
  if (!port_line.empty() && port_line.back() == '\r') {
    port_line.pop_back();
  }

  if (magic != "DISCOVER/1" || node_line.rfind("NODE ", 0) != 0 || host_line.rfind("HOST ", 0) != 0 ||
      port_line.rfind("PORT ", 0) != 0) {
    return std::nullopt;
  }

  const auto parsed_port = parse_int(port_line.substr(5));
  if (!parsed_port) {
    return std::nullopt;
  }

  return PeerAnnouncement{node_line.substr(5), host_line.substr(5), *parsed_port};
}

std::optional<std::string> directed_broadcast_host(const std::string& host) {
  if (!transfer::is_private_lan_host(host)) {
    return std::nullopt;
  }

  const auto last_dot = host.rfind('.');
  if (last_dot == std::string::npos) {
    return std::nullopt;
  }

  return host.substr(0, last_dot + 1) + "255";
}

std::vector<std::string> unicast_probe_hosts(const std::string& host) {
  if (!transfer::is_private_lan_host(host)) {
    return {};
  }

  const auto last_dot = host.rfind('.');
  if (last_dot == std::string::npos) {
    return {};
  }

  const auto parsed_octet = parse_int(host.substr(last_dot + 1));
  if (!parsed_octet || *parsed_octet < 0 || *parsed_octet > 255) {
    return {};
  }

  const std::string prefix = host.substr(0, last_dot + 1);
  std::vector<std::string> targets;
  targets.reserve(253);
  for (int octet = 1; octet <= 254; ++octet) {
    if (octet == *parsed_octet) {
      continue;
    }
    targets.push_back(prefix + std::to_string(octet));
  }
  return targets;
}

socket_t create_udp_socket_for_send() {
  const socket_t socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_handle == invalid_socket) {
    return invalid_socket;
  }

  int broadcast = 1;
  setsockopt(socket_handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
  return socket_handle;
}

socket_t create_udp_listener_socket(int port) {
  const socket_t socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_handle == invalid_socket) {
    return invalid_socket;
  }

  int reuse = 1;
  setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  int broadcast = 1;
  setsockopt(socket_handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

#ifdef _WIN32
  DWORD timeout_ms = 1000;
  setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
  timeval timeout{};
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close_socket(socket_handle);
    return invalid_socket;
  }

  return socket_handle;
}

bool send_udp_message(socket_t socket_handle, const std::string& host, int port, const std::string& payload) {
  sockaddr_in target{};
  target.sin_family = AF_INET;
  target.sin_port = htons(static_cast<unsigned short>(port));
  if (inet_pton(AF_INET, host.c_str(), &target.sin_addr) != 1) {
    return false;
  }

  const int sent = sendto(socket_handle,
                          payload.data(),
                          static_cast<int>(payload.size()),
                          0,
                          reinterpret_cast<sockaddr*>(&target),
                          sizeof(target));
  return sent == static_cast<int>(payload.size());
}

}  // namespace

DiscoveryService::DiscoveryService(AppState& state, int discovery_port)
    : state_(state), discovery_port_(discovery_port) {}

void DiscoveryService::set_peer_detected_callback(PeerDetectedCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  peer_detected_callback_ = std::move(callback);
}

void DiscoveryService::start() {
  std::thread([this]() { announce_loop(); }).detach();
  std::thread([this]() { listen_loop(); }).detach();
}

void DiscoveryService::notify_peer_detected(const transfer::PeerEndpoint& peer) const {
  PeerDetectedCallback callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    const auto key = transfer::peer_endpoint_key(peer.host, peer.port);
    const auto found = next_probe_at_.find(key);
    if (found != next_probe_at_.end() && found->second > now) {
      return;
    }
    next_probe_at_[key] = now + kDiscoveryProbeCooldown;
    callback = peer_detected_callback_;
  }

  if (callback) {
    callback(peer);
  }
}

void DiscoveryService::announce_loop() const {
  const socket_t socket_handle = create_udp_socket_for_send();
  if (socket_handle == invalid_socket) {
    return;
  }

  auto next_unicast_sweep = std::chrono::steady_clock::now();
  while (state_.running.load()) {
    const auto payload = discovery_message(state_);
    (void)send_udp_message(socket_handle, "255.255.255.255", discovery_port_, payload);
    if (const auto directed = directed_broadcast_host(state_.advertised_host)) {
      (void)send_udp_message(socket_handle, *directed, discovery_port_, payload);
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_unicast_sweep) {
      for (const auto& target : unicast_probe_hosts(state_.advertised_host)) {
        (void)send_udp_message(socket_handle, target, discovery_port_, payload);
      }
      next_unicast_sweep = now + kUnicastSweepInterval;
    }
    std::this_thread::sleep_for(kDiscoveryAnnounceInterval);
  }

  close_socket(socket_handle);
}

void DiscoveryService::listen_loop() const {
  const socket_t listener = create_udp_listener_socket(discovery_port_);
  if (listener == invalid_socket) {
    return;
  }

  std::array<char, 1024> buffer{};
  while (state_.running.load()) {
    sockaddr_in source{};
    socket_length_t source_size = sizeof(source);
    const int received =
        recvfrom(listener, buffer.data(), static_cast<int>(buffer.size() - 1), 0, reinterpret_cast<sockaddr*>(&source), &source_size);
    if (received <= 0) {
      continue;
    }

    buffer[static_cast<std::size_t>(received)] = '\0';
    const auto announcement = parse_announcement(std::string(buffer.data(), static_cast<std::size_t>(received)));
    if (!announcement || announcement->node_id == state_.node_id) {
      continue;
    }

    const auto peer = transfer::parse_peer_endpoint(
        announcement->host + ":" + std::to_string(announcement->port), announcement->port);
    if (!peer) {
      continue;
    }

    note_peer_hello(announcement->node_id, peer->host, peer->port, "discovered");
    notify_peer_detected(*peer);
  }

  close_socket(listener);
}

void DiscoveryService::note_peer_hello(const std::string& node_id,
                                       const std::string& host,
                                       int port,
                                       const std::string& source) const {
  SwarmPeerRecord peer;
  peer.node_id = node_id;
  peer.host = host;
  peer.port = port;
  peer.source = source;
  peer.reachable = true;
  state_.upsert_swarm_peer(peer);
}

void DiscoveryService::bootstrap_peer(const transfer::PeerEndpoint& peer) {
  note_peer_hello("bootstrap-" + transfer::peer_endpoint_key(peer.host, peer.port),
                  peer.host,
                  peer.port,
                  "bootstrap");
  notify_peer_detected(peer);
}
