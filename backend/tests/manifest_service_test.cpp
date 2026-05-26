#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "services/swarm/manifest_service.hpp"

int main() {
  namespace fs = std::filesystem;

  const fs::path temp = fs::temp_directory_path() / "loopline-manifest-test.bin";
  std::ofstream output(temp, std::ios::binary);
  output << std::string(700000, 'x');
  output.close();

  ManifestService service;
  const auto manifest = service.build_manifest(temp, "demo.bin", "node-a");
  assert(manifest.has_value());
  assert(manifest->display_name == "demo.bin");
  assert(manifest->publisher_node_id == "node-a");
  assert(manifest->piece_size == 256 * 1024);
  assert(manifest->piece_count == 3);
  assert(manifest->piece_hashes.size() == 3);
  assert(!manifest->torrent_id.empty());

  fs::remove(temp);
  return 0;
}
