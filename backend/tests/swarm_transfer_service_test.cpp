#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "models/app_state.hpp"
#include "services/net-io/net_io.hpp"
#include "services/swarm/catalog_service.hpp"
#include "services/swarm/manifest_service.hpp"
#include "services/swarm/piece_store_service.hpp"
#include "services/swarm/swarm_transfer_service.hpp"

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace {

namespace fs = std::filesystem;

std::vector<char> make_payload(std::size_t size) {
  std::vector<char> payload;
  payload.reserve(size);
  for (std::size_t index = 0; index < size; ++index) {
    payload.push_back(static_cast<char>('A' + (index % 23)));
  }
  return payload;
}

std::vector<char> read_bytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool wait_for(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
  const auto start = std::chrono::steady_clock::now();
  while ((std::chrono::steady_clock::now() - start) < timeout) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return predicate();
}

const SwarmPeerRecord* find_peer_by_endpoint(const std::vector<SwarmPeerRecord>& peers,
                                             const std::string& host,
                                             int port) {
  const auto found = std::find_if(peers.begin(), peers.end(), [&](const auto& peer) {
    return peer.host == host && peer.port == port;
  });
  return found == peers.end() ? nullptr : &(*found);
}

struct SwarmNode {
  AppState state;
  ManifestService manifest_service;
  PieceStoreService piece_store;
  CatalogService catalog;
  SwarmTransferService transfer;
  std::thread listener;

  explicit SwarmNode(const fs::path& root, int port)
      : piece_store(root / "torrents"),
        catalog(state),
        transfer(state, catalog, manifest_service, piece_store) {
    state.bind_host = "127.0.0.1";
    state.advertised_host = "127.0.0.1";
    state.http_port = port - 1;
    state.transfer_port = port;
    state.receive_dir = root / "received";
    state.sent_dir = root / "sent";
    state.shared_dir = root / "shared";
    fs::create_directories(state.receive_dir);
    fs::create_directories(state.sent_dir);
    fs::create_directories(state.shared_dir);
  }

  void start() {
    listener = std::thread([this]() { transfer.transfer_listener(); });
    const bool ready = wait_for([this]() { return state.listener_active.load(); }, std::chrono::milliseconds(1500));
    assert(ready);
  }

  void stop() {
    state.running = false;
    const socket_t peer = netio::connect_to_peer(state.advertised_host, state.transfer_port);
    if (peer != invalid_socket) {
      close_socket(peer);
    }
    if (listener.joinable()) {
      listener.join();
    }
  }
};

}  // namespace

int main() {
#ifdef _WIN32
  WSADATA data{};
  const int startup = WSAStartup(MAKEWORD(2, 2), &data);
  assert(startup == 0);
#endif

  const fs::path root = fs::temp_directory_path() / "loopline-swarm-transfer-test";
  fs::remove_all(root);
  fs::create_directories(root);

  SwarmNode publisher(root / "publisher", 9411);
  SwarmNode leecher(root / "leecher", 9412);
  SwarmNode fanout(root / "fanout", 9413);

  publisher.start();
  leecher.start();
  fanout.start();

  {
    SwarmNode offline(root / "offline", 9414);
    const bool offline_bootstrap = offline.transfer.bootstrap_peer(transfer::PeerEndpoint{"127.0.0.1", 9599});
    assert(!offline_bootstrap);
    const auto peers = offline.state.swarm_peers_snapshot();
    assert(peers.size() == 1);
    assert(peers[0].host == "127.0.0.1");
    assert(peers[0].port == 9599);
    assert(!peers[0].reachable);
  }

  const auto payload = make_payload(700000);
  const auto manifest = publisher.transfer.publish_file("demo.bin", payload);
  assert(manifest.has_value());
  assert(manifest->piece_count >= 3);

  const bool leecher_catalog_ready =
      leecher.transfer.bootstrap_peer(transfer::PeerEndpoint{"127.0.0.1", 9411});
  assert(leecher_catalog_ready);
  const auto* leecher_peer = find_peer_by_endpoint(leecher.state.swarm_peers_snapshot(), "127.0.0.1", 9411);
  assert(leecher_peer != nullptr);
  assert(leecher_peer->reachable);
  assert(wait_for([&]() { return leecher.state.library_snapshot().size() == 1; }, std::chrono::milliseconds(1500)));

  leecher.transfer.start_download_by_id(manifest->torrent_id);
  const bool leecher_complete = wait_for(
      [&]() {
        const auto sessions = leecher.state.download_sessions_snapshot();
        return sessions.size() == 1 &&
               (sessions[0].status == "complete" || sessions[0].status == "seeding");
      },
      std::chrono::seconds(6));
  assert(leecher_complete);
  assert(read_bytes(leecher.state.receive_dir / "demo.bin") == payload);

  assert(fanout.transfer.bootstrap_peer(transfer::PeerEndpoint{"127.0.0.1", 9412}));
  const bool transitive_peer_visible = wait_for(
      [&]() {
        const auto* peer = find_peer_by_endpoint(fanout.state.swarm_peers_snapshot(), "127.0.0.1", 9411);
        return peer != nullptr && peer->reachable;
      },
      std::chrono::seconds(4));
  assert(transitive_peer_visible);
  const bool two_seeders_visible = wait_for(
      [&]() {
        const auto library = fanout.state.library_snapshot();
        return library.size() == 1 && library[0].seeder_count >= 2;
      },
      std::chrono::seconds(2));
  assert(two_seeders_visible);

  fanout.transfer.start_download_by_id(manifest->torrent_id);
  const bool fanout_complete = wait_for(
      [&]() {
        const auto sessions = fanout.state.download_sessions_snapshot();
        return sessions.size() == 1 &&
               (sessions[0].status == "complete" || sessions[0].status == "seeding") &&
               sessions[0].active_peers.size() >= 2;
      },
      std::chrono::seconds(6));
  assert(fanout_complete);

  const auto final_sessions = fanout.state.download_sessions_snapshot();
  assert(final_sessions[0].verified_pieces == manifest->piece_count);
  assert(read_bytes(fanout.state.receive_dir / "demo.bin") == payload);

  const auto second_payload = make_payload(310000);
  const auto second_manifest = publisher.transfer.publish_file("demo-2.bin", second_payload);
  assert(second_manifest.has_value());
  const bool second_manifest_visible = wait_for(
      [&]() {
        const auto library = fanout.state.library_snapshot();
        return library.size() == 2 &&
               std::any_of(library.begin(), library.end(), [&](const auto& entry) {
                 return entry.torrent_id == second_manifest->torrent_id;
               });
      },
      std::chrono::seconds(4));
  assert(second_manifest_visible);

  fanout.transfer.start_download_by_id(second_manifest->torrent_id);
  const bool second_download_complete = wait_for(
      [&]() {
        const auto sessions = fanout.state.download_sessions_snapshot();
        return sessions.size() == 2 &&
               std::any_of(sessions.begin(), sessions.end(), [&](const auto& session) {
                 return session.torrent_id == second_manifest->torrent_id &&
                        (session.status == "complete" || session.status == "seeding");
               });
      },
      std::chrono::seconds(6));
  assert(second_download_complete);
  assert(read_bytes(fanout.state.receive_dir / "demo-2.bin") == second_payload);

  fanout.stop();
  leecher.stop();
  publisher.stop();
  fs::remove_all(root);

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
