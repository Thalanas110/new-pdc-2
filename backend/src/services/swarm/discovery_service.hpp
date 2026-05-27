#pragma once

#include "models/app_state.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>

class DiscoveryService {
 public:
  using PeerDetectedCallback = std::function<void(const transfer::PeerEndpoint&)>;

  DiscoveryService(AppState& state, int discovery_port);

  void set_peer_detected_callback(PeerDetectedCallback callback);
  void start();
  void note_peer_hello(const std::string& node_id,
                       const std::string& host,
                       int port,
                       const std::string& source) const;
  void bootstrap_peer(const transfer::PeerEndpoint& peer);

 private:
  void announce_loop() const;
  void listen_loop() const;
  void notify_peer_detected(const transfer::PeerEndpoint& peer) const;

  AppState& state_;
  int discovery_port_ = 0;
  mutable std::mutex mutex_;
  mutable std::map<std::string, std::chrono::steady_clock::time_point> next_probe_at_;
  PeerDetectedCallback peer_detected_callback_;
};
