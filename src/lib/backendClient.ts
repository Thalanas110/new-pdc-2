import type { BackendStatus, FileKind, SyncPeer, TorrentLibraryEntry, TransferFileEntry } from './transferModel';
import { getTransferPercent } from './transferModel';

export async function fetchStatus(signal?: AbortSignal): Promise<BackendStatus> {
  const response = await fetch('/api/status', { signal });
  if (!response.ok) {
    throw new Error(`Status request failed: ${response.status}`);
  }
  const payload = (await response.json()) as Partial<BackendStatus>;
  return {
    nodeId: payload.nodeId ?? '',
    host: payload.host ?? '',
    httpPort: payload.httpPort ?? 0,
    transferPort: payload.transferPort ?? 0,
    peers: payload.peers ?? [],
    library: payload.library ?? [],
    downloads: payload.downloads ?? [],
    receiveDir: payload.receiveDir ?? '',
    sentDir: payload.sentDir,
    sharedDir: payload.sharedDir,
    listenerActive: payload.listenerActive ?? false,
    allowRemotePeers: payload.allowRemotePeers,
    bindHost: payload.bindHost,
    advertisedHost: payload.advertisedHost,
    syncPeers: payload.syncPeers,
    transfers: payload.transfers ?? [],
  };
}

export async function startReceiver(port: number): Promise<void> {
  const response = await fetch(`/api/receive/start?port=${port}`, {
    method: 'POST',
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(body || `Receiver request failed: ${response.status}`);
  }
}

export async function fetchLibrary(): Promise<TorrentLibraryEntry[]> {
  const response = await fetch('/api/library');
  if (!response.ok) {
    throw new Error(`Library request failed: ${response.status}`);
  }

  const payload = (await response.json()) as TorrentLibraryEntry[] | { library?: TorrentLibraryEntry[]; torrents?: TorrentLibraryEntry[] };
  if (Array.isArray(payload)) {
    return payload;
  }
  return payload.library ?? payload.torrents ?? [];
}

export async function fetchFiles(kind: FileKind, signal?: AbortSignal): Promise<TransferFileEntry[]> {
  const response = await fetch(`/api/files?kind=${kind}`, { signal });
  if (!response.ok) {
    throw new Error(`File list request failed: ${response.status}`);
  }
  const payload = (await response.json()) as { files?: TransferFileEntry[] };
  return payload.files ?? [];
}

export type BootstrapPeer = {
  host: string;
  port: number;
};

export async function bootstrapPeer(peer: BootstrapPeer): Promise<void> {
  const params = new URLSearchParams({
    host: peer.host,
    port: String(peer.port),
  });
  const response = await fetch(`/api/swarm/bootstrap?${params.toString()}`, {
    method: 'POST',
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(body || `Swarm bootstrap failed: ${response.status}`);
  }
}

export async function addSyncPeer(peer: SyncPeer): Promise<void> {
  const params = new URLSearchParams({
    host: peer.host,
    port: String(peer.port),
  });
  const response = await fetch(`/api/sync/peers?${params.toString()}`, {
    method: 'POST',
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(body || `Sync peer request failed: ${response.status}`);
  }
}

export async function removeSyncPeer(peer: SyncPeer): Promise<void> {
  const params = new URLSearchParams({
    host: peer.host,
    port: String(peer.port),
  });
  const response = await fetch(`/api/sync/peers/remove?${params.toString()}`, {
    method: 'POST',
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(body || `Sync peer remove failed: ${response.status}`);
  }
}

export async function startDownload(torrentId: string): Promise<void> {
  const response = await fetch(`/api/downloads/start?torrentId=${encodeURIComponent(torrentId)}`, {
    method: 'POST',
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(body || `Download request failed: ${response.status}`);
  }
}

export type SendFileOptions = {
  file: File;
  peerHost: string;
  peerPort: number;
  onProgress: (percent: number) => void;
};

export type UploadSharedFileOptions = {
  file: File;
  onProgress: (percent: number) => void;
};

export type PublishFileOptions = {
  file: File;
  onProgress: (percent: number) => void;
};

export type SharedSyncResult = {
  peers: number;
  files: number;
  attempted: number;
  synced: number;
};

export async function syncSharedFolder(): Promise<SharedSyncResult> {
  const response = await fetch('/api/shared/sync', {
    method: 'POST',
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(body || `Shared sync failed: ${response.status}`);
  }
  return response.json() as Promise<SharedSyncResult>;
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

    request.onload = () => {
      if (request.status >= 200 && request.status < 300) {
        resolve();
        return;
      }
      reject(new Error(request.responseText || `Publish failed: ${request.status}`));
    };

    request.onerror = () => reject(new Error('Could not reach the C++ backend'));
    request.send(file);
  });
}

export function uploadSharedFile({ file, onProgress }: UploadSharedFileOptions): Promise<string> {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    request.open('POST', '/api/shared/upload');
    request.setRequestHeader('X-File-Name', encodeURIComponent(file.name));
    request.setRequestHeader('Content-Type', 'application/octet-stream');

    request.upload.onprogress = (event) => {
      if (event.lengthComputable) {
        onProgress(getTransferPercent(event.loaded, event.total));
      }
    };

    request.onload = () => {
      if (request.status >= 200 && request.status < 300) {
        try {
          const payload = JSON.parse(request.responseText) as { name?: string };
          resolve(payload.name ?? file.name);
        } catch {
          resolve(file.name);
        }
        return;
      }

      reject(new Error(request.responseText || `Shared upload failed: ${request.status}`));
    };

    request.onerror = () => reject(new Error('Could not reach the C++ backend'));
    request.send(file);
  });
}

export function sendFile({ file, peerHost, peerPort, onProgress }: SendFileOptions): Promise<string> {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    request.open('POST', '/api/send');
    request.setRequestHeader('X-File-Name', encodeURIComponent(file.name));
    request.setRequestHeader('X-Peer-Host', peerHost);
    request.setRequestHeader('X-Peer-Port', String(peerPort));
    request.setRequestHeader('Content-Type', 'application/octet-stream');

    request.upload.onprogress = (event) => {
      if (event.lengthComputable) {
        onProgress(getTransferPercent(event.loaded, event.total));
      }
    };

    request.onload = () => {
      if (request.status >= 200 && request.status < 300) {
        try {
          const payload = JSON.parse(request.responseText) as { id?: string };
          resolve(payload.id ?? '');
        } catch {
          resolve('');
        }
        return;
      }

      reject(new Error(request.responseText || `Send failed: ${request.status}`));
    };

    request.onerror = () => reject(new Error('Could not reach the C++ backend'));
    request.send(file);
  });
}
