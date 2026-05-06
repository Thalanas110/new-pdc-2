export function formatBytes(bytes: number): string {
  const safeBytes = Math.max(0, bytes);
  if (safeBytes < 1024) {
    return `${Math.round(safeBytes)} B`;
  }

  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = safeBytes / 1024;
  let unitIndex = 0;

  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }

  const rounded = Number.isInteger(value) ? value.toFixed(0) : value.toFixed(1);
  return `${rounded} ${units[unitIndex]}`;
}

export function getTransferPercent(transferredBytes: number, totalBytes: number): number {
  if (totalBytes <= 0) {
    return 0;
  }

  const percent = (transferredBytes / totalBytes) * 100;
  return Math.min(100, Math.max(0, Math.round(percent)));
}

export function isLocalhostAddress(value: string): boolean {
  const host = value.trim().toLowerCase();
  return host === 'localhost' || host === '127.0.0.1' || host === '::1';
}

function parseIpv4(value: string): number[] | null {
  const parts = value.trim().split('.');
  if (parts.length !== 4) {
    return null;
  }

  const octets = parts.map((part) => Number(part));
  if (octets.some((part, index) => !Number.isInteger(part) || part < 0 || part > 255 || String(part) !== parts[index])) {
    return null;
  }
  return octets;
}

export function isPrivateLanAddress(value: string): boolean {
  const octets = parseIpv4(value);
  if (!octets) {
    return false;
  }

  const [first, second] = octets;
  return (
    first === 10 ||
    (first === 172 && second >= 16 && second <= 31) ||
    (first === 192 && second === 168)
  );
}

export function isAllowedPeerAddress(value: string): boolean {
  return isLocalhostAddress(value) || isPrivateLanAddress(value);
}

export function buildPeerUrl(host: string, port: number): string {
  const normalizedHost = host.trim().toLowerCase() === 'localhost' ? 'localhost' : host.trim();
  return `http://${normalizedHost}:${port}`;
}

export type TransferStatus = 'queued' | 'transferring' | 'complete' | 'failed';

export type TransferRecord = {
  id: string;
  direction: 'incoming' | 'outgoing';
  fileName: string;
  status: TransferStatus;
  peer: string;
  size: number;
  bytesTransferred: number;
  message: string;
  startedAt: string;
  completedAt: string;
};

export type BackendStatus = {
  nodeId: string;
  host: string;
  httpPort: number;
  transferPort: number;
  receiveDir: string;
  listenerActive: boolean;
  allowRemotePeers?: boolean;
  bindHost?: string;
  advertisedHost?: string;
  transfers: TransferRecord[];
};
