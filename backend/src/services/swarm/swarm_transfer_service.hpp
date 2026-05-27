#pragma once

#include "models/app_state.hpp"
#include "services/swarm/catalog_service.hpp"
#include "services/swarm/manifest_service.hpp"
#include "services/swarm/piece_scheduler.hpp"
#include "services/swarm/piece_store_service.hpp"
#include "shared/net_socket.hpp"

#include <optional>
#include <string>
#include <vector>

class SwarmTransferService {
 public:
  SwarmTransferService(AppState& state,
                       CatalogService& catalog,
                       ManifestService& manifest_service,
                       PieceStoreService& piece_store);

  void transfer_listener();
  bool bootstrap_peer(const transfer::PeerEndpoint& peer);
  std::optional<TorrentManifest> publish_file(const std::string& file_name, const std::vector<char>& body);
  void start_download(const TorrentManifest& manifest);
  void start_download_by_id(const std::string& torrent_id);
  void publish_from_http(socket_t client, const std::string& file_name, const std::vector<char>& body);
  void send_local_file_inline(socket_t client, const std::string& torrent_id) const;
  void send_local_file_attachment(socket_t client, const std::string& torrent_id) const;
  std::string downloads_json() const;

 private:
  transfer::PeerEndpoint local_endpoint() const;
  void update_download_session(const TorrentManifest& manifest,
                               const std::string& status,
                               std::uint64_t verified_pieces,
                               const std::vector<std::string>& active_peers) const;
  bool fetch_catalog_from_peer(const transfer::PeerEndpoint& peer);
  bool fetch_peers_from_peer(const transfer::PeerEndpoint& peer);
  bool send_hello_to_peer(const transfer::PeerEndpoint& peer) const;
  bool announce_manifest_to_peer(const transfer::PeerEndpoint& peer, const TorrentManifest& manifest) const;
  void announce_manifest_to_known_peers(const TorrentManifest& manifest, const std::string& exclude_peer_key = "");
  std::optional<std::vector<bool>> request_bitfield(const transfer::PeerEndpoint& peer,
                                                    const TorrentManifest& manifest) const;
  std::optional<std::vector<char>> request_piece(const transfer::PeerEndpoint& peer,
                                                 const TorrentManifest& manifest,
                                                 std::uint64_t piece_index) const;
  bool store_local_pieces(const TorrentManifest& manifest, const std::vector<char>& body);
  bool sync_receive_file(const TorrentManifest& manifest) const;
  void run_download(const TorrentManifest& manifest);
  void handle_swarm_client(socket_t client, const std::string& peer_host);
  void send_local_file_with_disposition(socket_t client,
                                        const std::string& torrent_id,
                                        const std::string& disposition) const;

  AppState& state_;
  CatalogService& catalog_;
  ManifestService& manifest_service_;
  PieceStoreService& piece_store_;
  PieceScheduler scheduler_;
};
