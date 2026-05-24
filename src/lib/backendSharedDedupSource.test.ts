import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

describe('backend shared dedup sync', () => {
  it('short-circuits chunk requests when the shared destination is already current', () => {
    const source = readFileSync('backend/src/services/shared-sync/shared_sync_service_core.cpp', 'utf8');

    expect(source).toContain('const auto existing_signature = shared_file_signature(state_.shared_dir / safe_name);');
    expect(source).toContain('if (existing_signature && *existing_signature == signature) {');
    expect(source).toContain('return {};');
  });
});
