# Codebase Q&A

## Q1: Where is the distributed system here and how does this work?

**A:** This is a **LAN peer-to-peer file transfer system** ("Loopline P2P"). There is no central server — every node is both client and server.

**A:** This is a **LAN peer-to-peer file transfer system** ("Loopline P2P"). There is no central server — every node is both client and server.

### Architecture Overview

Each node runs a C++ backend (`p2p_server.exe`) that opens **3 ports**:

| Port | Purpose |
|------|---------|
| 8787 (HTTP) | REST API & frontend |
| 8788 (Swarm) | P2P file transfer protocol |
| 8789 (UDP) | Peer discovery |

The React frontend (Vite + TanStack Router) talks to its local backend via HTTP. Backends talk to each other over the swarm protocol (TCP) and discovery (UDP).

### How It Works

**1. Peer Discovery** (`discovery_service.cpp`)

- Every 2 seconds, each node broadcasts a `DISCOVER/1` UDP message containing `NODE`, `HOST`, and `PORT` to `255.255.255.255:8789` and to the directed broadcast address (e.g. `192.168.1.255`)
- Every 12 seconds, it does a **unicast sweep** — probes every IP in the local subnet (1–254) individually
- A listener thread picks up discovery messages from other nodes and registers them as peers
- Bootstrap peers can also be provided via the `P2P_SYNC_PEERS` env var

**2. Swarm Protocol** (`swarm_transfer_service.cpp`, text-based TCP)

Once a peer is discovered, the swarm handshake proceeds:

| Command | Direction | Purpose |
|---------|-----------|---------|
| `HELLO` | Bi-directional | Exchange node ID, advertised host/port |
| `CATALOG_REQUEST` / `CATALOG_RESPONSE` | Request | Fetch list of available files (torrent manifests) |
| `PEERS_REQUEST` / `PEERS_RESPONSE` | Request | Exchange known peer lists (gossip protocol) |
| `MANIFEST_PUSH` | Push | Announce a newly published file to all known peers |
| `BITFIELD_REQUEST` / `BITFIELD_RESPONSE` | Request | Ask which pieces a peer has for a given torrent |
| `PIECE_REQUEST` / `PIECE_RESPONSE` | Request | Download a specific piece range |

**3. Torrent-style File Transfer** (`torrent_types.hpp`, `manifest_service.cpp`, `piece_store_service.cpp`)

- Files are split into **pieces** (default 256 KB each)
- Each piece has a **64-bit hash** stored in a `TorrentManifest`
- The manifest (torrent ID, name, size, piece hashes, publisher) serves as the file catalog entry
- Pieces are stored individually on disk under `backend/torrents/`
- When downloading, the `PieceScheduler` plans which pieces to request from which peer (up to 2 in-flight per peer)
- Multiple peers are queried for their bitfields, then pieces are fetched in parallel using `std::thread`
- Once all pieces are verified, the file is assembled and saved to `backend/received/`
- The node then becomes a **seeder** and announces the manifest to other peers

**4. Key Backend Components**

| Component | File | Role |
|-----------|------|------|
| `SwarmTransferService` | `swarm_transfer_service.cpp` | Central orchestrator: background sync loop, transfer listener, download execution |
| `DiscoveryService` | `discovery_service.cpp` | UDP broadcast/listen for LAN peer discovery |
| `CatalogService` | `catalog_service.cpp` | Torrent manifest registry, tracks seeders per torrent |
| `ManifestService` | `manifest_service.cpp` | Builds torrent manifests from files (splits into pieces, hashes) |
| `PieceStoreService` | `piece_store_service.cpp` | Stores/loads individual pieces, assembles complete files |
| `PieceScheduler` | `piece_scheduler.cpp` | Optimizes piece selection across available peers |
| `SwarmProtocol` | `swarm_protocol.cpp` | Encodes protocol messages |
| `AppState` | `app_state.hpp` | Thread-safe shared state (transfers, peers, catalog, sessions) |
| `HttpController` | `http_controller.cpp` | REST API for the frontend |
| `FileVaultService` | `file_vault_service.cpp` | Serves received/sent files for download via HTTP |

**5. Frontend** — React app with TanStack Router (`src/`)

- Provides a web UI to browse the library catalog, publish files, monitor downloads, view received/sent files
- Communicates with the local backend via HTTP on port 8787

### Distributed System Characteristics

- **Decentralized**: No central coordinator; all nodes are equal
- **Gossip-style discovery**: Peers share their peer lists, creating transitive connectivity
- **BitTorrent-like swarming**: Files split into pieces, parallel download from multiple seeders
- **Eventual consistency**: Catalog propagates as peers exchange manifests
- **Fault tolerance**: Cooldown/penalty system for unresponsive peers; download retries with backoff
- **LAN-only security**: `is_private_lan_host()` restricts peer connections to private IP ranges (10.x, 172.16-31.x, 192.168.x) — no public internet routing

---

## Q2: Demonstrate, using specific file paths and line numbers, the data flow end-to-end.

### Flow A: User publishes a file (Laptop A → Swarm)

**Step 1 — Frontend sends file to backend via HTTP POST**

| File | Lines | What happens |
|------|-------|-------------|
| `src/components/LooplineTransferApp.tsx` | 92–112 | `onPublish()` calls `publishFile()` from the UI |
| `src/lib/backendClient.ts` | 154–178 | `publishFile()` creates `XMLHttpRequest` POST to `/api/publish` with `X-File-Name` header and raw file body |

**Step 2 — Backend HTTP controller receives the request**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/controllers/http_controller.cpp` | 84–87 | Route `POST /api/publish` reads `x-file-name` header, extracts file name, calls `deps_.publish_file(client, file_name, request.body)` |
| `backend/src/app/backend_app.cpp` | 175–177 | The lambda wires `deps.publish_file` to `swarm_transfer_service.publish_from_http()` |

**Step 3 — SwarmTransferService processes the file**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 1003–1017 | `publish_from_http()` calls `publish_file()` |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 502–541 | `publish_file()` writes to temp file, calls `manifest_service_.build_manifest()`, then `store_local_pieces()`, then `catalog_.publish_local_manifest()`, then `announce_manifest_to_known_peers()` |

**Step 4 — ManifestService splits file into pieces and hashes them**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/manifest_service.cpp` | 46–98 | `build_manifest()` reads the file in 256 KB chunks (`piece_size`), hashes each chunk with FNV-1a (`hash_piece()`, line 15–22), stores hashes in `TorrentManifest.piece_hashes` |

**Step 5 — PieceStoreService stores each piece on disk**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/piece_store_service.cpp` | 116–174 | `store_piece()` validates hash, writes piece to `backend/torrents/pieces/torrent-<hash>/piece-<index>.bin` |

**Step 6 — CatalogService registers the file locally**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/catalog_service.cpp` | 25–33 | `publish_local_manifest()` inserts manifest into `manifests_` map, records seeder (self), syncs library entry into `AppState` |

**Step 7 — Announce to all known peers via TCP**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 583–594 | `announce_manifest_to_known_peers()` iterates `swarm_peers_snapshot()`, calls `announce_manifest_to_peer()` for each |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 557–581 | `announce_manifest_to_peer()` opens TCP connection, sends `SWARM/1\nMANIFEST_PUSH\nHOST ... PORT ... TORRENT ... NAME ...` (manifest fields), waits for `SWARM/1\nOK` |

---

### Flow B: Peer discovery (how Laptop A finds Laptop B)

**Step 1 — UDP broadcast announce loop**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/discovery_service.cpp` | 221–245 | `announce_loop()`: every 2 seconds, sends `DISCOVER/1\nNODE ...\nHOST ...\nPORT ...` UDP message to `255.255.255.255:8789` and subnet-directed broadcast. Every 12s also unicast-probes every `.1`–`.254` in the subnet (`unicast_probe_hosts()`, lines 99–124) |

**Step 2 — UDP listener receives announcements**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/discovery_service.cpp` | 247–280 | `listen_loop()`: `recvfrom()` on UDP 8789, parses the `DISCOVER/1` message via `parse_announcement()` (lines 49–84), checks `node_id != self`, registers peer via `note_peer_hello()` and fires callback `notify_peer_detected()` |

**Step 3 — Callback triggers swarm handshake**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/app/backend_app.cpp` | 253–255 | Callback wired: `swarm_transfer_service.schedule_peer_probe(peer)` |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 273–279 | `schedule_peer_probe()` detaches a thread calling `bootstrap_peer()` |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 462–484 | `bootstrap_peer()` calls: `send_hello_to_peer()` → `fetch_peers_from_peer()` → `fetch_catalog_from_peer()` |

**Step 4 — HELLO handshake**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_protocol.cpp` | 24–30 | `encode_hello()` builds `SWARM/1\nHELLO\nNODE ...\nHOST ...\nPORT ...\n` |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 324–343 | `send_hello_to_peer()` connects, sends HELLO, expects `SWARM/1\nOK` response |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 834–858 | `handle_swarm_client()` handles incoming HELLO, parses NODE/HOST/PORT, upserts peer into `AppState`, responds `SWARM/1\nOK` |

**Step 5 — Peer exchange (gossip)**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 388–460 | `fetch_peers_from_peer()` sends `PEERS_REQUEST`, receives `PEERS_RESPONSE` with list of known peers, upserts each into `AppState`, recursively schedules probes for new peers |

**Step 6 — Catalog exchange**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 345–386 | `fetch_catalog_from_peer()` sends `CATALOG_REQUEST`, receives `CATALOG_RESPONSE` with `COUNT N` followed by N manifest blocks, calls `catalog_.note_remote_manifest()` for each |
| `backend/src/services/swarm/catalog_service.cpp` | 35–44 | `note_remote_manifest()` inserts manifest and seeder into maps, syncs library entry |

---

### Flow C: User downloads a file (Laptop B downloads from Laptop A)

**Step 1 — Frontend triggers download**

| File | Lines | What happens |
|------|-------|-------------|
| `src/components/LooplineTransferApp.tsx` | 129–140 | `onStartDownload(torrentId)` calls `requestDownload(torrentId)` |
| `src/lib/backendClient.ts` | 109–117 | `startDownload()` sends `POST /api/downloads/start?torrentId=...` |

**Step 2 — Backend HTTP controller**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/controllers/http_controller.cpp` | 88–91 | Route `POST /api/downloads/start` extracts `torrentId`, calls `deps_.start_download(torrent_id)` |
| `backend/src/app/backend_app.cpp` | 194–196 | Lambda wired to `swarm_transfer_service.start_download_by_id()` |

**Step 3 — SwarmTransferService starts download thread**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 549–555 | `start_download_by_id()` looks up manifest from catalog, calls `start_download()` |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 543–547 | `start_download()` sets session to "discovering", sets local status to "downloading", detaches thread calling `run_download()` |

**Step 4 — Check existing pieces, build missing list**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 698–722 | `run_download()`: iterates `manifest.piece_count`, checks `piece_store_.has_piece()` for each, builds `missing` vector |

**Step 5 — Query seeders for their bitfield**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 725–745 | Gets `seeders_for()` from catalog, connects to each, calls `request_bitfield()` |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 596–631 | `request_bitfield()` sends `BITFIELD_REQUEST\nTORRENT ...`, receives `BITFIELD_RESPONSE` with `BITS ...` (string of 0s/1s) |

**Step 6 — Piece scheduler plans which pieces to fetch where**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 754 | `scheduler_.plan_requests(missing, availabilities, kMaxInFlightPerPeer)` where `kMaxInFlightPerPeer = 2` (line 27) |
| `backend/src/services/swarm/piece_scheduler.cpp` | 134–257 | `plan_requests()` builds a **min-cost max-flow** network (lines 180–233): source→pieces→slots (per-peer slots with ordinal costs + penalty weights)→sink. Runs SPFA (line 59–124). Returns map of `peer_key → [piece_indices]` |

**Step 7 — Parallel piece fetching from multiple seeders**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 767–804 | Creates `std::thread` per seeder (line 782), each thread iterates its assigned pieces, calls `request_piece()` |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 633–677 | `request_piece()` sends `PIECE_REQUEST\nTORRENT ...\nPIECE ...`, receives `PIECE_RESPONSE` with binary piece data |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 782–797 | Each received piece is stored via `piece_store_.store_piece()`, `have[]` updated, `verified` count incremented |

**Step 8 — Peer serves pieces (Laptop A's side)**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 960–997 | `handle_swarm_client()` handles `PIECE_REQUEST`: looks up manifest, loads piece via `piece_store_.load_piece()`, responds with `PIECE_RESPONSE\n...\nBYTES N\n\n` + binary data |

**Step 9 — After all pieces downloaded, verify and assemble**

| File | Lines | What happens |
|------|-------|-------------|
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 813–824 | After `missing` is empty: calls `sync_receive_file()` → `piece_store_.assemble_file()`, then `catalog_.mark_local_completion()`, `announce_manifest_to_known_peers()`, sets session to "seeding" |
| `backend/src/services/swarm/piece_store_service.cpp` | 241–305 | `assemble_file()`: verifies no missing pieces (line 242), reads all pieces in order (lines 260–277), writes to temp file, verifies size matches `manifest.file_size` (line 282), atomically renames to final path (line 297) |
| `backend/src/services/swarm/swarm_transfer_service.cpp` | 679–696 | `sync_receive_file()` copies assembled file to `backend/received/<filename>` |

---

### Summary

```
Laptop A (publisher)                     Laptop B (downloader)
      |                                        |
      |--[DISCOVER/1 UDP broadcast:8789]------>|  (discovery_service.cpp:221-245 / 247-280)
      |<--[HELLO TCP:8788]---------------------|  (swarm_transfer_service.cpp:324-343 / 834-858)
      |--[CATALOG_RESPONSE]--------------------|  (swarm_transfer_service.cpp:345-386)
      |                                        |
      |  User publishes file                    |
      |  POST /api/publish                     |
      |  (http_controller.cpp:84-87)            |
      |  publish_file()                         |
      |  (swarm_transfer_service.cpp:502-541)   |
      |  ManifestService splits & hashes        |
      |  (manifest_service.cpp:46-98)           |
      |  PieceStore stores pieces               |
      |  (piece_store_service.cpp:116-174)      |
      |                                        |
      |--[MANIFEST_PUSH TCP:8788]-------------->|  (swarm_transfer_service.cpp:557-581)
      |                                        |
      |  User starts download                   |
      |  POST /api/downloads/start              |
      |  (http_controller.cpp:88-91)            |
      |  run_download() thread                  |
      |  (swarm_transfer_service.cpp:698-824)   |
      |                                        |
      |<--[BITFIELD_REQUEST]--------------------|  (swarm_transfer_service.cpp:596-631)
      |--[BITFIELD_RESPONSE]------------------->|
      |                                        |
      |  PieceScheduler.plan_requests()         |
      |  (piece_scheduler.cpp:134-257)          |
      |  Min-cost max-flow optimization         |
      |                                        |
      |<--[PIECE_REQUEST]-----------------------|  (swarm_transfer_service.cpp:633-677)
      |--[PIECE_RESPONSE + binary data]-------->|  (swarm_transfer_service.cpp:960-997)
      |  (repeated for all pieces in parallel)  |
      |                                        |
      |  piece_store.assemble_file()            |
      |  (piece_store_service.cpp:241-305)      |
      |  File saved to backend/received/        |
      |  (swarm_transfer_service.cpp:679-696)   |
      |  Session becomes "seeding"              |
      |  (swarm_transfer_service.cpp:820-823)   |
      |--[MANIFEST_PUSH (now seeder)]---------->|  (swarm_transfer_service.cpp:822)

---

## Q3: Prove this is NOT a Google Drive / centralized cloud system

### Evidence 1: No central server — every node runs the same binary

`Dockerfile:10-32` and `backend_app.cpp:217-273` show that each instance is self-contained. There is **one Docker image** that builds the **exact same `p2p_server` binary** for every container. There is no "server" vs "client" build — every node is identical.

The `docker-compose.yml:1-23` runs a **single service** (`loopline`). If this were Google Drive, there would be separate services: a database, an API server, an auth server, a file storage server, etc. There is only one.

### Evidence 2: No cloud API calls — frontend only talks to localhost

`vite.config.ts:18-22`:
```ts
proxy: {
  '/api': {
    target: 'http://127.0.0.1:8787',  // <-- localhost only!
  },
}
```

Every fetch in `backendClient.ts` uses relative paths (`/api/status`, `/api/library`, `/api/publish`). These hit `http://127.0.0.1:8787` — the **user's own local C++ backend**. Not a remote cloud API. Zero `fetch()` calls go to an external domain.

### Evidence 3: No database, no cloud storage

A `grep` for `mongodb|postgres|mysql|redis|supabase|firebase|aws|s3|cloud` across all source files (`.cpp`, `.hpp`, `.ts`, `.tsx`) returned **zero relevant results** — only npm package integrity hashes in `package-lock.json`.

`AppState` (`app_state.hpp:81-133`) uses **in-memory `std::vector` and `std::map` with a `std::mutex`** — no database, no disk-based persistence beyond flat files. The catalog (`catalog_service.hpp:33-35`) is `std::map<std::string, TorrentManifest>` — entirely in memory.

### Evidence 4: Peer discovery is LAN broadcast, not a central registry

`discovery_service.cpp:221-245` sends UDP to `255.255.255.255:8789` — a **local subnet broadcast**. There is no central "peer discovery server." It also probes every IP from `.1` to `.254` in the local subnet. If this were Google Drive, peers would authenticate against a central user database.

### Evidence 5: Peers connect directly to each other over TCP

`swarm_transfer_service.cpp:345-386` — `fetch_catalog_from_peer()` directly opens a TCP socket to `peer.host:peer.port`. Files are transferred **peer-to-peer** (`:633-677`). There is no intermediary storage. Laptop A sends bytes directly to Laptop B.

### Evidence 6: Files stay on the local machine, not "uploaded to the cloud"

`piece_store_service.cpp:116-174` stores pieces at `backend/torrents/pieces/`. The assembled file lands at `backend/received/`. All paths are **local filesystem**. Nothing goes to S3, GCS, or any remote storage.

### Evidence 7: LAN-only security model

`transfer_core.cpp:108-143` (`is_private_lan_host()`) restricts peers to private IP ranges: `10.x.x.x`, `172.16-31.x.x`, `192.168.x.x`. The frontend enforces this too (`transferModel.ts:47-63`). This is intentionally firewalled to LAN — the exact opposite of a cloud service like Google Drive which is accessible from anywhere.

### Evidence 8: Fragile topology = proof of decentralization

As discussed in Q1's answer: if Laptops A and C are disconnected, the rest of the swarm keeps working. In Google Drive, if your internet goes down, you lose access entirely. Here, Laptops B, D, and E continue sharing files because **there is no central authority to lose**. The system degrades gracefully rather than failing entirely.

---

## Q4: Why can't it detect other people automatically when using a phone hotspot?

**Short answer:** Phone hotspots enable **Wi-Fi client isolation** (`ap_isolate`), which drops all direct device-to-device traffic at the Wi-Fi chip level — including the UDP broadcasts and unicast sweeps the discovery system relies on.

### How discovery is *supposed* to work (on a normal router)

The discovery system uses **three parallel mechanisms** in `discovery_service.cpp`:

| Mechanism | Code | Lines |
|-----------|------|-------|
| UDP broadcast | `send_udp_message(socket_handle, "255.255.255.255", ...)` | `discovery_service.cpp:230` |
| Directed broadcast | `send_udp_message(socket_handle, *directed, ...)` | `discovery_service.cpp:231-232` |
| Unicast sweep | `unicast_probe_hosts()` loop every 12s | `discovery_service.cpp:236-238`, computes hosts at lines 99-124 |

On a normal Wi-Fi router, the bridge forwards all three to every connected device.

### What phone hotspots do

**1. Client isolation (the main reason)**

Phone hotspots configure the Wi-Fi access point to **drop all frames between wireless clients**. Even if Laptop A gets `192.168.43.10` and Laptop B gets `192.168.43.20`:
- Laptop A's UDP broadcast to `255.255.255.255:8789` → AP receives it but **never forwards it** to Laptop B.
- Laptop A's unicast sweep to `192.168.43.20:8789` → AP drops it before it reaches Laptop B.

Phone manufacturers do this intentionally to prevent malware from spreading between hotspot users and to reduce support calls.

**2. NAT without a downstream bridge**

Even with isolation off, phone hotspots use a minimal NAT setup that often lacks an Ethernet bridge between downstream interfaces. Traffic between hotspot clients may need to pass through the phone's routing stack, and many phone kernels are configured against this.

### The code proves the authors knew about this

The UI literally tells you about it at `LooplineTransferApp.tsx:252-254`:
```
"If your phone hotspot hides peers, use the Swarm tab and type one seeder's host plus port {status.transferPort}."
```

The **Swarm tab** (`LooplineTransferApp.tsx:114-127`) lets you manually type a peer's IP and port for `bootstrapPeer()`. This is the intended workaround — not a bug to fix.

`backend_app.cpp:256` — `add_bootstrap_peers_from_list()` with `P2P_SYNC_PEERS` env var is another pre-configured fallback.

### Summary

The system is designed for **LANs with proper bridges** (Ethernet switches, normal Wi-Fi routers). Phone hotspots are intentionally hostile to client-to-client traffic. The manual bootstrap on the Swarm tab is the built-in fix.

---

## Q5: What happens when you connect 1000 devices?

**Short answer:** The system will suffer from **three cascading failures** — the background sync loop becomes a bottleneck, thread count explodes, and the network is flooded with redundant traffic. It will not crash instantly, but it will become unusably slow.

### Failure #1 — The sequential background sync loop (O(n²) scaling)

`swarm_transfer_service.cpp:281-298` — `background_sync_loop()`:

```cpp
void background_sync_loop() {
  while (state_.running.load()) {
    const auto peers = state_.swarm_peers_snapshot();
    for (const auto& peer_record : peers) {        // <-- iterates ALL 999 peers
      bootstrap_peer(peer);                          // <-- SEQUENTIAL, blocks for each
    }
    std::this_thread::sleep_for(kCatalogSyncInterval);  // 5 seconds
  }
}
```

For each peer, `bootstrap_peer()` (`:462-484`) opens **3 sequential TCP connections** — HELLO, PEERS_REQUEST, CATALOG_REQUEST:

```cpp
bool bootstrap_peer(const transfer::PeerEndpoint& peer) {
  any_ok = send_hello_to_peer(peer) || any_ok;        // TCP connect + round-trip
  any_ok = fetch_peers_from_peer(peer) || any_ok;      // TCP connect + round-trip
  any_ok = fetch_catalog_from_peer(peer) || any_ok;    // TCP connect + round-trip
}
```

With 999 peers at ~10ms LAN latency each → **~30 seconds per cycle**. The `sleep_for(5s)` never actually sleeps — the loop is always behind. By the time it finishes, it restarts immediately, **permanently at 100% CPU** on that thread, never catching up.

Each node runs this loop, meaning **all 1000 nodes are doing this simultaneously** — 3,000 TCP connections per node × 1,000 nodes = **3 million TCP connections** every 30 seconds on the network.

### Failure #2 — Thread explosion (no thread pool)

**Every** incoming TCP connection gets its own `std::thread`:

| Creates threads at | Line | Pattern |
|---|---|---|
| HTTP server — each client | `backend_app.cpp:210` | `std::thread(...).detach()` |
| Swarm listener — each client | `swarm_transfer_service.cpp:317` | `std::thread(...).detach()` |
| Download — each seeder | `swarm_transfer_service.cpp:782` | `workers.emplace_back(...)` |
| Peer probe — each new peer | `swarm_transfer_service.cpp:278` | `std::thread(...).detach()` |

With 1000 devices chattering constantly:
- Dozens of inbound swarm connections per second → each spawns a thread
- Downloading a file with 500 seeders → 500 worker threads
- No thread pool, no `io_uring`, no epoll — every connection is **blocking I/O on its own OS thread**

Windows has a ~2000 thread default limit per process. Linux has no hard limit, but context switching overhead at 1000+ threads is severe.

### Failure #3 — Peer list explosion (999 entries in every response)

When any peer handles a `PEERS_REQUEST` (`swarm_transfer_service.cpp:876-897`):

```cpp
response << "SWARM/1\nPEERS_RESPONSE\nCOUNT " << (peers.size() + 1) << '\n';
response << "NODE " << state_.node_id << '\n';
response << "HOST " << advertised_host << '\n';
response << "PORT " << transfer_port << '\n';
response << "END\n";
for (const auto& peer : peers) {           // 999 entries
  response << "NODE ..." << '\n';
  response << "HOST ..." << '\n';
  response << "PORT ..." << '\n';
  response << "END\n";
}
```

Each node sends its full 999-peer list to every peer it bootstraps to. With `background_sync_loop()` constantly re-bootstrapping, this is **O(n²) network traffic** — every node sends its entire peer list to every other node, every cycle.

### Failure #4 — Discovery broadcast storm

`discovery_service.cpp:221-245` — every 2 seconds, each node sends a UDP broadcast. With 1000 nodes:
```
1000 packets/sec sent per node (1 broadcast every 2s ÷ ... actually every 2s)
But: 1000 senders × ~1 broadcast/2s = 500 broadcast packets/sec arriving at each node's `recvfrom()`
```

Plus every 12 seconds: the `unicast_probe_hosts()` sweep (`:236-238`) sends 253 unicast UDP packets to `.1-.254`. Each of 1000 nodes doing this = 253,000 UDP packets every 12 seconds = **21,000 UDP packets/sec** on the wire from discovery alone.

### Failure #5 — Unbounded in-memory state

`AppState` (`app_state.hpp:126-132`) stores everything in memory:
- `std::vector<SwarmPeerRecord>` — 1000 entries
- `std::vector<TorrentLibraryEntry>` — if each of 1000 nodes publishes 10 files, 10,000 entries per node
- `std::vector<TransferRecord>` — every transfer ever logged

`CatalogService` (`catalog_service.hpp:33-35`):
```cpp
std::map<std::string, TorrentManifest> manifests_;      // 10,000+ manifests
std::map<std::string, std::map<std::string, ...>> seeders_;  // who has what
```

Every CATALOG_RESPONSE sends the **full list of manifests** to the requesting peer. With 10,000 manifests, each response is megabytes.

### What actually breaks first

| Component | Fails at ~ | Why |
|-----------|-----------|-----|
| Background sync loop | ~50-100 peers | Sequential TCP round-trips take too long; never finishes a cycle |
| Thread count | ~200-500 peers | Per-connection thread creation overwhelms the OS scheduler |
| UDP discovery noise | ~200-500 peers | `recvfrom` loop spends all time reading & discarding broadcast packets |
| Catalog response size | ~500-1000 peers | Sending 10K manifests over TCP overwhelms bandwidth on every bootstrap |
| Peer list gossip | ~500-1000 peers | O(n²) peer list exchanges flood the network; every node re-learns the same peers |

The **background sync loop** (`swarm_transfer_service.cpp:281-298`) is the worst bottleneck — it's O(n) sequential TCP connections in a single thread with no batching, no rate limiting beyond a cooldown, and no backpressure. This would be the first thing to make the system unusable.

---

## Q5: What happens when a file download fails mid-download?

### Three levels of failure handling

The download runs in `run_download()` at `swarm_transfer_service.cpp:698-824`. There are three distinct failure modes, each handled differently.

---

### Level 1 — A single piece request fails (transient error)

```cpp
// swarm_transfer_service.cpp:782-798
workers.emplace_back([&, peer_name, pieces, endpoint = endpoint_found->second]() {
  for (const auto piece_index : pieces) {
    const auto bytes = request_piece(endpoint, manifest, piece_index);
    if (!bytes || !piece_store_.store_piece(manifest, piece_index, *bytes)) {
      std::lock_guard<std::mutex> lock(state_mutex);
      penalties[peer_name] += 1;   // ← peer gets a strike
      continue;                     // ← moves on to next piece
    }
    ...
    made_progress = true;
  }
});
```

If `request_piece()` fails (peer disconnected, TCP timeout, corrupt response):
- The piece is **not stored** — `store_piece()` is never called, or the hash verification at `piece_store_service.cpp:127` rejects corrupt data
- **That peer gets +1 penalty** (`penalties[peer_name] += 1`). The `PieceScheduler` (`piece_scheduler.cpp:227-230`) uses penalties as a weight in the min-cost max-flow — penalized peers get fewer pieces next round
- The **thread continues** to the next piece; it doesn't crash or abort
- If other threads made progress, `made_progress` stays `true` and the outer `while` loop re-queries all seeders and re-plans

The key: **one bad peer doesn't kill the download**. It just shifts work to other peers.

---

### Level 2 — All peers fail (no progress this round)

```cpp
// swarm_transfer_service.cpp:806-811
missing = build_missing();
if (!made_progress.load() && !missing.empty()) {
  update_download_session(manifest, "failed", verified, active_peers);
  catalog_.set_local_status(manifest.torrent_id, "failed");
  return;   // ← exits run_download()
}
```

If every worker thread failed every piece request, and `made_progress` is still `false`:
1. The download session is marked **"failed"** — visible in the UI's Downloads view (`TorrentDownloadEntry.status`)
2. The catalog entry gets `local_status = "failed"` — visible in the library
3. **`run_download()` returns** — the thread dies

But: **already-downloaded pieces persist on disk** at `backend/torrents/pieces/torrent-<hash>/piece-<index>.bin` (`piece_store_service.cpp:101-108`). They survive process restarts.

---

### Level 3 — Assembly fails (all pieces downloaded, but file can't be built)

```cpp
// swarm_transfer_service.cpp:814-818
if (!sync_receive_file(manifest)) {
  update_download_session(manifest, "failed", verified, active_peers);
  catalog_.set_local_status(manifest.torrent_id, "failed");
  return;
}
```

Even if all pieces are verified, `sync_receive_file()` → `piece_store_.assemble_file()` (`piece_store_service.cpp:241-305`) might fail at:
- **Disk full** — `std::ofstream` fails to write (`:255`)
- **File size mismatch** — assembled file doesn't match `manifest.file_size` (`:282`)
- **Missing piece discovered** — `missing_pieces()` at line 242 finds gaps (shouldn't happen if we got here, but checks anyway)

All pieces are still on disk. The download is marked failed, but **no data is lost**.

---

### What survives for resume

```
Pieces on disk:  ✓  back end /torrents/pieces/torrent-<hash>/
                   piece-000000.bin  ✓
                   piece-000001.bin  ✓
                   piece-000002.bin  ✗  (never downloaded)
                   piece-000003.bin  ✓
                   ...
```

When the user clicks "start download" again (`start_download_by_id()`, `:549-555`), `run_download()` begins with:

```cpp
// swarm_transfer_service.cpp:699-706
for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
  if (piece_store_.has_piece(manifest, index)) {  // ← checks disk + hash
    have[index] = true;
    ++verified;
  }
}
```

`has_piece()` (`piece_store_service.cpp:176-200`) verifies:
1. File exists on disk (`:183`)
2. Size matches expected piece size (`:189-191`)
3. **FNV-1a hash matches** the manifest (`:199`) — if the file was corrupted on disk, it's treated as missing

Result: **the download resumes from where it failed**, skipping already-verified pieces.

### What doesn't happen (limitations)

| Missing feature | Evidence | Consequence |
|---|---|---|
| **No automatic retry** | No timer, no retry loop — once status is "failed", `run_download()` returns | User must manually click download again |
| **No exponential backoff** | `penalties` just increments; there's no cooldown for penalized peers | A dead peer keeps getting retried every `while` iteration |
| **No timeout on socket ops** | `net_io.cpp:100-127` — `connect()` uses OS default timeout (~20s on Windows) | A single blocking `connect()` can stall a worker thread for 20 seconds |
| **No download queue** | `start_download()` just detaches a thread (`:546`) | If the user starts 100 downloads, 100 threads run simultaneously, all competing for the same peers' bandwidth |
| **No graceful shutdown** | `detach()` — if the process exits, threads are killed mid-I/O | Piece files written to `.tmp` (lines 138-155) are cleaned up on next startup? Actually no — stale `.tmp` files may persist |

### Summary

```
Download starts
     │
     ├── Piece 0 → OK (stored to disk)
     ├── Piece 1 → FAIL (peer timeout) → penalty++, retry next round
     ├── Piece 2 → OK (stored to disk)
     ├── Piece 3 → FAIL (hash mismatch) → penalty++, retry next round
     ├── Piece 4 → FAIL → penalty++
     │
     ├── [new round] → Piece 1 → OK (different peer)
     ├── [new round] → Piece 3 → OK
     ├── [new round] → Piece 4 → FAIL again
     │
     └── All remaining peers exhausted, no progress this round
          → status = "failed", thread exits
          → Pieces 0, 1, 2, 3 remain on disk ✓
          → User clicks download again
          → Pieces 0,1,2,3 are skipped (has_piece = true)
          → Download resumes from piece 4
```

No data is lost. The partial download survives process crashes, restarts, and peer failures. The only downside is manual retry.

---

## Q6: Why not use an O(n log n) algorithm for all of these?

**Short answer:** Because not everything *can* be O(n log n) — some things are inherently O(n) or worse — and for the target LAN scale (~5-50 devices), the constant factors and code simplicity matter more than asymptotic complexity.

Let's go through each algorithm in the codebase and see what complexity it actually has, and whether O(n log n) is even possible.

---

### What's already O(n log n) or better

| Code | Algorithm | Actual complexity | Could it be better? |
|------|-----------|-------------------|---------------------|
| `piece_scheduler.cpp:150-159` | `std::stable_sort` to order missing pieces by availability (rarest first) | **O(n log n)** | No — comparison sort is optimally O(n log n) |
| `catalog_service.hpp:33` | `std::map<std::string, TorrentManifest>` for manifest lookups | **O(log n)** | No — balanced tree is optimal for ordered lookups |
| `app_state.hpp:132` | `std::map<std::string, SharedFileSignature>` | **O(log n)** | No |
| `manifest_service.cpp:15-22` | FNV-1a hashing each piece | **O(n)** | **Impossible** to do better — you *must* read every byte of the file |

### What's intentionally simpler than O(n log n)

**1. Peer storage: `std::vector` with linear scans**

`app_state.hpp:127-131`:
```cpp
std::vector<TransferRecord> transfers_;
std::vector<transfer::PeerEndpoint> sync_peers_;
std::vector<SwarmPeerRecord> swarm_peers_;
```

Lookups are O(n) — linear scan. For a LAN with 5-50 peers, this is instant. Using a `std::unordered_map` would add O(1) lookups but:
- No deterministic iteration order (debugging harder)
- Higher memory overhead per entry
- More complex serialization code
- The target class size is ~40 students, not 40,000

**2. Discovery unicast sweep: O(254) = O(1)**

`discovery_service.cpp:99-124` — always probes 253 IPs (`.1` to `.254` minus self):

```cpp
for (int octet = 1; octet <= 254; ++octet) {
    targets.push_back(prefix + std::to_string(octet));
}
```

This is **O(1)** with respect to the number of devices — it's bounded by the subnet size (/24). You can't make `/24` probe faster than O(254), and O(254) IS "O(1)" in algorithm analysis because it doesn't grow with input size. An O(n log n) version of this would mean... using a binary search to find peers on the network? That doesn't make sense — you can't binary-search an IP range without a DNS-style registry.

**3. Background sync loop: O(n) sequential blocking I/O**

`swarm_transfer_service.cpp:281-298` — iterates all peers sequentially:

```cpp
for (const auto& peer_record : peers) {
    bootstrap_peer(peer);  // 3 TCP connections, blocking
}
```

**The bottleneck here is not CPU complexity** — it's that each iteration **blocks on I/O** (TCP connect, send, recv). Switching from O(n) to O(log n) wouldn't help because the thread is blocked on the network, not the CPU. The fix would be **async I/O** (epoll, io_uring), not a different sort algorithm. An O(n log n) algorithm doesn't solve "I'm waiting 30ms for a TCP handshake 999 times in a row."

---

### The one place that's genuinely over-engineered: the min-cost max-flow

`piece_scheduler.cpp:180-233` — builds a min-cost max-flow network to optimally assign pieces to peers:

```
Source → [each missing piece] → [each peer slot] → Sink
```

The SPFA-based (Shortest Path Faster Algorithm) min-cost max-flow runs successive shortest augmenting paths:

```cpp
// piece_scheduler.cpp:59-124
while (true) {
    // SPFA shortest path (O(VE) worst case)
    // Augment 1 unit of flow
    // Repeat P times (P = missing pieces)
}
```

**Complexity:** O(P × V × E) worst case, where:
- P = number of missing pieces (could be 1000+ for a 256 MB file)
- V = 1 + P + (peers × max_in_flight) + 1
- E = P + (P × candidate_slots) + slots

This is **not O(n log n)** — it's closer to **O(P² × peers)**. A classroom demo with a 1 MB file → ~4 pieces → trivial. A 256 MB file with 50 seeders → 1000+ pieces → the flow network has thousands of nodes and edges.

**Why not use an O(n log n) greedy algorithm instead?**

A simple **"rarest piece first, random peer"** strategy would be O(n log n) and would work nearly as well in practice:

```
1. Sort missing pieces by availability count (rarest first)  → O(n log n)
2. For each piece, assign it to a random peer that has it   → O(n)
```

But min-cost max-flow gives you:
- **Optimal** load balancing (avoids overloading any single peer)
- **Penalty-aware** assignment (dead peers sink to the bottom)
- **Ordinal-aware** spreading (spreads sequential pieces across different peers for parallelism)

The authors chose correctness over raw speed. For a LAN classroom demo where the file sizes are small, this is fine. For 1000 devices with large files, it would be the first CPU bottleneck.

---

### The real bottlenecks aren't about asymptotic complexity

Looking at the code, here's what actually limits performance:

| Real bottleneck | Location | Problem | O(n log n) wouldn't fix |
|---|---|---|---|
| Sequential blocking I/O | `swarm_transfer_service.cpp:281-298` | Waits for TCP one peer at a time | No — needs async/epoll |
| Thread-per-connection | `:317`, `backend_app.cpp:210` | OS thread churn | No — needs thread pool |
| Full catalog resend | `:864-871` | Sends ALL manifests every sync | Could deduplicate, but that's a protocol change |
| No download queue | `:543-547` | Unlimited parallel downloads | No — needs admission control |
| Unbounded memory | `catalog_service.hpp:33-35` | Manifests accumulate forever | No — needs eviction policy |

### The real answer

The code is written for a **specific classroom use case**: ~40 students on a LAN, sharing files under 100 MB. At that scale:
- O(n) vector scans of 40 peers are instant
- Min-cost max-flow over 400 pieces and 40 peers finishes in microseconds
- Thread-per-connection creates 40 threads, which is fine
- Memory is not an issue

An O(n log n) algorithm everywhere would mean:
- Using `std::unordered_map` instead of `std::vector` → more memory, less debuggable
- Replacing min-cost max-flow with greedy → suboptimal peer utilization
- Adding binary search trees where linear scan is faster at n=40

**Asymptotic complexity matters when n grows unbounded.** This system is designed for a bounded LAN size. The authors optimized for simplicity, readability, and correctness — not for scaling to 1000 devices. That's a deliberate engineering trade-off, not an oversight.
