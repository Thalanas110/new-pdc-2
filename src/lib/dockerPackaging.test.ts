import { describe, expect, it } from 'vitest';
import dockerfileText from '../../Dockerfile?raw';

describe('docker packaging', () => {
  it('normalizes the entrypoint script and invokes it through sh', () => {
    expect(dockerfileText).toContain("sed -i 's/\\r$//'");
    expect(dockerfileText).toContain('CMD ["/bin/sh", "/usr/local/bin/loopline-entrypoint"]');
  });

  it('mounts the shared folder and torrent store into the runtime container', () => {
    expect(dockerfileText).toContain('P2P_SHARED_DIR=/data/shared');
    expect(dockerfileText).toContain('VOLUME ["/data/received", "/data/sent", "/data/shared", "/data/torrents"]');
  });
});
