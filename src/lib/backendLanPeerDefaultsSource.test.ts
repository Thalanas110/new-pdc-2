import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('backend LAN peer defaults', () => {
  it('allows private LAN peers by default for laptop-to-laptop sharing', () => {
    const source = readFileSync('backend/src/main.cpp', 'utf8');

    expect(source).toContain('std::string bind_host = "0.0.0.0";');
    expect(source).toContain('bool allow_remote_peers = true;');
    expect(source).toContain('env_value("P2P_BIND_HOST", "0.0.0.0")');
    expect(source).toContain('env_flag("P2P_ALLOW_REMOTE", true)');
  });
});
