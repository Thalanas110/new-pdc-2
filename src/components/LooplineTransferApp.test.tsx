import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { HomeView } from './LooplineTransferApp';

describe('torrent swarm home', () => {
  it('renders publish, swarm, library, and downloads tabs', () => {
    render(<HomeView />);

    expect(screen.getByRole('button', { name: 'Publish' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Swarm' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Library' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Downloads' })).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Publish' })).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Library' }));
    expect(screen.getByRole('heading', { name: 'Library' })).toBeInTheDocument();
  });
});
