import { describe, expect, it } from 'vitest';
import dockerfileText from '../../Dockerfile?raw';

describe('docker packaging', () => {
  it('normalizes the entrypoint script and invokes it through sh', () => {
    expect(dockerfileText).toContain("sed -i 's/\\r$//'");
    expect(dockerfileText).toContain('CMD ["/bin/sh", "/usr/local/bin/loopline-entrypoint"]');
  });
});
