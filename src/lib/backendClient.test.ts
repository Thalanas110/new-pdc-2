import { afterEach, describe, expect, it, vi } from 'vitest';
import { uploadSharedFile } from './backendClient';

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

describe('backend client shared uploads', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    FakeXMLHttpRequest.latest = null;
  });

  it('uploads a file directly into the shared folder endpoint', async () => {
    vi.stubGlobal('XMLHttpRequest', FakeXMLHttpRequest);
    const file = new File(['shared contents'], 'shared note.txt', { type: 'text/plain' });

    await uploadSharedFile({
      file,
      onProgress: vi.fn(),
    });

    const request = FakeXMLHttpRequest.latest;
    expect(request).not.toBeNull();
    expect(request?.method).toBe('POST');
    expect(request?.url).toBe('/api/shared/upload');
    expect(request?.requestHeaders['X-File-Name']).toBe(encodeURIComponent(file.name));
    expect(request?.requestHeaders['Content-Type']).toBe('application/octet-stream');
    expect(request?.requestBody).toBe(file);
  });
});
