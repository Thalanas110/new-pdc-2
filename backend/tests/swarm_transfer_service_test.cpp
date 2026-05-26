#include <cassert>
#include <filesystem>

#include "models/app_state.hpp"
#include "services/swarm/catalog_service.hpp"
#include "services/swarm/manifest_service.hpp"
#include "services/swarm/piece_store_service.hpp"
#include "services/swarm/swarm_transfer_service.hpp"

int main() {
  namespace fs = std::filesystem;

  AppState state;
  ManifestService manifest_service;
  const fs::path root = fs::temp_directory_path() / "loopline-swarm-transfer-test";
  fs::remove_all(root);
  PieceStoreService piece_store(root);
  CatalogService catalog(state);
  SwarmTransferService transfer(state, catalog, manifest_service, piece_store);

  TorrentManifest manifest;
  manifest.torrent_id = "torrent-a";
  manifest.display_name = "demo.bin";
  manifest.file_size = 1024;
  manifest.piece_size = 256;
  manifest.piece_count = 4;

  transfer.start_download(manifest);

  const auto downloads = state.download_sessions_snapshot();
  assert(downloads.size() == 1);
  assert(downloads[0].torrent_id == "torrent-a");
  assert(downloads[0].status == "discovering");

  fs::remove_all(root);
  return 0;
}
