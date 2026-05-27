import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { HomeView } from './LooplineTransferApp';

describe('torrent swarm home', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('renders publish, swarm, library, and downloads tabs', async () => {
    const fetchMock = vi.fn(async (input: string | URL | Request) => {
      const url = String(input);
      if (url.includes('/api/library')) {
        return new Response('[]', {
          status: 200,
          headers: { 'Content-Type': 'application/json' },
        });
      }

      return new Response(
        JSON.stringify({
          nodeId: 'node-a',
          host: '127.0.0.1',
          httpPort: 8787,
          transferPort: 8788,
          peers: [],
          library: [],
          downloads: [],
          receiveDir: 'backend/received',
          listenerActive: true,
          transfers: [],
          syncPeers: [],
        }),
        {
          status: 200,
          headers: { 'Content-Type': 'application/json' },
        },
      );
    });
    vi.stubGlobal('fetch', fetchMock);

    render(<HomeView />);

    await waitFor(() => {
      expect(fetchMock).toHaveBeenCalled();
    });

    expect(screen.getByRole('button', { name: 'Publish' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Swarm' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Library' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Downloads' })).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Publish' })).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'How to operate the swarm.' })).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Library' }));
    expect(screen.getByRole('heading', { name: 'Library' })).toBeInTheDocument();
  });
});
