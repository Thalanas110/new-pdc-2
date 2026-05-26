import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('backend swarm HTTP wiring', () => {
  it('wires the swarm services and routes through the backend controller', () => {
    const appSource = readFileSync('backend/src/app/backend_app.cpp', 'utf8');
    const controllerSource = readFileSync('backend/src/controllers/http_controller.cpp', 'utf8');

    expect(appSource).toContain('DiscoveryService discovery_service');
    expect(appSource).toContain('SwarmTransferService swarm_transfer_service');
    expect(controllerSource).toContain('route == "/api/publish"');
    expect(controllerSource).toContain('route == "/api/swarm/bootstrap"');
    expect(controllerSource).toContain('route == "/api/library"');
    expect(controllerSource).toContain('route == "/api/downloads/start"');
  });
});
