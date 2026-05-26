#pragma once

#include "models/app_state.hpp"
#include "services/swarm/catalog_service.hpp"
#include "services/swarm/manifest_service.hpp"
#include "services/swarm/piece_store_service.hpp"
#include "shared/net_socket.hpp"

#include <string>
#include <vector>

class SwarmTransferService {
 public:
  SwarmTransferService(AppState& state,
                       CatalogService& catalog,
                       ManifestService& manifest_service,
                       PieceStoreService& piece_store);

  void start_download(const TorrentManifest& manifest);
  void start_download_by_id(const std::string& torrent_id);
  void publish_from_http(socket_t client, const std::string& file_name, const std::vector<char>& body);
  std::string downloads_json() const;

 private:
  AppState& state_;
  CatalogService& catalog_;
  ManifestService& manifest_service_;
  PieceStoreService& piece_store_;
};
