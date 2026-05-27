import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { HomeView } from './LooplineTransferApp';

describe('torrent swarm home', () => {
  it('renders publish, swarm, library, downloads, and files tabs', () => {
    render(<HomeView />);

    expect(screen.getByRole('button', { name: 'Publish' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Swarm' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Library' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Downloads' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Files' })).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Publish' })).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'How to operate the swarm.' })).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Library' }));
    expect(screen.getByRole('heading', { name: 'Library' })).toBeInTheDocument();
  });
});
