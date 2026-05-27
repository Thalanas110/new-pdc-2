#include "app/backend_app.hpp"

#include "controllers/http_controller.hpp"
#include "models/app_state.hpp"
#include "services/file-vault/file_vault_service.hpp"
#include "services/net-io/net_io.hpp"
#include "services/swarm/catalog_service.hpp"
#include "services/swarm/discovery_service.hpp"
#include "services/swarm/manifest_service.hpp"
#include "services/swarm/piece_store_service.hpp"
#include "services/swarm/swarm_transfer_service.hpp"
#include "views/http_view.hpp"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace {

std::string lower_copy(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
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

int arg_value(int argc, char* argv[], const std::string& name, int fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (name == argv[index]) {
      return parse_int(argv[index + 1]).value_or(fallback);
    }
  }
  return fallback;
}

std::optional<std::string> detect_private_lan_host() {
  const socket_t probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (probe != invalid_socket) {
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
    if (connect(probe, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
      sockaddr_in local{};
      socket_length_t local_size = sizeof(local);
      if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &local_size) == 0) {
        char buffer[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer)) != nullptr) {
          const std::string detected = buffer;
          if (transfer::is_private_lan_host(detected)) {
            close_socket(probe);
            return detected;
          }
        }
      }
    }
    close_socket(probe);
  }

  char hostname[256] = {};
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    return std::nullopt;
  }

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* results = nullptr;
  if (getaddrinfo(hostname, nullptr, &hints, &results) != 0) {
    return std::nullopt;
  }

  std::optional<std::string> detected;
  for (addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
    if (candidate->ai_addr == nullptr) {
      continue;
    }
    const auto* address = reinterpret_cast<const sockaddr_in*>(candidate->ai_addr);
    char buffer[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof(buffer)) == nullptr) {
      continue;
    }
    const std::string host = buffer;
    if (transfer::is_private_lan_host(host)) {
      detected = host;
      break;
    }
  }

  freeaddrinfo(results);
  return detected;
}

std::string arg_string(int argc, char* argv[], const std::string& name, const std::string& fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (name == argv[index]) {
      return argv[index + 1];
    }
  }
  return fallback;
}

void add_bootstrap_peers_from_list(DiscoveryService& discovery_service,
                                   const AppState& state,
                                   const std::string& raw_peers) {
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
      discovery_service.bootstrap_peer(*peer);
    }

    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
}

void http_server(AppState& state,
                 CatalogService& catalog_service,
                 DiscoveryService& discovery_service,
                 SwarmTransferService& swarm_transfer_service,
                 FileVaultService& file_vault_service) {
  const socket_t listener = netio::create_listener(state.bind_host, state.http_port);
  if (listener == invalid_socket) {
    std::cerr << "Could not start HTTP server on " << state.bind_host << ':' << state.http_port << '\n';
    return;
  }

  HttpController::Dependencies deps;
  deps.status_json = [&state]() { return state.status_json(); };
  deps.library_json = [&catalog_service]() { return catalog_service.library_json(); };
  deps.downloads_json = [&swarm_transfer_service]() { return swarm_transfer_service.downloads_json(); };
  deps.files_json = [&file_vault_service](const std::string& kind) { return file_vault_service.file_list_json(kind); };
  deps.publish_file = [&swarm_transfer_service](socket_t client, const std::string& file_name, const std::vector<char>& body) {
    swarm_transfer_service.publish_from_http(client, file_name, body);
  };
  deps.open_file = [&file_vault_service](socket_t client, const std::string& kind, const std::string& name) {
    file_vault_service.send_file_inline(client, kind, name);
  };
  deps.download_file = [&file_vault_service](socket_t client, const std::string& kind, const std::string& name) {
    file_vault_service.send_file_attachment(client, kind, name);
  };
  deps.open_library_file = [&swarm_transfer_service](socket_t client, const std::string& torrent_id) {
    swarm_transfer_service.send_local_file_inline(client, torrent_id);
  };
  deps.download_library_file = [&swarm_transfer_service](socket_t client, const std::string& torrent_id) {
    swarm_transfer_service.send_local_file_attachment(client, torrent_id);
  };
  deps.bootstrap_peer = [&discovery_service, &swarm_transfer_service](const transfer::PeerEndpoint& peer) {
    discovery_service.bootstrap_peer(peer);
    return swarm_transfer_service.bootstrap_peer(peer);
  };
  deps.start_download = [&swarm_transfer_service](const std::string& torrent_id) {
    swarm_transfer_service.start_download_by_id(torrent_id);
  };
  deps.transfer_port = [&state]() { return state.transfer_port; };
  deps.is_allowed_peer = [&state](const std::string& host) { return transfer::is_allowed_peer(host, state.allow_remote_peers); };

  auto controller = std::make_shared<HttpController>(HttpView{}, std::move(deps));

  std::cout << "HTTP API: http://" << state.advertised_host << ':' << state.http_port << '\n';
  while (state.running.load()) {
    sockaddr_in peer_address{};
    socket_length_t peer_size = sizeof(peer_address);
    const socket_t client = accept(listener, reinterpret_cast<sockaddr*>(&peer_address), &peer_size);
    if (client == invalid_socket) {
      continue;
    }
    std::thread([controller, client]() { controller->handle_client(client); }).detach();
  }
  close_socket(listener);
}

}  // namespace

int BackendApp::run(int argc, char* argv[]) {
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
  state.bind_host = arg_string(argc, argv, "--bind", env_value("P2P_BIND_HOST", "0.0.0.0"));
  state.allow_remote_peers = env_flag("P2P_ALLOW_REMOTE", true);
  const std::string configured_advertise = arg_string(argc, argv, "--advertise", env_value("P2P_ADVERTISED_HOST", ""));
  if (!configured_advertise.empty()) {
    state.advertised_host = configured_advertise;
  } else if (state.allow_remote_peers) {
    state.advertised_host = detect_private_lan_host().value_or("127.0.0.1");
  } else {
    state.advertised_host = "127.0.0.1";
  }
  state.receive_dir = env_value("P2P_RECEIVE_DIR", (std::filesystem::current_path() / "backend" / "received").string());
  state.sent_dir = env_value("P2P_SENT_DIR", (std::filesystem::current_path() / "backend" / "sent").string());
  state.shared_dir = env_value("P2P_SHARED_DIR", (std::filesystem::current_path() / "shared").string());
  std::filesystem::create_directories(state.receive_dir);
  std::filesystem::create_directories(state.sent_dir);
  std::filesystem::create_directories(state.shared_dir);

  ManifestService manifest_service;
  PieceStoreService piece_store_service(std::filesystem::current_path() / "backend" / "torrents");
  CatalogService catalog_service(state);
  DiscoveryService discovery_service(state, 8789);
  SwarmTransferService swarm_transfer_service(state, catalog_service, manifest_service, piece_store_service);
  FileVaultService file_vault_service(state);
  discovery_service.set_peer_detected_callback([&swarm_transfer_service](const transfer::PeerEndpoint& peer) {
    swarm_transfer_service.schedule_peer_probe(peer);
  });
  add_bootstrap_peers_from_list(discovery_service, state, env_value("P2P_SYNC_PEERS", ""));
  discovery_service.start();
  std::thread([&swarm_transfer_service]() { swarm_transfer_service.transfer_listener(); }).detach();

  std::cout << "Loopline P2P receiver: " << state.advertised_host << ':' << state.transfer_port << '\n';
  std::cout << "Bind host: " << state.bind_host
            << " | remote peers: " << (state.allow_remote_peers ? "enabled" : "disabled") << '\n';
  std::cout << "Received files: " << state.receive_dir.string() << '\n';
  std::cout << "Sent files: " << state.sent_dir.string() << '\n';
  std::cout << "Shared folder: " << state.shared_dir.string() << '\n';
  http_server(state, catalog_service, discovery_service, swarm_transfer_service, file_vault_service);

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}

