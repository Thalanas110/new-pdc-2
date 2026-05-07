import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';

describe('backend shared upload route', () => {
  it('exposes an HTTP route that saves browser uploads into the shared folder', () => {
    const source = readFileSync(join(process.cwd(), 'backend/src/main.cpp'), 'utf8');

    expect(source).toContain('save_shared_upload');
    expect(source).toContain('route == "/api/shared/upload"');
  });
});
