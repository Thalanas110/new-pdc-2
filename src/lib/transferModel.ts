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
  const trimmedHost = host.trim();
  if (trimmedHost.includes(':') && !trimmedHost.startsWith('[') && !trimmedHost.endsWith(']')) {
    return `http://[${trimmedHost}]:${port}`;
  }

  const normalizedHost = trimmedHost.toLowerCase() === 'localhost' ? 'localhost' : trimmedHost;
  return `http://${normalizedHost}:${port}`;
}

export const fileKinds = ['received', 'sent', 'shared'] as const;
export type FileKind = (typeof fileKinds)[number];
export type FilePreviewKind = 'image' | 'video' | 'audio' | 'pdf' | 'text' | 'unsupported';

export type TransferFileEntry = {
  kind: FileKind;
  name: string;
  size: number;
  modifiedAt: string;
  contentType: string;
  url: string;
  downloadUrl: string;
};

export function getFilePreviewKind(fileName: string, contentType: string): FilePreviewKind {
  const type = contentType.toLowerCase();
  const extension = fileName.toLowerCase().split('.').pop() ?? '';

  if (type.startsWith('image/')) {
    return 'image';
  }
  if (type.startsWith('video/')) {
    return 'video';
  }
  if (type.startsWith('audio/')) {
    return 'audio';
  }
  if (type === 'application/pdf' || extension === 'pdf') {
    return 'pdf';
  }
  if (type.startsWith('text/') || ['txt', 'log', 'csv', 'json', 'md'].includes(extension)) {
    return 'text';
  }
  return 'unsupported';
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

export type SyncPeer = {
  host: string;
  port: number;
};

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
  receiveDir: string;
  sentDir?: string;
  sharedDir?: string;
  listenerActive: boolean;
  allowRemotePeers?: boolean;
  bindHost?: string;
  advertisedHost?: string;
  syncPeers?: SyncPeer[];
  transfers: TransferRecord[];
};
