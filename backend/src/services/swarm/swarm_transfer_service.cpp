#include "services/swarm/swarm_transfer_service.hpp"

SwarmTransferService::SwarmTransferService(AppState& state,
                                           CatalogService& catalog,
                                           ManifestService& manifest_service,
                                           PieceStoreService& piece_store)
    : state_(state), catalog_(catalog), manifest_service_(manifest_service), piece_store_(piece_store) {}

void SwarmTransferService::start_download(const TorrentManifest& manifest) {
  DownloadSessionRecord session;
  session.torrent_id = manifest.torrent_id;
  session.display_name = manifest.display_name;
  session.status = "discovering";
  session.file_size = manifest.file_size;
  session.verified_pieces = 0;
  session.piece_count = manifest.piece_count;
  state_.replace_download_session(session);
}

void SwarmTransferService::start_download_by_id(const std::string& torrent_id) {
  for (const auto& entry : catalog_.library_snapshot()) {
    if (entry.torrent_id == torrent_id) {
      TorrentManifest manifest;
      manifest.torrent_id = entry.torrent_id;
      manifest.display_name = entry.display_name;
      manifest.file_size = entry.file_size;
      manifest.piece_count = entry.piece_count;
      start_download(manifest);
      return;
    }
  }
}

void SwarmTransferService::publish_from_http(socket_t client,
                                             const std::string& file_name,
                                             const std::vector<char>& body) {
  (void)client;
  (void)file_name;
  (void)body;
}

std::string SwarmTransferService::downloads_json() const {
  return state_.downloads_json();
}
