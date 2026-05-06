# Loopline P2P

Localhost and private-LAN P2P file transfer with a React + TanStack Router frontend and a C++ transfer backend.

## Run

```powershell
npm install
npm run build:backend
npm run start:local
```

Open [http://127.0.0.1:5173](http://127.0.0.1:5173).

## Docker LAN P2P

Run this on both laptops while they are on the same Wi-Fi/LAN:

```powershell
docker compose up --build
```

Open [http://localhost:8080](http://localhost:8080) on each laptop.

To send from laptop A to laptop B:

1. Find laptop B's LAN IP, for example `192.168.1.42`.
2. On laptop A, set **Peer host** to laptop B's LAN IP.
3. Keep **Peer port** as `8788`.
4. Pick a file and press **Send file**.

Docker exposes:

- UI: `http://localhost:8080`
- Transfer socket: `8788/tcp`

If the laptops cannot connect, allow inbound TCP `8788` through the receiving laptop's firewall and make sure both laptops are on the same network. Received files are saved to `backend/received` on the host.

## Phone Hotspot Mode

This works over a phone hotspot when the phone allows connected devices to talk to each other. Many phones do; some carrier or hotspot modes enable client isolation, which blocks laptop-to-laptop traffic. No Docker or app setting can bypass client isolation because the phone drops the packet before it reaches the other laptop.

Use this checklist:

1. Connect both laptops to the same phone hotspot.
2. Run `docker compose up --build` on both laptops.
3. On the receiving laptop, find the hotspot Wi-Fi IPv4 address:

```powershell
ipconfig
```

Look for the Wi-Fi adapter address. Common hotspot ranges are `172.20.10.x` for iPhone, `192.168.43.x` for Android, `192.168.137.x` for Windows sharing, and sometimes `10.x.x.x`.

4. From the sending laptop, test the receiving laptop before sending:

```powershell
Test-NetConnection RECEIVER_HOTSPOT_IP -Port 8788
```

If `TcpTestSucceeded` is `True`, use `RECEIVER_HOTSPOT_IP` as **Peer host** and `8788` as **Peer port**.

If it is `False`, check Windows Defender Firewall on the receiving laptop and allow inbound TCP `8788`. If the firewall is open and it still fails, the phone hotspot is isolating clients; use another phone/hotspot mode, a normal router Wi-Fi, or a direct laptop hotspot.

## Ports

- Frontend: `127.0.0.1:5173`
- Backend HTTP API: `127.0.0.1:8787`
- C++ transfer socket: `127.0.0.1:8788`

Received files are saved to `backend/received`.

## Manual Two-Terminal Mode

```powershell
npm run dev:backend
```

```powershell
npm run dev -- --host 127.0.0.1 --port 5173
```
