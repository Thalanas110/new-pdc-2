#include "services/swarm/discovery_service.hpp"

DiscoveryService::DiscoveryService(AppState& state, int discovery_port)
    : state_(state), discovery_port_(discovery_port) {}

void DiscoveryService::note_peer_hello(const std::string& node_id,
                                       const std::string& host,
                                       int port,
                                       const std::string& source) {
  SwarmPeerRecord peer;
  peer.node_id = node_id;
  peer.host = host;
  peer.port = port;
  peer.source = source;
  peer.reachable = true;
  state_.upsert_swarm_peer(peer);
}

void DiscoveryService::bootstrap_peer(const transfer::PeerEndpoint& peer) {
  note_peer_hello("bootstrap-" + peer.host, peer.host, peer.port, "bootstrap");
}
