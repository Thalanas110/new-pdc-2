#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

#include "models/app_state.hpp"
#include "services/swarm/discovery_service.hpp"

namespace {

const SwarmPeerRecord* find_peer_by_endpoint(const std::vector<SwarmPeerRecord>& peers,
                                             const std::string& host,
                                             int port) {
  const auto found = std::find_if(peers.begin(), peers.end(), [&](const auto& peer) {
    return peer.host == host && peer.port == port;
  });
  return found == peers.end() ? nullptr : &(*found);
}

bool contains_text(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  {
    AppState state;
    DiscoveryService discovery(state, 8789);

    discovery.bootstrap_peer({"192.168.43.11", 8788});
    discovery.bootstrap_peer({"192.168.43.11", 8789});

    const auto peers = state.swarm_peers_snapshot();
    assert(peers.size() == 2);

    const auto* first = find_peer_by_endpoint(peers, "192.168.43.11", 8788);
    const auto* second = find_peer_by_endpoint(peers, "192.168.43.11", 8789);
    assert(first != nullptr);
    assert(second != nullptr);
    assert(first->node_id != second->node_id);
    assert(first->source == "bootstrap");
    assert(second->source == "bootstrap");
  }

  {
    AppState state;
    DiscoveryService discovery(state, 8789);

    discovery.note_peer_hello("node-b", "192.168.43.12", 8788, "discovered");
    discovery.bootstrap_peer({"192.168.43.12", 8788});

    const auto peers = state.swarm_peers_snapshot();
    assert(peers.size() == 1);

    const auto* peer = find_peer_by_endpoint(peers, "192.168.43.12", 8788);
    assert(peer != nullptr);
    assert(peer->node_id == "node-b");
    assert(peer->source == "discovered");
    assert(peer->reachable);
  }

  {
    AppState state;

    SwarmPeerRecord peer;
    peer.node_id = "node-c";
    peer.host = "192.168.43.13";
    peer.port = 8788;
    peer.source = "discovered";
    peer.reachable = true;
    state.upsert_swarm_peer(peer);

    peer.port = 8790;
    peer.source = "manual";
    peer.reachable = false;
    state.upsert_swarm_peer(peer);

    const auto peers = state.swarm_peers_snapshot();
    assert(peers.size() == 1);
    assert(peers[0].node_id == "node-c");
    assert(peers[0].port == 8790);
    assert(peers[0].source == "manual");
    assert(!peers[0].reachable);
    assert(!peers[0].last_seen_at.empty());
  }

  {
    AppState state;

    TorrentLibraryEntry library_entry;
    library_entry.torrent_id = "torrent-1";
    library_entry.display_name = "Ubuntu ISO";
    library_entry.file_size = 1024;
    library_entry.piece_count = 4;
    library_entry.seeder_count = 2;
    library_entry.leecher_count = 1;
    library_entry.local_status = "available";
    state.replace_library_entry(library_entry);

    library_entry.display_name = "Ubuntu 24.04 ISO";
    library_entry.file_size = 2048;
    library_entry.local_status = "seeding";
    state.replace_library_entry(library_entry);

    const auto library = state.library_snapshot();
    assert(library.size() == 1);
    assert(library[0].display_name == "Ubuntu 24.04 ISO");
    assert(library[0].file_size == 2048);

    const auto library_json = state.library_json();
    assert(contains_text(library_json, "\"torrentId\":\"torrent-1\""));
    assert(contains_text(library_json, "\"displayName\":\"Ubuntu 24.04 ISO\""));
    assert(contains_text(library_json, "\"localStatus\":\"seeding\""));

    DownloadSessionRecord session;
    session.torrent_id = "torrent-1";
    session.display_name = "Ubuntu 24.04 ISO";
    session.status = "downloading";
    session.file_size = 2048;
    session.verified_pieces = 1;
    session.piece_count = 4;
    session.active_peers = {"node-b"};
    state.replace_download_session(session);

    session.status = "verifying";
    session.verified_pieces = 4;
    session.active_peers = {"node-b", "node-c"};
    state.replace_download_session(session);

    const auto sessions = state.download_sessions_snapshot();
    assert(sessions.size() == 1);
    assert(sessions[0].status == "verifying");
    assert(sessions[0].verified_pieces == 4);
    assert(sessions[0].active_peers.size() == 2);

    const auto downloads_json = state.downloads_json();
    assert(contains_text(downloads_json, "\"torrentId\":\"torrent-1\""));
    assert(contains_text(downloads_json, "\"status\":\"verifying\""));
    assert(contains_text(downloads_json, "\"activePeers\":[\"node-b\",\"node-c\"]"));
  }

  return 0;
}
