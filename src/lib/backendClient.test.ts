import { afterEach, describe, expect, it, vi } from 'vitest';
import { bootstrapPeer, fetchLibrary, fetchStatus, publishFile, startDownload } from './backendClient';

type HeaderMap = Record<string, string>;

class FakeXMLHttpRequest {
  static latest: FakeXMLHttpRequest | null = null;

  method = '';
  url = '';
  requestBody: BodyInit | null = null;
  requestHeaders: HeaderMap = {};
  responseText = '{"ok":true}';
  status = 200;
  upload: { onprogress: ((event: ProgressEvent) => void) | null } = { onprogress: null };
  onerror: (() => void) | null = null;
  onload: (() => void) | null = null;

  constructor() {
    FakeXMLHttpRequest.latest = this;
  }

  open(method: string, url: string) {
    this.method = method;
    this.url = url;
  }

  setRequestHeader(name: string, value: string) {
    this.requestHeaders[name] = value;
  }

  send(body: BodyInit) {
    this.requestBody = body;
    this.onload?.();
  }
}

describe('backend client swarm calls', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    FakeXMLHttpRequest.latest = null;
  });

  it('publishes a file to the publish endpoint', async () => {
    vi.stubGlobal('XMLHttpRequest', FakeXMLHttpRequest);
    const file = new File(['swarm contents'], 'swarm-note.txt', { type: 'text/plain' });

    await publishFile({
      file,
      onProgress: vi.fn(),
    });

    const request = FakeXMLHttpRequest.latest;
    expect(request).not.toBeNull();
    expect(request?.method).toBe('POST');
    expect(request?.url).toBe('/api/publish');
    expect(request?.requestHeaders['X-File-Name']).toBe(encodeURIComponent(file.name));
    expect(request?.requestHeaders['Content-Type']).toBe('application/octet-stream');
    expect(request?.requestBody).toBe(file);
  });

  it('bootstraps a peer through the swarm bootstrap endpoint', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response(null, { status: 200 }));
    vi.stubGlobal('fetch', fetchMock);

    await bootstrapPeer({ host: '127.0.0.1', port: 9090 });

    expect(fetchMock).toHaveBeenCalledWith('/api/swarm/bootstrap?host=127.0.0.1&port=9090', {
      method: 'POST',
    });
  });

  it('fetches the torrent library from the library endpoint', async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response('{"library":[]}', {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      }),
    );
    vi.stubGlobal('fetch', fetchMock);

    await fetchLibrary();

    expect(fetchMock).toHaveBeenCalledWith('/api/library');
  });

  it('reads swarm peers and downloads from the status endpoint', async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(
        JSON.stringify({
          nodeId: 'node-a',
          host: '127.0.0.1',
          httpPort: 8787,
          transferPort: 8788,
          peers: [{ nodeId: 'node-b', host: '127.0.0.1', port: 8789, source: 'bootstrap', lastSeenAt: '', reachable: true }],
          downloads: [
            {
              torrentId: 'torrent-a',
              displayName: 'demo.bin',
              status: 'downloading',
              fileSize: 700000,
              verifiedPieces: 2,
              pieceCount: 3,
              activePeers: ['127.0.0.1:8788', '127.0.0.1:8789'],
            },
          ],
          transfers: [],
          receiveDir: 'backend/received',
          listenerActive: true,
        }),
        {
          status: 200,
          headers: { 'Content-Type': 'application/json' },
        },
      ),
    );
    vi.stubGlobal('fetch', fetchMock);

    const status = await fetchStatus();

    expect(fetchMock).toHaveBeenCalledWith('/api/status', { signal: undefined });
    expect(status.peers).toHaveLength(1);
    expect(status.downloads[0]?.activePeers).toHaveLength(2);
    expect(status.listenerActive).toBe(true);
  });

  it('starts a download through the swarm downloads endpoint', async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response(null, { status: 200 }));
    vi.stubGlobal('fetch', fetchMock);

    await startDownload('torrent-a');

    expect(fetchMock).toHaveBeenCalledWith('/api/downloads/start?torrentId=torrent-a', {
      method: 'POST',
    });
  });
});
