import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';

describe('backend shared sync route', () => {
  it('keeps automatic shared sync and exposes a manual sync trigger', () => {
    const appSource = readFileSync('backend/src/app/backend_app.cpp', 'utf8');
    const controllerSource = readFileSync('backend/src/controllers/http_controller.cpp', 'utf8');
    const syncSource = readFileSync('backend/src/services/shared-sync/shared_sync_service_outbound.cpp', 'utf8');

    expect(appSource).toContain('std::thread shared_watcher(&SharedSyncService::watch_shared_folder, &shared_sync_service);');
    expect(controllerSource).toContain('route == "/api/shared/sync"');
    expect(syncSource).toContain('SharedSyncSummary SharedSyncService::sync_shared_folder_once()');
    expect(syncSource).toContain('send_shared_file_to_peer(peer, name, file_path, *signature, true)');
  });
});
