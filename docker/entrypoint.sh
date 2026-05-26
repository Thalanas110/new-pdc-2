#!/bin/sh
set -eu

mkdir -p "${P2P_RECEIVE_DIR:-/data/received}"
mkdir -p "${P2P_SENT_DIR:-/data/sent}"
mkdir -p "${P2P_SHARED_DIR:-/data/shared}"
mkdir -p /data/torrents /app/backend
ln -sfn /data/torrents /app/backend/torrents

p2p_server \
  --http "${P2P_HTTP_PORT:-8787}" \
  --transfer "${P2P_TRANSFER_PORT:-8788}" \
  --bind "${P2P_BIND_HOST:-0.0.0.0}" \
  --advertise "${P2P_ADVERTISED_HOST:-0.0.0.0}" &
backend_pid="$!"

nginx -g "daemon off;" &
nginx_pid="$!"

trap 'kill "$backend_pid" "$nginx_pid" 2>/dev/null || true' INT TERM

while kill -0 "$backend_pid" 2>/dev/null && kill -0 "$nginx_pid" 2>/dev/null; do
  sleep 1
done

kill "$backend_pid" "$nginx_pid" 2>/dev/null || true
wait "$backend_pid" 2>/dev/null || true
wait "$nginx_pid" 2>/dev/null || true
