import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { HomeView } from './__root';

describe('root transfer home', () => {
  it('renders the localhost transfer console', () => {
    render(<HomeView />);

    expect(screen.getByRole('heading', { name: /Loopline/i })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Start receiver/i })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Send file/i })).toBeDisabled();
    expect(screen.getByLabelText(/Peer host/i)).toHaveValue('127.0.0.1');
  });
});
