import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';

describe('shared sync cleanup', () => {
  it('no longer references shared-sync routes or services in the main backend path', () => {
    const appSource = readFileSync('backend/src/app/backend_app.cpp', 'utf8');
    const controllerSource = readFileSync('backend/src/controllers/http_controller.cpp', 'utf8');

    expect(appSource).not.toContain('SharedSyncService');
    expect(appSource).not.toContain('TransferService transfer_service');
    expect(controllerSource).not.toContain('/api/shared/sync');
    expect(controllerSource).not.toContain('/api/shared/upload');
    expect(controllerSource).not.toContain('/api/send');
  });
});
