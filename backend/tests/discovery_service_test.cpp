#include <cassert>
#include <string>

#include "models/app_state.hpp"
#include "services/swarm/discovery_service.hpp"

int main() {
  AppState state;
  DiscoveryService discovery(state, 8789);

  discovery.note_peer_hello("node-b", "192.168.43.11", 8788, "discovered");
  discovery.note_peer_hello("node-c", "192.168.43.12", 8788, "bootstrap");

  const auto peers = state.swarm_peers_snapshot();
  assert(peers.size() == 2);
  assert(peers[0].source == "discovered" || peers[1].source == "discovered");
  assert(peers[0].source == "bootstrap" || peers[1].source == "bootstrap");

  return 0;
}
