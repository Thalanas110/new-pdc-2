import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

describe('backend shared sync route', () => {
  it('keeps automatic shared sync and exposes a manual sync trigger', () => {
    const source = readFileSync('backend/src/main.cpp', 'utf8');

    expect(source).toMatch(/shared_folder_watcher[\s\S]*send_shared_file_to_peer/);
    expect(source).toContain('sync_shared_folder_once');
    expect(source).toContain('route == "/api/shared/sync"');
  });
});
