#pragma once

#include "models/app_state.hpp"

#include <string>

class DiscoveryService {
 public:
  DiscoveryService(AppState& state, int discovery_port);

  void note_peer_hello(const std::string& node_id,
                       const std::string& host,
                       int port,
                       const std::string& source);
  void bootstrap_peer(const transfer::PeerEndpoint& peer);

 private:
  AppState& state_;
  int discovery_port_ = 0;
};
