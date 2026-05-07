import type { BackendStatus, FileKind, SyncPeer, TransferFileEntry } from './transferModel';
import { getTransferPercent } from './transferModel';

export async function fetchStatus(signal?: AbortSignal): Promise<BackendStatus> {
  const response = await fetch('/api/status', { signal });
  if (!response.ok) {
    throw new Error(`Status request failed: ${response.status}`);
  }
  return response.json() as Promise<BackendStatus>;
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

export async function fetchFiles(kind: FileKind, signal?: AbortSignal): Promise<TransferFileEntry[]> {
  const response = await fetch(`/api/files?kind=${kind}`, { signal });
  if (!response.ok) {
    throw new Error(`File list request failed: ${response.status}`);
  }
  const payload = (await response.json()) as { files?: TransferFileEntry[] };
  return payload.files ?? [];
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
