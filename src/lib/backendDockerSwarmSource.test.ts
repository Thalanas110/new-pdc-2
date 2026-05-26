import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('docker swarm packaging', () => {
  it('builds the swarm backend explicitly and persists torrent pieces', () => {
    const dockerfile = readFileSync('Dockerfile', 'utf8');
    const compose = readFileSync('docker-compose.yml', 'utf8');
    const entrypoint = readFileSync('docker/entrypoint.sh', 'utf8');

    expect(dockerfile).toContain('backend/src/services/swarm/swarm_transfer_service.cpp');
    expect(dockerfile).not.toContain("find backend/src -type f -name '*.cpp'");
    expect(dockerfile).not.toContain('shared_sync_service_core.cpp');
    expect(dockerfile).not.toContain('services/transfer/transfer_service.cpp');
    expect(dockerfile).toContain('/data/torrents');
    expect(dockerfile).toContain('/app/backend/torrents');
    expect(compose).toContain('./backend/torrents:/data/torrents');
    expect(entrypoint).toContain('/data/torrents');
    expect(entrypoint).toContain('/app/backend/torrents');
  });
});
