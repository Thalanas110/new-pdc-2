import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('backend shared upload route', () => {
  it('exposes an HTTP route that saves browser uploads into the shared folder', () => {
    const appSource = readFileSync('backend/src/app/backend_app.cpp', 'utf8');
    const controllerSource = readFileSync('backend/src/controllers/http_controller.cpp', 'utf8');

    expect(appSource).toContain('deps.save_shared_upload');
    expect(appSource).toContain('file_vault_service.save_shared_upload');
    expect(controllerSource).toContain('route == "/api/shared/upload"');
  });
});
