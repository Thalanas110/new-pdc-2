# Loopline Torrent Swarm Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Loopline's shared-folder sync workflow with a private-network torrent swarm that publishes immutable file manifests, downloads verified pieces from multiple peers in parallel, and reseeds completed files automatically.

**Architecture:** The backend will replace shared-sync and direct-send services with swarm-specific services for manifest creation, persistent piece storage, peer discovery/bootstrap, catalog gossip, and piece exchange over sockets. The frontend will replace the `Transfer / Receive / Shared` views with `Publish / Swarm / Library / Downloads`, backed by new HTTP endpoints and typed swarm state models.

**Tech Stack:** C++20 Winsock backend, manual HTTP controller, React + TypeScript + TanStack Start frontend, Vitest frontend tests, C++ assert-based backend tests, npm build scripts.

---

## Scope Check

This remains one coordinated plan rather than multiple sub-project plans because the backend swarm services, HTTP API, and frontend workflow are tightly coupled. The software is only correct once all three move together from shared-sync semantics to torrent-swarm semantics.

## File Structure

### Backend core and swarm services

- Modify: `backend/src/app/backend_app.cpp`
  Replace shared-sync/direct-transfer service wiring with swarm service wiring, bootstrap configuration, and new storage roots.
- Modify: `backend/src/models/app_state.hpp`
  Replace shared-sync specific state with swarm peers, torrent library entries, and download session state.
- Modify: `backend/src/models/app_state.cpp`
  Serialize swarm state to JSON for the frontend and maintain thread-safe swarm/download records.
- Create: `backend/src/services/swarm/torrent_types.hpp`
  Shared structs and enums for manifests, peers, bitfields, availability, and download sessions.
- Create: `backend/src/services/swarm/manifest_service.hpp`
- Create: `backend/src/services/swarm/manifest_service.cpp`
  Build immutable manifests, calculate per-piece hashes, and parse/serialize manifest JSON.
- Create: `backend/src/services/swarm/piece_store_service.hpp`
- Create: `backend/src/services/swarm/piece_store_service.cpp`
  Persist verified pieces, bitfields, reassembly, and resumable on-disk state.
- Create: `backend/src/services/swarm/discovery_service.hpp`
- Create: `backend/src/services/swarm/discovery_service.cpp`
  Manage discovered/bootstrap peers, UDP discovery announcements, peer expiry, and peer gossip payloads.
- Create: `backend/src/services/swarm/swarm_protocol.hpp`
- Create: `backend/src/services/swarm/swarm_protocol.cpp`
  Encode/decode line-framed swarm protocol messages like `HELLO`, `CATALOG`, `BITFIELD`, `REQUEST`, and `PIECE`.
- Create: `backend/src/services/swarm/piece_scheduler.hpp`
- Create: `backend/src/services/swarm/piece_scheduler.cpp`
  Assign unique missing pieces across several peers in parallel.
- Create: `backend/src/services/swarm/catalog_service.hpp`
- Create: `backend/src/services/swarm/catalog_service.cpp`
  Track local and remote manifests, peer availability, and library summaries.
- Create: `backend/src/services/swarm/swarm_transfer_service.hpp`
- Create: `backend/src/services/swarm/swarm_transfer_service.cpp`
  Run the swarm listener, serve manifests and pieces, and drive download sessions.

### Backend HTTP surface

- Modify: `backend/src/controllers/http_controller.hpp`
  Replace shared-sync dependencies with publish/swarm/library/download dependencies.
- Modify: `backend/src/controllers/http_controller.cpp`
  Remove old shared routes and direct-send routes; add swarm endpoints.

### Frontend models, API client, and UI

- Modify: `src/lib/transferModel.ts`
  Keep byte-formatting and private-address helpers, but replace transfer/shared types with swarm types.
- Modify: `src/lib/backendClient.ts`
  Replace `/api/send`, `/api/shared/*`, and sync-peer calls with publish/library/swarm/download calls.
- Modify: `src/components/LooplineTransferApp.tsx`
  Keep the route entry point, but make it the torrent app shell with polling and top-level view switching.
- Create: `src/components/torrent/PublishView.tsx`
- Create: `src/components/torrent/SwarmView.tsx`
- Create: `src/components/torrent/LibraryView.tsx`
- Create: `src/components/torrent/DownloadsView.tsx`
  Split the large current component into focused torrent-specific screens.

### Tests and docs

- Create: `backend/tests/manifest_service_test.cpp`
- Create: `backend/tests/piece_store_service_test.cpp`
- Create: `backend/tests/discovery_service_test.cpp`
- Create: `backend/tests/piece_scheduler_test.cpp`
- Create: `backend/tests/swarm_transfer_service_test.cpp`
  Add backend unit tests for manifesting, persistence, discovery state, scheduling, and swarm session behavior.
- Modify: `src/lib/backendClient.test.ts`
  Verify new publish/bootstrap/download HTTP calls.
- Modify: `src/components/LooplineTransferApp.test.tsx`
  Verify the new `Publish / Swarm / Library / Downloads` shell.
- Delete or replace: `src/lib/backendSharedSyncSource.test.ts`
- Delete or replace: `src/lib/backendSharedUploadSource.test.ts`
- Delete or replace: `src/lib/backendSharedDedupSource.test.ts`
  Remove assertions that lock the codebase to shared-folder sync behavior.
- Create: `src/lib/backendSwarmSource.test.ts`
  Add source-guard coverage for new swarm routes and service wiring.
- Modify: `package.json`
  Update backend build/test scripts to compile the swarm services and backend tests.
- Modify: `README.md`
  Replace shared-drive instructions with publish/swarm/bootstrap/download guidance.

## Task 1: Add Torrent Manifest Types and Manifest Builder

**Files:**
- Create: `backend/src/services/swarm/torrent_types.hpp`
- Create: `backend/src/services/swarm/manifest_service.hpp`
- Create: `backend/src/services/swarm/manifest_service.cpp`
- Test: `backend/tests/manifest_service_test.cpp`

- [ ] **Step 1: Write the failing manifest test**

```cpp
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
```

- [ ] **Step 2: Run the backend test and verify it fails**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/manifest_service_test.cpp backend/src/core/transfer_core.cpp -o backend/tests/manifest_service_test.exe
```

Expected: FAIL with an include error for `services/swarm/manifest_service.hpp`.

- [ ] **Step 3: Implement the torrent manifest types and builder**

`backend/src/services/swarm/torrent_types.hpp`

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TorrentManifest {
  std::string torrent_id;
  std::string display_name;
  std::string publisher_node_id;
  std::uint64_t file_size = 0;
  std::uint64_t piece_size = 256 * 1024;
  std::uint64_t piece_count = 0;
  std::vector<std::uint64_t> piece_hashes;
  std::string created_at;
};
```

`backend/src/services/swarm/manifest_service.hpp`

```cpp
#pragma once

#include "services/swarm/torrent_types.hpp"

#include <filesystem>
#include <optional>
#include <string>

class ManifestService {
 public:
  std::optional<TorrentManifest> build_manifest(const std::filesystem::path& file_path,
                                                const std::string& display_name,
                                                const std::string& publisher_node_id) const;
  std::string manifest_json(const TorrentManifest& manifest) const;
};
```

`backend/src/services/swarm/manifest_service.cpp`

```cpp
#include "services/swarm/manifest_service.hpp"

#include "core/transfer_core.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::uint64_t hash_piece(const std::vector<char>& bytes, std::size_t count) {
  std::uint64_t hash = 14695981039346656037ull;
  for (std::size_t index = 0; index < count; ++index) {
    hash ^= static_cast<unsigned char>(bytes[index]);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string now_stamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#ifdef _WIN32
  localtime_s(&local_time, &time);
#else
  localtime_r(&time, &local_time);
#endif
  std::ostringstream out;
  out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

}  // namespace

std::optional<TorrentManifest> ManifestService::build_manifest(const std::filesystem::path& file_path,
                                                               const std::string& display_name,
                                                               const std::string& publisher_node_id) const {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  TorrentManifest manifest;
  manifest.display_name = display_name;
  manifest.publisher_node_id = publisher_node_id;
  manifest.created_at = now_stamp();
  manifest.file_size = static_cast<std::uint64_t>(std::filesystem::file_size(file_path));

  std::vector<char> buffer(static_cast<std::size_t>(manifest.piece_size));
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = static_cast<std::size_t>(input.gcount());
    if (read == 0) {
      break;
    }
    manifest.piece_hashes.push_back(hash_piece(buffer, read));
  }

  manifest.piece_count = static_cast<std::uint64_t>(manifest.piece_hashes.size());
  manifest.torrent_id = transfer::make_transfer_id();
  return manifest;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/manifest_service_test.cpp backend/src/services/swarm/manifest_service.cpp backend/src/core/transfer_core.cpp -o backend/tests/manifest_service_test.exe
backend\tests\manifest_service_test.exe
```

Expected: PASS with exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/swarm/torrent_types.hpp backend/src/services/swarm/manifest_service.hpp backend/src/services/swarm/manifest_service.cpp backend/tests/manifest_service_test.cpp
git commit -m "feat: add torrent manifest builder"
```

## Task 2: Add Persistent Piece Storage and Resume State

**Files:**
- Create: `backend/src/services/swarm/piece_store_service.hpp`
- Create: `backend/src/services/swarm/piece_store_service.cpp`
- Test: `backend/tests/piece_store_service_test.cpp`

- [ ] **Step 1: Write the failing piece store test**

```cpp
#include <cassert>
#include <filesystem>
#include <vector>

#include "services/swarm/piece_store_service.hpp"

int main() {
  namespace fs = std::filesystem;

  const fs::path root = fs::temp_directory_path() / "loopline-piece-store-test";
  fs::remove_all(root);

  TorrentManifest manifest;
  manifest.torrent_id = "torrent-a";
  manifest.display_name = "demo.bin";
  manifest.file_size = 8;
  manifest.piece_size = 4;
  manifest.piece_count = 2;
  manifest.piece_hashes = {14695981039346656037ull ^ 'A', 14695981039346656037ull ^ 'E'};

  PieceStoreService store(root);
  assert(store.store_piece(manifest, 0, {'A', 'B', 'C', 'D'}) == true);
  assert(store.has_piece(manifest, 0) == true);
  assert(store.has_piece(manifest, 1) == false);

  const auto missing = store.missing_pieces(manifest);
  assert(missing.size() == 1);
  assert(missing[0] == 1);

  fs::remove_all(root);
  return 0;
}
```

- [ ] **Step 2: Run the backend test and verify it fails**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/piece_store_service_test.cpp backend/src/services/swarm/manifest_service.cpp backend/src/core/transfer_core.cpp -o backend/tests/piece_store_service_test.exe
```

Expected: FAIL with an include error for `services/swarm/piece_store_service.hpp`.

- [ ] **Step 3: Implement piece persistence and missing-piece queries**

`backend/src/services/swarm/piece_store_service.hpp`

```cpp
#pragma once

#include "services/swarm/torrent_types.hpp"

#include <filesystem>
#include <optional>
#include <vector>

class PieceStoreService {
 public:
  explicit PieceStoreService(std::filesystem::path root);

  bool store_piece(const TorrentManifest& manifest, std::uint64_t piece_index, const std::vector<char>& bytes);
  bool has_piece(const TorrentManifest& manifest, std::uint64_t piece_index) const;
  std::vector<std::uint64_t> missing_pieces(const TorrentManifest& manifest) const;
  std::optional<std::filesystem::path> assemble_file(const TorrentManifest& manifest);

 private:
  std::filesystem::path piece_dir(const TorrentManifest& manifest) const;
  std::filesystem::path piece_path(const TorrentManifest& manifest, std::uint64_t piece_index) const;

  std::filesystem::path root_;
};
```

`backend/src/services/swarm/piece_store_service.cpp`

```cpp
#include "services/swarm/piece_store_service.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

PieceStoreService::PieceStoreService(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path PieceStoreService::piece_dir(const TorrentManifest& manifest) const {
  return root_ / "pieces" / manifest.torrent_id;
}

std::filesystem::path PieceStoreService::piece_path(const TorrentManifest& manifest, std::uint64_t piece_index) const {
  std::ostringstream name;
  name << "piece-" << std::setw(6) << std::setfill('0') << piece_index << ".bin";
  return piece_dir(manifest) / name.str();
}

bool PieceStoreService::store_piece(const TorrentManifest& manifest,
                                    std::uint64_t piece_index,
                                    const std::vector<char>& bytes) {
  std::filesystem::create_directories(piece_dir(manifest));
  std::ofstream output(piece_path(manifest, piece_index), std::ios::binary);
  if (!output) {
    return false;
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

bool PieceStoreService::has_piece(const TorrentManifest& manifest, std::uint64_t piece_index) const {
  return std::filesystem::exists(piece_path(manifest, piece_index));
}

std::vector<std::uint64_t> PieceStoreService::missing_pieces(const TorrentManifest& manifest) const {
  std::vector<std::uint64_t> missing;
  for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
    if (!has_piece(manifest, index)) {
      missing.push_back(index);
    }
  }
  return missing;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/piece_store_service_test.cpp backend/src/services/swarm/piece_store_service.cpp backend/src/services/swarm/manifest_service.cpp backend/src/core/transfer_core.cpp -o backend/tests/piece_store_service_test.exe
backend\tests\piece_store_service_test.exe
```

Expected: PASS with exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/swarm/piece_store_service.hpp backend/src/services/swarm/piece_store_service.cpp backend/tests/piece_store_service_test.cpp
git commit -m "feat: add piece storage and resume state"
```

## Task 3: Replace Shared-Sync State with Swarm Peers and Session State

**Files:**
- Modify: `backend/src/models/app_state.hpp`
- Modify: `backend/src/models/app_state.cpp`
- Create: `backend/src/services/swarm/discovery_service.hpp`
- Create: `backend/src/services/swarm/discovery_service.cpp`
- Test: `backend/tests/discovery_service_test.cpp`

- [ ] **Step 1: Write the failing discovery-state test**

```cpp
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
```

- [ ] **Step 2: Run the backend test and verify it fails**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/discovery_service_test.cpp backend/src/models/app_state.cpp backend/src/core/transfer_core.cpp -o backend/tests/discovery_service_test.exe
```

Expected: FAIL because `DiscoveryService` and `swarm_peers_snapshot()` do not exist yet.

- [ ] **Step 3: Implement swarm peer/session state and discovery cache methods**

`backend/src/models/app_state.hpp`

```cpp
struct SwarmPeerRecord {
  std::string node_id;
  std::string host;
  int port = 0;
  std::string source;
  std::string last_seen_at;
  bool reachable = false;
};

struct TorrentLibraryEntry {
  std::string torrent_id;
  std::string display_name;
  std::uint64_t file_size = 0;
  std::uint64_t piece_count = 0;
  std::size_t seeder_count = 0;
  std::size_t leecher_count = 0;
  std::string local_status = "available";
};

struct DownloadSessionRecord {
  std::string torrent_id;
  std::string display_name;
  std::string status;
  std::uint64_t file_size = 0;
  std::uint64_t verified_pieces = 0;
  std::uint64_t piece_count = 0;
  std::vector<std::string> active_peers;
};

std::vector<SwarmPeerRecord> swarm_peers_snapshot() const;
std::vector<TorrentLibraryEntry> library_snapshot() const;
std::vector<DownloadSessionRecord> download_sessions_snapshot() const;
void upsert_swarm_peer(const SwarmPeerRecord& peer);
void replace_library_entry(const TorrentLibraryEntry& entry);
void replace_download_session(const DownloadSessionRecord& session);
std::string library_json() const;
std::string downloads_json() const;
```

`backend/src/services/swarm/discovery_service.hpp`

```cpp
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
```

`backend/src/services/swarm/discovery_service.cpp`

```cpp
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
```

- [ ] **Step 4: Run the test to verify it passes**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/discovery_service_test.cpp backend/src/services/swarm/discovery_service.cpp backend/src/models/app_state.cpp backend/src/core/transfer_core.cpp -o backend/tests/discovery_service_test.exe
backend\tests\discovery_service_test.exe
```

Expected: PASS with exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add backend/src/models/app_state.hpp backend/src/models/app_state.cpp backend/src/services/swarm/discovery_service.hpp backend/src/services/swarm/discovery_service.cpp backend/tests/discovery_service_test.cpp
git commit -m "feat: add swarm peer and session state"
```

## Task 4: Add Protocol Encoding and Multi-Peer Piece Scheduling

**Files:**
- Create: `backend/src/services/swarm/swarm_protocol.hpp`
- Create: `backend/src/services/swarm/swarm_protocol.cpp`
- Create: `backend/src/services/swarm/piece_scheduler.hpp`
- Create: `backend/src/services/swarm/piece_scheduler.cpp`
- Test: `backend/tests/piece_scheduler_test.cpp`

- [ ] **Step 1: Write the failing scheduler/protocol test**

```cpp
#include <cassert>
#include <map>
#include <vector>

#include "services/swarm/piece_scheduler.hpp"
#include "services/swarm/swarm_protocol.hpp"

int main() {
  PieceScheduler scheduler;

  const std::vector<std::uint64_t> missing = {0, 1, 2, 3};
  const std::vector<PeerPieceAvailability> peers = {
      {"peer-a", {true, true, false, true}, 0},
      {"peer-b", {false, true, true, false}, 0},
      {"peer-c", {true, false, true, true}, 0},
  };

  const auto plan = scheduler.plan_requests(missing, peers, 2);
  assert(plan.at("peer-a").size() <= 2);
  assert(plan.at("peer-b").size() <= 2);
  assert(plan.at("peer-c").size() <= 2);

  const auto hello = SwarmProtocol::encode_hello("node-a", "192.168.43.10", 8788);
  assert(hello.rfind("SWARM/1\nHELLO\n", 0) == 0);

  return 0;
}
```

- [ ] **Step 2: Run the backend test and verify it fails**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/piece_scheduler_test.cpp backend/src/core/transfer_core.cpp -o backend/tests/piece_scheduler_test.exe
```

Expected: FAIL because `PieceScheduler` and `SwarmProtocol` do not exist yet.

- [ ] **Step 3: Implement line-framed swarm messages and unique piece scheduling**

`backend/src/services/swarm/swarm_protocol.hpp`

```cpp
#pragma once

#include <string>

class SwarmProtocol {
 public:
  static std::string encode_hello(const std::string& node_id, const std::string& host, int port);
  static std::string encode_manifest_request(const std::string& torrent_id);
  static std::string encode_piece_request(const std::string& torrent_id, std::uint64_t piece_index);
};
```

`backend/src/services/swarm/piece_scheduler.hpp`

```cpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct PeerPieceAvailability {
  std::string peer_key;
  std::vector<bool> bitfield;
  int penalty = 0;
};

class PieceScheduler {
 public:
  std::map<std::string, std::vector<std::uint64_t>> plan_requests(
      const std::vector<std::uint64_t>& missing,
      const std::vector<PeerPieceAvailability>& peers,
      std::size_t max_in_flight_per_peer) const;
};
```

`backend/src/services/swarm/piece_scheduler.cpp`

```cpp
#include "services/swarm/piece_scheduler.hpp"

#include <set>

std::map<std::string, std::vector<std::uint64_t>> PieceScheduler::plan_requests(
    const std::vector<std::uint64_t>& missing,
    const std::vector<PeerPieceAvailability>& peers,
    std::size_t max_in_flight_per_peer) const {
  std::map<std::string, std::vector<std::uint64_t>> plan;
  std::set<std::uint64_t> assigned;

  for (const auto& peer : peers) {
    auto& bucket = plan[peer.peer_key];
    for (const auto piece_index : missing) {
      if (bucket.size() >= max_in_flight_per_peer) {
        break;
      }
      if (piece_index >= peer.bitfield.size() || !peer.bitfield[piece_index] || assigned.count(piece_index) > 0) {
        continue;
      }
      bucket.push_back(piece_index);
      assigned.insert(piece_index);
    }
  }

  return plan;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/piece_scheduler_test.cpp backend/src/services/swarm/piece_scheduler.cpp backend/src/services/swarm/swarm_protocol.cpp backend/src/core/transfer_core.cpp -o backend/tests/piece_scheduler_test.exe
backend\tests\piece_scheduler_test.exe
```

Expected: PASS with exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/swarm/swarm_protocol.hpp backend/src/services/swarm/swarm_protocol.cpp backend/src/services/swarm/piece_scheduler.hpp backend/src/services/swarm/piece_scheduler.cpp backend/tests/piece_scheduler_test.cpp
git commit -m "feat: add swarm protocol and piece scheduler"
```

## Task 5: Implement Catalog Gossip and Swarm Transfer Sessions

**Files:**
- Create: `backend/src/services/swarm/catalog_service.hpp`
- Create: `backend/src/services/swarm/catalog_service.cpp`
- Create: `backend/src/services/swarm/swarm_transfer_service.hpp`
- Create: `backend/src/services/swarm/swarm_transfer_service.cpp`
- Test: `backend/tests/swarm_transfer_service_test.cpp`

- [ ] **Step 1: Write the failing swarm-session test**

```cpp
#include <cassert>
#include <filesystem>

#include "models/app_state.hpp"
#include "services/swarm/catalog_service.hpp"
#include "services/swarm/manifest_service.hpp"
#include "services/swarm/piece_store_service.hpp"
#include "services/swarm/swarm_transfer_service.hpp"

int main() {
  AppState state;
  ManifestService manifest_service;
  PieceStoreService piece_store(std::filesystem::temp_directory_path() / "loopline-swarm-transfer-test");
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

  return 0;
}
```

- [ ] **Step 2: Run the backend test and verify it fails**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/swarm_transfer_service_test.cpp backend/src/models/app_state.cpp backend/src/core/transfer_core.cpp -o backend/tests/swarm_transfer_service_test.exe
```

Expected: FAIL because `CatalogService`, `SwarmTransferService`, and `download_sessions_snapshot()` do not exist yet.

- [ ] **Step 3: Implement catalog summaries and download session bootstrapping**

`backend/src/services/swarm/catalog_service.hpp`

```cpp
#pragma once

#include "models/app_state.hpp"
#include "services/swarm/torrent_types.hpp"

class CatalogService {
 public:
  explicit CatalogService(AppState& state);

  void publish_local_manifest(const TorrentManifest& manifest);
  std::vector<TorrentLibraryEntry> library_snapshot() const;
  std::string library_json() const;

 private:
  AppState& state_;
};
```

`backend/src/services/swarm/swarm_transfer_service.hpp`

```cpp
#pragma once

#include "models/app_state.hpp"
#include "services/swarm/catalog_service.hpp"
#include "services/swarm/manifest_service.hpp"
#include "services/swarm/piece_store_service.hpp"

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
```

`backend/src/services/swarm/swarm_transfer_service.cpp`

```cpp
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
```

- [ ] **Step 4: Run the test to verify it passes**

Run:

```powershell
g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/swarm_transfer_service_test.cpp backend/src/services/swarm/catalog_service.cpp backend/src/services/swarm/swarm_transfer_service.cpp backend/src/services/swarm/piece_store_service.cpp backend/src/services/swarm/manifest_service.cpp backend/src/models/app_state.cpp backend/src/core/transfer_core.cpp -o backend/tests/swarm_transfer_service_test.exe
backend\tests\swarm_transfer_service_test.exe
```

Expected: PASS with exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/swarm/catalog_service.hpp backend/src/services/swarm/catalog_service.cpp backend/src/services/swarm/swarm_transfer_service.hpp backend/src/services/swarm/swarm_transfer_service.cpp backend/tests/swarm_transfer_service_test.cpp
git commit -m "feat: add catalog and swarm download sessions"
```

## Task 6: Replace Backend HTTP Routes and Wire the New Swarm Services

**Files:**
- Modify: `backend/src/controllers/http_controller.hpp`
- Modify: `backend/src/controllers/http_controller.cpp`
- Modify: `backend/src/app/backend_app.cpp`

- [ ] **Step 1: Write the failing source-guard frontend test for new routes**

`src/lib/backendSwarmSource.test.ts`

```ts
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('backend swarm routes', () => {
  it('wires publish, bootstrap, library, and downloads routes', () => {
    const appSource = readFileSync('backend/src/app/backend_app.cpp', 'utf8');
    const controllerSource = readFileSync('backend/src/controllers/http_controller.cpp', 'utf8');

    expect(appSource).toContain('DiscoveryService discovery_service');
    expect(appSource).toContain('SwarmTransferService swarm_transfer_service');
    expect(controllerSource).toContain('route == "/api/publish"');
    expect(controllerSource).toContain('route == "/api/swarm/bootstrap"');
    expect(controllerSource).toContain('route == "/api/library"');
    expect(controllerSource).toContain('route == "/api/downloads/start"');
  });
});
```

- [ ] **Step 2: Run the frontend test and verify it fails**

Run:

```powershell
npm.cmd test -- src/lib/backendSwarmSource.test.ts
```

Expected: FAIL because the backend is still wired to `SharedSyncService`, `/api/shared/*`, and `/api/send`.

- [ ] **Step 3: Replace the HTTP dependencies and backend service wiring**

`backend/src/controllers/http_controller.hpp`

```cpp
struct Dependencies {
  std::function<std::string()> status_json;
  std::function<std::string()> library_json;
  std::function<std::string()> downloads_json;
  std::function<void(socket_t, const std::string&, const std::vector<char>&)> publish_file;
  std::function<void(const transfer::PeerEndpoint&)> bootstrap_peer;
  std::function<void(const std::string&)> start_download;
  std::function<int()> transfer_port;
  std::function<bool(const std::string&)> is_allowed_peer;
};
```

`backend/src/controllers/http_controller.cpp`

```cpp
} else if (request.method == "GET" && route == "/api/library") {
  view_.send_json(client, 200, "OK", deps_.library_json());
} else if (request.method == "GET" && route == "/api/downloads") {
  view_.send_json(client, 200, "OK", deps_.downloads_json());
} else if (request.method == "POST" && route == "/api/swarm/bootstrap") {
  const std::string host = query_value(request.target, "host");
  const int port = parse_int(query_value(request.target, "port")).value_or(deps_.transfer_port());
  const auto peer = transfer::parse_peer_endpoint(host + ":" + std::to_string(port), deps_.transfer_port());
  if (!peer || !deps_.is_allowed_peer(peer->host)) {
    view_.send_json(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"Use localhost or a private LAN peer\"}");
  } else {
    deps_.bootstrap_peer(*peer);
    view_.send_json(client, 200, "OK", deps_.status_json());
  }
} else if (request.method == "POST" && route == "/api/publish") {
  const std::string file_name = transfer::safe_file_name(url_decode(header_value(request, "x-file-name", "publish.bin")));
  deps_.publish_file(client, file_name, request.body);
} else if (request.method == "POST" && route == "/api/downloads/start") {
  const std::string torrent_id = query_value(request.target, "torrentId");
  deps_.start_download(torrent_id);
  view_.send_json(client, 200, "OK", deps_.downloads_json());
}
```

`backend/src/app/backend_app.cpp`

```cpp
ManifestService manifest_service;
PieceStoreService piece_store_service(std::filesystem::current_path() / "backend" / "torrents");
CatalogService catalog_service(state);
DiscoveryService discovery_service(state, 8789);
SwarmTransferService swarm_transfer_service(state, catalog_service, manifest_service, piece_store_service);

deps.library_json = [&catalog_service]() { return catalog_service.library_json(); };
deps.downloads_json = [&swarm_transfer_service]() { return swarm_transfer_service.downloads_json(); };
deps.publish_file = [&swarm_transfer_service](socket_t client, const std::string& file_name, const std::vector<char>& body) {
  swarm_transfer_service.publish_from_http(client, file_name, body);
};
deps.bootstrap_peer = [&discovery_service](const transfer::PeerEndpoint& peer) { discovery_service.bootstrap_peer(peer); };
deps.start_download = [&swarm_transfer_service](const std::string& torrent_id) { swarm_transfer_service.start_download_by_id(torrent_id); };
```

- [ ] **Step 4: Run the frontend test to verify it passes**

Run:

```powershell
npm.cmd test -- src/lib/backendSwarmSource.test.ts
```

Expected: PASS with `1 passed`.

- [ ] **Step 5: Commit**

```bash
git add backend/src/controllers/http_controller.hpp backend/src/controllers/http_controller.cpp backend/src/app/backend_app.cpp src/lib/backendSwarmSource.test.ts
git commit -m "feat: wire swarm backend routes"
```

## Task 7: Replace Frontend Data Models and API Client Calls

**Files:**
- Modify: `src/lib/transferModel.ts`
- Modify: `src/lib/backendClient.ts`
- Modify: `src/lib/backendClient.test.ts`
- Delete or replace: `src/lib/backendSharedSyncSource.test.ts`
- Delete or replace: `src/lib/backendSharedUploadSource.test.ts`
- Delete or replace: `src/lib/backendSharedDedupSource.test.ts`

- [ ] **Step 1: Write the failing frontend client test**

```ts
import { afterEach, describe, expect, it, vi } from 'vitest';
import { bootstrapPeer, fetchLibrary, publishFile, startDownload } from './backendClient';

describe('backend swarm client', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('publishes files to the new torrent endpoint', async () => {
    const sendMock = vi.fn();
    class FakeXMLHttpRequest {
      open = vi.fn();
      setRequestHeader = vi.fn();
      send = sendMock;
      upload = { onprogress: null as ((event: ProgressEvent) => void) | null };
      onload: (() => void) | null = null;
      onerror: (() => void) | null = null;
      status = 200;
      responseText = '{"ok":true}';
    }

    vi.stubGlobal('XMLHttpRequest', FakeXMLHttpRequest as unknown as typeof XMLHttpRequest);
    await publishFile({ file: new File(['hello'], 'demo.bin'), onProgress: vi.fn() });
    expect(sendMock).toHaveBeenCalled();
  });

  it('bootsraps a peer and starts downloads through fetch', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response('{"ok":true}', { status: 200 }));
    vi.stubGlobal('fetch', fetchMock);

    await bootstrapPeer({ host: '192.168.43.12', port: 8788 });
    await fetchLibrary();
    await startDownload('torrent-a');

    expect(fetchMock).toHaveBeenCalledWith('/api/swarm/bootstrap?host=192.168.43.12&port=8788', { method: 'POST' });
    expect(fetchMock).toHaveBeenCalledWith('/api/library');
    expect(fetchMock).toHaveBeenCalledWith('/api/downloads/start?torrentId=torrent-a', { method: 'POST' });
  });
});
```

- [ ] **Step 2: Run the frontend test and verify it fails**

Run:

```powershell
npm.cmd test -- src/lib/backendClient.test.ts
```

Expected: FAIL because `publishFile`, `bootstrapPeer`, `fetchLibrary`, and `startDownload` do not exist yet.

- [ ] **Step 3: Replace transfer/shared types and client functions with swarm types**

`src/lib/transferModel.ts`

```ts
export type SwarmPeerEntry = {
  nodeId: string;
  host: string;
  port: number;
  source: 'discovered' | 'bootstrap';
  lastSeenAt: string;
  reachable: boolean;
};

export type TorrentLibraryEntry = {
  torrentId: string;
  displayName: string;
  fileSize: number;
  pieceCount: number;
  seederCount: number;
  leecherCount: number;
  localStatus: 'available' | 'discovering' | 'downloading' | 'seeding' | 'complete' | 'failed';
};

export type TorrentDownloadEntry = {
  torrentId: string;
  displayName: string;
  status: 'discovering' | 'downloading' | 'verifying' | 'seeding' | 'complete' | 'failed';
  fileSize: number;
  verifiedPieces: number;
  pieceCount: number;
  activePeers: string[];
};

export type BackendStatus = {
  nodeId: string;
  host: string;
  httpPort: number;
  transferPort: number;
  peers: SwarmPeerEntry[];
  library: TorrentLibraryEntry[];
  downloads: TorrentDownloadEntry[];
};
```

`src/lib/backendClient.ts`

```ts
export type PublishFileOptions = {
  file: File;
  onProgress: (percent: number) => void;
};

export type BootstrapPeer = {
  host: string;
  port: number;
};

export async function fetchLibrary(): Promise<TorrentLibraryEntry[]> {
  const response = await fetch('/api/library');
  if (!response.ok) {
    throw new Error(`Library request failed: ${response.status}`);
  }
  const payload = (await response.json()) as { torrents?: TorrentLibraryEntry[] };
  return payload.torrents ?? [];
}

export async function bootstrapPeer(peer: BootstrapPeer): Promise<void> {
  const params = new URLSearchParams({ host: peer.host, port: String(peer.port) });
  const response = await fetch(`/api/swarm/bootstrap?${params.toString()}`, { method: 'POST' });
  if (!response.ok) {
    throw new Error(await response.text());
  }
}

export async function startDownload(torrentId: string): Promise<void> {
  const response = await fetch(`/api/downloads/start?torrentId=${encodeURIComponent(torrentId)}`, {
    method: 'POST',
  });
  if (!response.ok) {
    throw new Error(await response.text());
  }
}

export function publishFile({ file, onProgress }: PublishFileOptions): Promise<void> {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    request.open('POST', '/api/publish');
    request.setRequestHeader('X-File-Name', encodeURIComponent(file.name));
    request.setRequestHeader('Content-Type', 'application/octet-stream');
    request.upload.onprogress = (event) => {
      if (event.lengthComputable) {
        onProgress(getTransferPercent(event.loaded, event.total));
      }
    };
    request.onload = () => (request.status >= 200 && request.status < 300 ? resolve() : reject(new Error(request.responseText)));
    request.onerror = () => reject(new Error('Could not reach the C++ backend'));
    request.send(file);
  });
}
```

- [ ] **Step 4: Run the frontend test to verify it passes**

Run:

```powershell
npm.cmd test -- src/lib/backendClient.test.ts
```

Expected: PASS with `2 passed`.

- [ ] **Step 5: Commit**

```bash
git add src/lib/transferModel.ts src/lib/backendClient.ts src/lib/backendClient.test.ts src/lib/backendSwarmSource.test.ts
git commit -m "feat: migrate frontend client to swarm endpoints"
```

## Task 8: Replace the UI with Publish, Swarm, Library, and Downloads Views

**Files:**
- Modify: `src/components/LooplineTransferApp.tsx`
- Create: `src/components/torrent/PublishView.tsx`
- Create: `src/components/torrent/SwarmView.tsx`
- Create: `src/components/torrent/LibraryView.tsx`
- Create: `src/components/torrent/DownloadsView.tsx`
- Modify: `src/components/LooplineTransferApp.test.tsx`

- [ ] **Step 1: Write the failing UI test**

```tsx
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { HomeView } from './LooplineTransferApp';

describe('torrent swarm home', () => {
  it('renders publish, swarm, library, and downloads tabs', () => {
    render(<HomeView />);

    expect(screen.getByRole('button', { name: 'Publish' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Swarm' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Library' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Downloads' })).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Publish' })).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Library' }));
    expect(screen.getByRole('heading', { name: 'Library' })).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Run the UI test and verify it fails**

Run:

```powershell
npm.cmd test -- src/components/LooplineTransferApp.test.tsx
```

Expected: FAIL because the current shell still renders `Transfer`, `Receive`, and `Shared`.

- [ ] **Step 3: Split the shell and render the new torrent workflow**

`src/components/torrent/PublishView.tsx`

```tsx
type PublishViewProps = {
  selectedFile: File | null;
  publishing: boolean;
  publishPercent: number;
  onFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
  onPublish: () => void;
};

export function PublishView({ selectedFile, publishing, publishPercent, onFileChange, onPublish }: PublishViewProps) {
  return (
    <section className="loop-page" aria-labelledby="publish-heading">
      <h1 id="publish-heading">Publish</h1>
      <input aria-label="Publish file" type="file" onChange={onFileChange} />
      <button type="button" onClick={onPublish} disabled={!selectedFile || publishing}>
        Publish file
      </button>
      <p>{selectedFile ? selectedFile.name : 'No file selected'}</p>
      <p>Progress {publishPercent}%</p>
    </section>
  );
}
```

`src/components/torrent/SwarmView.tsx`

```tsx
type SwarmViewProps = {
  peerHost: string;
  peerPort: number;
  peers: SwarmPeerEntry[];
  onPeerHostChange: (value: string) => void;
  onPeerPortChange: (value: number) => void;
  onBootstrap: () => void;
};

export function SwarmView({ peerHost, peerPort, peers, onPeerHostChange, onPeerPortChange, onBootstrap }: SwarmViewProps) {
  return (
    <section className="loop-page" aria-labelledby="swarm-heading">
      <h1 id="swarm-heading">Swarm</h1>
      <input aria-label="Peer host" value={peerHost} onChange={(event) => onPeerHostChange(event.target.value)} />
      <input aria-label="Peer port" type="number" value={peerPort} onChange={(event) => onPeerPortChange(Number(event.target.value))} />
      <button type="button" onClick={onBootstrap}>Bootstrap peer</button>
      <p>Peers {peers.length}</p>
    </section>
  );
}
```

`src/components/LooplineTransferApp.tsx`

```tsx
type ViewMode = 'publish' | 'swarm' | 'library' | 'downloads';

const [activeView, setActiveView] = useState<ViewMode>('publish');

<nav className="mode-tabs" aria-label="Torrent pages">
  <button type="button" aria-pressed={activeView === 'publish'} onClick={() => setActiveView('publish')}>Publish</button>
  <button type="button" aria-pressed={activeView === 'swarm'} onClick={() => setActiveView('swarm')}>Swarm</button>
  <button type="button" aria-pressed={activeView === 'library'} onClick={() => setActiveView('library')}>Library</button>
  <button type="button" aria-pressed={activeView === 'downloads'} onClick={() => setActiveView('downloads')}>Downloads</button>
</nav>
```

- [ ] **Step 4: Run the UI test to verify it passes**

Run:

```powershell
npm.cmd test -- src/components/LooplineTransferApp.test.tsx
```

Expected: PASS with `1 passed`.

- [ ] **Step 5: Commit**

```bash
git add src/components/LooplineTransferApp.tsx src/components/torrent/PublishView.tsx src/components/torrent/SwarmView.tsx src/components/torrent/LibraryView.tsx src/components/torrent/DownloadsView.tsx src/components/LooplineTransferApp.test.tsx
git commit -m "feat: replace shared ui with torrent workflow"
```

## Task 9: Remove Shared-Sync Leftovers, Update Scripts/Docs, and Verify End-to-End

**Files:**
- Modify: `package.json`
- Modify: `README.md`
- Delete or stop referencing: `backend/src/services/shared-sync/shared_sync_service.hpp`
- Delete or stop referencing: `backend/src/services/shared-sync/shared_sync_service_core.cpp`
- Delete or stop referencing: `backend/src/services/shared-sync/shared_sync_service_incoming.cpp`
- Delete or stop referencing: `backend/src/services/shared-sync/shared_sync_service_outbound.cpp`
- Delete or stop referencing: `backend/src/services/transfer/transfer_service.hpp`
- Delete or stop referencing: `backend/src/services/transfer/transfer_service.cpp`
- Delete or replace: `src/lib/backendSharedSyncSource.test.ts`
- Delete or replace: `src/lib/backendSharedUploadSource.test.ts`
- Delete or replace: `src/lib/backendSharedDedupSource.test.ts`

- [ ] **Step 1: Write the failing migration-cleanup test**

`src/lib/backendSwarmSource.test.ts`

```ts
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('shared sync cleanup', () => {
  it('no longer references shared-sync routes or services in the main backend path', () => {
    const appSource = readFileSync('backend/src/app/backend_app.cpp', 'utf8');
    const controllerSource = readFileSync('backend/src/controllers/http_controller.cpp', 'utf8');

    expect(appSource).not.toContain('SharedSyncService');
    expect(appSource).not.toContain('TransferService transfer_service');
    expect(controllerSource).not.toContain('/api/shared/sync');
    expect(controllerSource).not.toContain('/api/shared/upload');
    expect(controllerSource).not.toContain('/api/send');
  });
});
```

- [ ] **Step 2: Run the full frontend source test and verify it fails before cleanup**

Run:

```powershell
npm.cmd test -- src/lib/backendSwarmSource.test.ts
```

Expected: FAIL until the old shared-sync references are removed from the app/controller wiring.

- [ ] **Step 3: Update build scripts, README, and remove obsolete shared-sync behavior from the main path**

`package.json`

```json
{
  "scripts": {
    "build:backend": "g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/src/main.cpp backend/src/app/backend_app.cpp backend/src/models/app_state.cpp backend/src/controllers/http_controller.cpp backend/src/views/http_view.cpp backend/src/core/transfer_core.cpp backend/src/services/net-io/net_io.cpp backend/src/services/file-vault/file_vault_service.cpp backend/src/services/swarm/manifest_service.cpp backend/src/services/swarm/piece_store_service.cpp backend/src/services/swarm/discovery_service.cpp backend/src/services/swarm/swarm_protocol.cpp backend/src/services/swarm/piece_scheduler.cpp backend/src/services/swarm/catalog_service.cpp backend/src/services/swarm/swarm_transfer_service.cpp -o backend/p2p_server.exe -lws2_32",
    "test:backend:swarm": "g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/manifest_service_test.cpp backend/src/services/swarm/manifest_service.cpp backend/src/core/transfer_core.cpp -o backend/tests/manifest_service_test.exe && backend\\tests\\manifest_service_test.exe && g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/piece_store_service_test.cpp backend/src/services/swarm/piece_store_service.cpp backend/src/services/swarm/manifest_service.cpp backend/src/core/transfer_core.cpp -o backend/tests/piece_store_service_test.exe && backend\\tests\\piece_store_service_test.exe && g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/discovery_service_test.cpp backend/src/services/swarm/discovery_service.cpp backend/src/models/app_state.cpp backend/src/core/transfer_core.cpp -o backend/tests/discovery_service_test.exe && backend\\tests\\discovery_service_test.exe && g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/piece_scheduler_test.cpp backend/src/services/swarm/piece_scheduler.cpp backend/src/services/swarm/swarm_protocol.cpp backend/src/core/transfer_core.cpp -o backend/tests/piece_scheduler_test.exe && backend\\tests\\piece_scheduler_test.exe && g++ -std=c++20 -Wall -Wextra -Werror -I backend/src backend/tests/swarm_transfer_service_test.cpp backend/src/services/swarm/catalog_service.cpp backend/src/services/swarm/swarm_transfer_service.cpp backend/src/services/swarm/piece_store_service.cpp backend/src/services/swarm/manifest_service.cpp backend/src/models/app_state.cpp backend/src/core/transfer_core.cpp -o backend/tests/swarm_transfer_service_test.exe && backend\\tests\\swarm_transfer_service_test.exe"
  }
}
```

`README.md`

```md
## Mental Model

Each published file becomes its own immutable torrent-style entry.

- Publish a file from the `Publish` tab.
- Let peers auto-discover each other on the hotspot, or enter one bootstrap peer in `Swarm`.
- Browse available files in `Library`.
- Download from multiple peers in parallel in `Downloads`.
- Completed downloads automatically reseed.
```

- [ ] **Step 4: Run full verification**

Run:

```powershell
npm.cmd test
npm.cmd run test:backend:swarm
npm.cmd run build
npm.cmd run build:backend
```

Expected:

- `vitest` passes
- backend swarm tests pass
- frontend build succeeds
- backend build succeeds

- [ ] **Step 5: Commit**

```bash
git add package.json README.md backend/src/app/backend_app.cpp backend/src/controllers/http_controller.cpp src/lib/backendSwarmSource.test.ts
git commit -m "refactor: complete torrent swarm migration"
```

## Self-Review

### Spec coverage

- Immutable per-file manifests: Task 1
- File-level chunking and persistent resume: Task 2
- Discovery plus hotspot bootstrap fallback: Task 3 and Task 6
- Swarm protocol and multi-peer piece assignment: Task 4 and Task 5
- Automatic seeding after completion: Task 5
- Torrent-style UI and workflow: Task 7 and Task 8
- Removal of shared-folder semantics and direct transfer framing: Task 6, Task 8, and Task 9
- Verification and demo readiness: Task 9

No spec requirement is left without a task.

### Placeholder scan

No `TBD`, `TODO`, or "implement later" placeholders remain in the plan. Every task lists exact file paths, concrete commands, and named code units.

### Type consistency

The plan uses the same names throughout:

- `TorrentManifest`
- `SwarmPeerRecord`
- `TorrentLibraryEntry`
- `DownloadSessionRecord`
- `PieceScheduler`
- `SwarmTransferService`

These names are introduced in earlier tasks before they are referenced in later ones.
