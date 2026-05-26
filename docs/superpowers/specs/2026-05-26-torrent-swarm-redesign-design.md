# Loopline Torrent Swarm Redesign

Date: 2026-05-26
Status: Drafted from approved design discussion

## Goal

Convert Loopline from a shared-drive style peer sync app into a torrent-style distributed system that behaves like a real swarm on a private network.

The redesigned system must:

- publish immutable files as torrent-like artifacts
- split each file into verified pieces
- allow multi-peer parallel piece download
- allow every completed downloader to become a seeder automatically
- operate without a permanent coordinator after discovery
- remain practical on an Android phone hotspot by supporting one-time bootstrap fallback when automatic peer discovery is unreliable

## Current Problem

The current implementation is still centered on a mutable `shared/` folder and peer-registered push sync. Even though it already uses resumable chunk transfer, the dominant model is still:

- watch a shared folder
- connect to known peers
- push the latest file version to them

That model resembles synchronized shared storage more than a torrent swarm. It does not make manifest exchange, piece availability, peer turnover, and reseeding the central system behavior.

## Target Product Definition

Loopline becomes a fully torrent-style app.

The existing `Transfer`, `Receive`, and `Shared` product framing is removed. The app becomes a publish/discover/download/seed workflow.

Direct one-to-one send/receive is out of scope for the redesigned product.

Mutable shared-folder sync is out of scope for the redesigned product.

Each published file is immutable. If a user changes a file, the system publishes a new torrent entry rather than overwriting an existing one.

## System Model

Every running node can perform four roles:

1. Publisher
   A user selects a file. The node chunks it, hashes each piece, writes a manifest, stores local piece availability, and starts seeding.
2. Discoverer
   The node discovers peers automatically on the hotspot or private network. If that fails, it can connect to one manually entered bootstrap peer and learn the rest of the swarm from gossip.
3. Leecher
   The node requests a manifest, learns which peers hold which pieces, and downloads different missing pieces from multiple peers in parallel.
4. Seeder
   The node advertises verified pieces it already has and uploads them to peers. After completing the whole file, it seeds the entire torrent automatically.

This is a swarm model, not a shared-folder replication model.

## Architecture Overview

The backend should be reorganized around these runtime services:

- `DiscoveryService`
  Responsible for automatic local peer discovery, bootstrap fallback, peer liveness, and peer list gossip.
- `CatalogService`
  Responsible for advertising known torrent manifests, syncing manifest metadata between peers, and maintaining the distributed library view.
- `ManifestService`
  Responsible for creating immutable manifests during publish and resolving manifest lookups during discovery/download.
- `PieceStoreService`
  Responsible for piece storage on disk, piece verification, bitfield state, and file reassembly.
- `SwarmTransferService`
  Responsible for bitfield exchange, piece scheduling, multi-peer piece requests, throttling, retries, and upload serving.
- `SessionService`
  Responsible for per-torrent state such as queued/downloading/seeding/complete, active peers, progress, and recovery after restart.
- `TorrentHttpController`
  Responsible for exposing publish, swarm, library, and download state to the frontend. This replaces the current shared-sync oriented API surface.

## Persistent Data Model

Each node persists the following state:

### Node Record

- `nodeId`
- advertised host
- transfer port
- last seen time
- peer source (`discovered` or `bootstrap`)
- reachability health

### Torrent Manifest

One manifest per published file.

- `torrentId` or info-hash
- original display name
- total file size
- piece size
- piece count
- ordered list of piece hashes
- publisher node ID
- created timestamp

### Local Piece State

Per torrent:

- local bitfield
- completed piece count
- partial piece files on disk
- complete file path when assembled

### Peer Availability State

Per torrent:

- known peers participating in the torrent
- last advertised bitfield per peer
- last availability update time

### Download Session State

Per torrent:

- status (`queued`, `discovering`, `downloading`, `verifying`, `seeding`, `complete`, `failed`)
- active peer set
- pending piece queue
- in-flight requests
- retry counters

## On-Disk Layout

The mutable `shared/` folder is no longer the source of truth.

Recommended storage layout:

- `backend/torrents/manifests/`
  Stores one manifest file per published torrent.
- `backend/torrents/pieces/<torrentId>/`
  Stores verified piece files for local download/resume/seeding.
- `backend/torrents/files/`
  Stores complete assembled files for published or completed torrents.
- `backend/torrents/state/`
  Stores local bitfields, peer cache, and session metadata.

The piece store must survive restart so interrupted downloads can resume without redownloading verified pieces.

## Chunking and Integrity

Chunking is performed on each published file.

A catalog of many files is supported only as a list of separate single-file torrents. There is no folder-level mutable sync object.

Recommended initial defaults:

- fixed piece size: `256 KB`
- piece hashes stored in the manifest
- full file considered complete only after all pieces verify and reassemble

Rules:

- every received piece is hash-verified before acceptance
- failed or corrupted pieces are discarded and re-requested
- completed pieces are immediately eligible for local advertisement
- changed files create new manifests instead of replacing old ones

## Peer Discovery and Bootstrap

### Primary Discovery Path

Each node periodically announces itself on the private network and listens for peer announcements.

Discovery messages must include:

- node ID
- listening address
- transfer port
- protocol version
- brief capability flags

After first contact, peers exchange known peer lists so discovery spreads by gossip.

### Hotspot Fallback

Android phone hotspots may make automatic discovery unreliable. For that reason, the app supports one-time bootstrap fallback:

- a user manually enters one reachable peer
- the node connects to that peer
- both sides exchange known peers and catalog summaries
- the node expands from that initial contact into the wider swarm

The bootstrap peer is not a server or permanent coordinator. It is only an entry point into the swarm.

### Liveness Rules

- private-network addresses only
- peers expire after repeated missed heartbeats or failed reconnects
- stale peer availability is pruned automatically
- transfers continue with remaining peers when one peer disappears

## Swarm Wire Protocol

The protocol should stop being based on whole-file push commands and instead expose swarm messages.

Required message families:

- `HELLO`
  Introduce node identity, address, port, and protocol version.
- `PEERS`
  Gossip additional known peers.
- `CATALOG`
  Advertise known torrent IDs and lightweight summaries.
- `MANIFEST_REQUEST`
  Ask for a specific manifest.
- `MANIFEST_RESPONSE`
  Return manifest metadata and piece-hash list.
- `BITFIELD`
  Advertise which pieces a node currently has for a torrent.
- `HAVE`
  Announce newly completed piece indexes incrementally.
- `REQUEST`
  Ask for one specific piece index from a peer.
- `PIECE`
  Return a piece payload with torrent ID and index.
- `REJECT` or `NOT_FOUND`
  Indicate that the peer cannot currently serve the requested piece.
- `PING` and `PONG`
  Maintain liveness.

Protocol invariants:

- manifest identity is immutable
- piece indexes are deterministic from the manifest
- nodes never treat a file name alone as identity
- piece payloads are accepted only after hash verification

## Publish Flow

1. User selects a file in the `Publish` view.
2. Backend computes piece boundaries and piece hashes.
3. Backend writes the manifest and local bitfield.
4. Backend stores the complete file in local torrent storage.
5. Backend advertises the new torrent in the local catalog.
6. Discovery and catalog gossip make the torrent visible to peers.
7. Local node starts seeding immediately.

## Download Flow

1. User opens the distributed library and chooses a torrent.
2. Node obtains the manifest if not already cached.
3. Node requests bitfields from available seeders/leechers.
4. Scheduler assigns different missing pieces across multiple peers in parallel.
5. Received pieces are verified and persisted.
6. Node emits `HAVE` as pieces complete.
7. Once all pieces are verified, backend reassembles the file and marks the torrent complete.
8. Node transitions into seeding automatically.

## Piece Scheduling Rules

The scheduler should prioritize real swarm behavior over simple sequential download.

Initial rules:

- request different pieces from different peers concurrently
- avoid assigning the same piece to many peers unless recovery is needed
- prefer peers that have responded reliably
- deprioritize peers that repeatedly reject or stall
- rebalance remaining requests when a peer disconnects

Optional future refinement:

- rarest-first piece selection once the basic system works

That refinement is not required for the first pass as long as the implementation already supports true multi-peer parallel piece download.

## UI Redesign

Replace the current three-tab model with these views:

- `Publish`
  Select a file, publish it, and begin seeding.
- `Swarm`
  Show discovery state, bootstrap status, known peers, reachable peers, and local listener state.
- `Library`
  Show distributed torrent catalog entries available from the swarm.
- `Downloads`
  Show per-file progress, piece counts, active peers, verification state, and seeding status.

Key UI concepts:

- each listed file represents one immutable torrent
- progress is based on verified piece count, not just bytes copied
- download state includes peer count and seeding state
- swarm state makes decentralization visible during demo

Remove from the UX:

- shared-folder auto-sync framing
- direct send/receive framing
- overwrite/delete propagation
- `Upload to shared folder` workflow

## Failure Handling

The system must behave correctly under partial failure.

Required behavior:

- partial downloads survive restart
- disconnects do not discard verified pieces
- missing pieces are re-requested from remaining peers
- corrupted pieces are rejected and retried
- stale peers disappear from active swarm state
- discovery failure falls back to manual bootstrap

If a peer advertises a piece but repeatedly fails to deliver it, the scheduler should temporarily lower that peer's priority for the current session.

## Testing Requirements

The redesigned app should be validated against the claims that distinguish a distributed torrent-style system from shared storage.

Required demonstrations:

- `Multi-peer download`
  One file downloads from several peers at the same time.
- `Automatic reseeding`
  A completed downloader immediately helps seed to another peer.
- `Resume`
  An interrupted download continues from saved verified pieces.
- `Integrity`
  A bad or incomplete piece is rejected and replaced.
- `Coordinator-free transfer`
  After discovery/bootstrap, file transfer continues without a central coordinator.
- `Hotspot practicality`
  The system still works on a phone hotspot when the first connection is made through one bootstrap peer.

## Migration Direction

The redesign should replace the current shared-sync architecture rather than layer new swarm terms over the existing shared-drive workflow.

Expected high-level migration:

- remove shared-folder watch and push-sync semantics from the main product path
- keep low-level socket infrastructure where reusable
- replace shared-sync protocol handlers with swarm protocol handlers
- replace shared-folder API endpoints with publish/library/swarm/download endpoints
- replace shared-centric frontend views with torrent-centric views

## Non-Goals

The first redesign does not need:

- internet-wide NAT traversal
- public trackers
- DHT
- encrypted peer transport
- mutable multi-file directory torrents
- advanced tit-for-tat choking logic

Those features are not necessary to qualify the project as a torrent-style distributed system for the current course goal.

## Acceptance Statement

This redesign should be considered successful when the app can be described honestly as:

"A private-network torrent-style file distribution system where peers discover each other, exchange immutable file manifests, download verified pieces from multiple peers in parallel, and automatically reseed completed files without relying on a permanent coordinator."
