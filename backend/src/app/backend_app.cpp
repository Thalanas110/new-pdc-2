#include "app/backend_app.hpp"

#include "controllers/http_controller.hpp"
#include "models/app_state.hpp"
#include "services/file-vault/file_vault_service.hpp"
#include "services/net-io/net_io.hpp"
#include "services/shared-sync/shared_sync_service.hpp"
#include "services/transfer/transfer_service.hpp"
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
      state.add_sync_peer(*peer);
    }

    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
}

void http_server(AppState& state,
                 FileVaultService& file_vault_service,
                 TransferService& transfer_service,
                 SharedSyncService& shared_sync_service) {
  const socket_t listener = netio::create_listener(state.bind_host, state.http_port);
  if (listener == invalid_socket) {
    std::cerr << "Could not start HTTP server on " << state.bind_host << ':' << state.http_port << '\n';
    return;
  }

  HttpController::Dependencies deps;
  deps.status_json = [&state]() { return state.status_json(); };
  deps.file_list_json = [&file_vault_service](const std::string& kind) {
    return file_vault_service.file_list_json(kind);
  };
  deps.send_file_inline = [&file_vault_service](socket_t client, const std::string& kind, const std::string& name) {
    file_vault_service.send_file_inline(client, kind, name);
  };
  deps.send_file_to_peer = [&transfer_service](socket_t client,
                                               const std::string& host,
                                               int port,
                                               const std::string& file_name,
                                               const std::vector<char>& body) {
    transfer_service.send_file_to_peer(client, host, port, file_name, body);
  };
  deps.sync_shared_folder_json = [&shared_sync_service]() { return shared_sync_service.sync_shared_folder_json(); };
  deps.save_shared_upload = [&file_vault_service](socket_t client, const std::string& file_name, const std::vector<char>& body) {
    file_vault_service.save_shared_upload(client, file_name, body);
  };
  deps.transfer_port = [&state]() { return state.transfer_port; };
  deps.listener_active = [&state]() { return state.listener_active.load(); };
  deps.add_sync_peer = [&state](const transfer::PeerEndpoint& peer) { state.add_sync_peer(peer); };
  deps.remove_sync_peer = [&state](const transfer::PeerEndpoint& peer) { state.remove_sync_peer(peer); };
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
  state.advertised_host = arg_string(argc, argv, "--advertise", env_value("P2P_ADVERTISED_HOST", state.bind_host));
  if (state.advertised_host == "0.0.0.0") {
    state.advertised_host = "127.0.0.1";
  }
  state.allow_remote_peers = env_flag("P2P_ALLOW_REMOTE", true);
  state.receive_dir = env_value("P2P_RECEIVE_DIR", (std::filesystem::current_path() / "backend" / "received").string());
  state.sent_dir = env_value("P2P_SENT_DIR", (std::filesystem::current_path() / "backend" / "sent").string());
  state.shared_dir = env_value("P2P_SHARED_DIR", (std::filesystem::current_path() / "shared").string());
  add_sync_peers_from_list(state, env_value("P2P_SYNC_PEERS", ""));
  std::filesystem::create_directories(state.receive_dir);
  std::filesystem::create_directories(state.sent_dir);
  std::filesystem::create_directories(state.shared_dir);

  FileVaultService file_vault_service(state);
  SharedSyncService shared_sync_service(state);
  TransferService transfer_service(state, shared_sync_service);

  std::thread receiver(&TransferService::transfer_listener, &transfer_service);
  receiver.detach();
  std::thread shared_watcher(&SharedSyncService::watch_shared_folder, &shared_sync_service);
  shared_watcher.detach();

  std::cout << "Loopline P2P receiver: " << state.advertised_host << ':' << state.transfer_port << '\n';
  std::cout << "Bind host: " << state.bind_host
            << " | remote peers: " << (state.allow_remote_peers ? "enabled" : "disabled") << '\n';
  std::cout << "Received files: " << state.receive_dir.string() << '\n';
  std::cout << "Sent files: " << state.sent_dir.string() << '\n';
  std::cout << "Shared folder: " << state.shared_dir.string() << '\n';
  http_server(state, file_vault_service, transfer_service, shared_sync_service);

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}

