# Loopline Torrent Swarm

Loopline is a private-network torrent-style file sharing app. The frontend is React + TanStack Start, and the backend is a C++ socket server that now models a small peer swarm instead of a shared drive.

## Mental Model

Each published file becomes its own immutable torrent-style entry.

- Publish a file from the `Publish` tab.
- Let peers auto-discover each other on the hotspot, or enter one bootstrap peer in `Swarm`.
- Browse available files in `Library`.
- Download from multiple peers in parallel in `Downloads`.
- Completed downloads automatically reseed.

## Quick Start

Run the full app:

```powershell
docker compose up --build
```

Open the UI:

```text
http://localhost:8080
```

## Swarm Workflow

1. Start Loopline on each laptop/device connected to the same private network or phone hotspot.
2. On the first device, publish a file from `Publish`.
3. On the other devices, use `Swarm` to auto-discover peers or enter one bootstrap peer manually.
4. The published file appears in `Library`.
5. Start the download from `Library` and watch progress in `Downloads`.
6. Once complete, the downloader becomes another seeder automatically.

## Hotspot Note

This project is designed for private IP ranges commonly seen on Android and iPhone hotspots. If automatic discovery is unreliable, enter one known reachable peer in `Swarm` and let the rest of the peer list spread from there.

## Dev Mode

Frontend:

```powershell
npm install
npm run dev
```

Backend:

```powershell
npm run build:backend
backend\p2p_server.exe
```

## Verification

Frontend tests:

```powershell
npm test
```

Backend swarm tests:

```powershell
npm run test:backend:swarm
```
