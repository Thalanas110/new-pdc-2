# Loopline P2P

Loopline is a private-network P2P file transfer app. The frontend is React + TanStack Start/TanStack Router, and the transfer backend is a C++ socket server.

It works like this:

- Every device that should receive files runs Loopline.
- The receiver listens on TCP port `8788`.
- The sender enters the receiver's reachable private IP address and sends the file directly.
- There is no cloud server, account, or relay.

## Quick Start With Docker

Run this on every laptop/device that will receive files:

```powershell
docker compose up --build
```

Open the UI on that same device:

```text
http://localhost:8080
```

Docker exposes:

- UI: `8080/tcp`
- C++ transfer socket: `8788/tcp`

Received files are saved on the host in:

```text
backend\received
```

Outgoing file copies are saved on the host in:

```text
backend\sent
```

Shared files live on the host in:

```text
shared
```

## Viewing Files In The App

Use the **File vault** panel at the bottom of the Loopline UI.

- **Received** shows files this device received.
- **Sent** shows local copies of files this device sent.
- **Shared** shows files in the live shared folder.
- Click a file to preview it without leaving the page.

Inline preview works for common browser-renderable formats:

- Images: `png`, `jpg`, `gif`, `webp`, `svg`
- Text: `txt`, `log`, `md`, `csv`, `json`
- Media: `mp4`, `webm`, `mp3`, `wav`, `ogg`
- PDF: `pdf`

Other formats are still saved in `backend\received`, `backend\sent`, or `shared`, but the browser may not be able to render them inline.

## Shared Folder Sync

The `shared\` folder is a live sync folder.

When Loopline is running:

- Put a file into `shared\` on one device.
- Loopline waits until the file stops changing.
- It sends the file to every registered sync peer.
- The other device saves it into its own `shared\` folder.
- The file appears in the **Shared** tab without leaving or refreshing the page.

Updates resend automatically. Renames behave like a delete plus a new file. Deletes are sent to online sync peers.

To register a sync peer from the UI:

1. Enter the other device's IP in **Peer host**.
2. Keep **Peer port** as `8788`.
3. Click **Sync folder**.

Sending a normal file to a peer also remembers that peer for shared-folder sync while the backend is running.

You can also start Docker with peers already registered:

```powershell
$env:P2P_SYNC_PEERS="10.113.71.244:8788,192.168.42.101:8788"
docker compose up --build
```

For reliable three-device sync, register a full mesh:

```text
Laptop A sync peers: Laptop B IP, Device C IP
Laptop B sync peers: Laptop A IP, Device C IP
Device C sync peers: Laptop A IP, Laptop B IP
```

Loopline is still direct P2P. If one pair cannot pass `Test-NetConnection RECEIVER_IP -Port 8788`, that pair cannot sync over the current network.

## Two-Laptop Phone Hotspot Setup

Example:

- Laptop A sends.
- Laptop B receives.
- Both are connected to the same phone hotspot.

On laptop B:

```powershell
docker compose up --build
```

Find laptop B's hotspot IP:

```powershell
ipconfig
```

Use the IPv4 address under the real hotspot adapter. In this example, the correct IP is `10.113.71.244`:

```text
Wireless LAN adapter WiFi:
   IPv4 Address. . . . . . . . . . . : 10.113.71.244
   Default Gateway . . . . . . . . . : 10.113.71.177
```

Ignore virtual/internal adapters like:

```text
vEthernet (WSL)
Docker
Hyper-V
VirtualBox
VMware
Bluetooth Network Connection
```

On laptop A, test laptop B before sending:

```powershell
Test-NetConnection 10.113.71.244 -Port 8788
```

If `TcpTestSucceeded` is `True`, open laptop A's Loopline UI and use:

```text
Peer host: 10.113.71.244
Peer port: 8788
```

## Adding A Third Device

Loopline does not care which device is "A", "B", or "C". Any device can send to any other device if the sender can reach the receiver's TCP port `8788`.

For each receiving device:

1. Run Loopline:

```powershell
docker compose up --build
```

2. Find that device's reachable private IP:

```powershell
ipconfig
```

3. From the sending device, test:

```powershell
Test-NetConnection RECEIVER_IP -Port 8788
```

4. If the test succeeds, use `RECEIVER_IP` as **Peer host** and `8788` as **Peer port**.

You can send:

- Laptop A -> Laptop B
- Laptop B -> Laptop A
- Laptop A -> Device C
- Device C -> Laptop B
- Any other pair, as long as `Test-NetConnection RECEIVER_IP -Port 8788` succeeds.

For shared-folder sync, add each reachable device as a **Sync folder** peer in the UI, or set `P2P_SYNC_PEERS` before `docker compose up --build`.

## Third Device Over USB Tethering

Short answer: **USB tethering can work, but only if the phone routes traffic between the hotspot Wi-Fi clients and the USB-tethered device.**

The app already allows common private ranges used by phone hotspots and tethering:

- `10.x.x.x`
- `172.16.x.x` through `172.31.x.x`
- `192.168.x.x`

The uncertain part is the phone. Some phones bridge or route between Wi-Fi hotspot and USB tethering clients. Some isolate them. If the phone isolates them, Loopline cannot bypass that because the packets never reach the other device.

### If Device C Is USB-Tethered

On device C:

1. Connect the phone by USB.
2. Enable USB tethering on the phone.
3. Run Loopline:

```powershell
docker compose up --build
```

4. Find device C's USB tether IP:

```powershell
ipconfig
```

Look for an adapter that sounds like USB/RNDIS/mobile Ethernet, for example:

```text
Ethernet adapter Remote NDIS Compatible Device
Ethernet adapter USB Ethernet/RNDIS Gadget
Ethernet adapter Apple Mobile Device Ethernet
```

Common USB tethering ranges include:

- Android USB tethering: often `192.168.42.x`
- Android hotspot Wi-Fi: often `192.168.43.x`
- iPhone hotspot/tethering: often `172.20.10.x`
- Some phones/carriers: `10.x.x.x`

From laptop A or laptop B, test device C:

```powershell
Test-NetConnection DEVICE_C_USB_IP -Port 8788
```

If `TcpTestSucceeded` is `True`, device C can receive files. Use:

```text
Peer host: DEVICE_C_USB_IP
Peer port: 8788
```

If `TcpTestSucceeded` is `False`, try the reverse direction from device C:

```powershell
Test-NetConnection LAPTOP_B_HOTSPOT_IP -Port 8788
```

Sometimes the USB-tethered device can send to Wi-Fi hotspot devices even when Wi-Fi hotspot devices cannot send to the USB-tethered device.

If both directions fail, the phone is isolating USB tethering from Wi-Fi hotspot clients. Use one of these layouts instead:

- Put all devices on the same Wi-Fi hotspot.
- Use a normal Wi-Fi router.
- Use one laptop's mobile hotspot and connect the other devices to that.
- Use a VPN/mesh network such as Tailscale or ZeroTier, then use the VPN IP as **Peer host**.

## Choosing The Correct IP From `ipconfig`

Use the IP from the adapter that connects to the network shared by the devices.

Good examples:

```text
Wireless LAN adapter WiFi
IPv4 Address: 10.113.71.244
```

```text
Ethernet adapter Remote NDIS Compatible Device
IPv4 Address: 192.168.42.101
```

Do not use these unless you know exactly why:

```text
vEthernet (WSL)
vEthernet (Docker)
Hyper-V
VirtualBox
VMware
Bluetooth Network Connection
Loopback
```

Rule of thumb: the right adapter usually has both an IPv4 address and a default gateway.

## Firewall Setup

The receiving device must allow inbound TCP `8788`.

If Windows blocks the connection, run PowerShell as Administrator on the receiving device:

```powershell
New-NetFirewallRule -DisplayName "Loopline P2P 8788" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8788
```

Optional, if you want to open the web UI from another device:

```powershell
New-NetFirewallRule -DisplayName "Loopline UI 8080" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8080
```

Check from the sender:

```powershell
Test-NetConnection RECEIVER_IP -Port 8788
```

## Troubleshooting

### `exec /usr/local/bin/loopline-entrypoint: no such file or directory`

Rebuild with the fixed Dockerfile:

```powershell
docker compose down
docker compose build --no-cache
docker compose up
```

### `Test-NetConnection` fails

Check these in order:

1. Is Docker running on the receiver?

```powershell
docker compose ps
```

2. Is Loopline healthy on the receiver?

```powershell
curl http://localhost:8080/api/health
```

Expected:

```json
{"ok":true}
```

3. Is the receiver firewall allowing TCP `8788`?
4. Are both devices on the same reachable network?
5. If using a phone hotspot, is client isolation enabled by the phone/carrier?
6. If using USB tethering, is the phone routing between USB tethering and Wi-Fi hotspot clients?

### The UI opens but sending fails

The UI only proves port `8080` works locally. File transfer uses TCP `8788` between devices. Always test the receiver's `8788` from the sender:

```powershell
Test-NetConnection RECEIVER_IP -Port 8788
```

### Docker gets weird after many rebuilds

Reset the container:

```powershell
docker compose down
docker compose build --no-cache
docker compose up
```

## Dev Mode

Docker is recommended for multi-device testing. Dev mode is useful for local development.

Local-only backend:

```powershell
npm install
npm run dev:backend
```

Frontend in another terminal:

```powershell
npm run dev
```

Open:

```text
http://127.0.0.1:5173
```

To allow dev-mode LAN sending/receiving, start the backend with:

```powershell
$env:P2P_BIND_HOST="0.0.0.0"
$env:P2P_ALLOW_REMOTE="1"
npm run dev:backend
```

## Ports And Environment

Default ports:

- Frontend dev server: `5173`
- Docker web UI: `8080`
- Backend HTTP API inside Docker: `8787`
- C++ transfer socket: `8788`

Docker environment defaults:

```text
P2P_BIND_HOST=0.0.0.0
P2P_HTTP_PORT=8787
P2P_TRANSFER_PORT=8788
P2P_ALLOW_REMOTE=1
P2P_RECEIVE_DIR=/data/received
P2P_SENT_DIR=/data/sent
P2P_SHARED_DIR=/data/shared
P2P_SYNC_PEERS=
```

## Mental Model

Think of each device as a receiver with an address:

```text
Device B = 10.113.71.244:8788
Device C = 192.168.42.101:8788
```

To send a file, enter the receiver's address in the sender UI. If the sender can connect to that address, Loopline works. If it cannot, the problem is network routing, firewall, hotspot isolation, or the wrong IP address.
