import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('backend LAN peer defaults', () => {
  it('allows private LAN peers by default for laptop-to-laptop sharing', () => {
    const source = readFileSync('backend/src/app/backend_app.cpp', 'utf8');

    expect(source).toContain('state.bind_host = arg_string(argc, argv, "--bind", env_value("P2P_BIND_HOST", "0.0.0.0"))');
    expect(source).toContain('state.allow_remote_peers = env_flag("P2P_ALLOW_REMOTE", true);');
    expect(source).toContain('state.advertised_host = detect_private_lan_host().value_or("127.0.0.1");');
    expect(source).toContain('discovery_service.set_peer_detected_callback');
    expect(source).toContain('discovery_service.start();');
  });
});
