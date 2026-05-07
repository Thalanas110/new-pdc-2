import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { HomeView } from './LooplineTransferApp';

describe('root transfer home', () => {
  it('renders the reference-style transfer and receive pages', () => {
    render(<HomeView />);

    expect(screen.getByRole('heading', { name: 'Transfer' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Transfer' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Receive' })).toHaveAttribute('aria-pressed', 'false');
    expect(screen.getByLabelText('HTTP')).toHaveValue('127.0.0.1');
    expect(screen.getByLabelText('Socket')).toHaveValue(8788);
    expect(screen.getByLabelText('Inbox')).toHaveValue('0');
    expect(screen.getByText(/Click anywhere to upload/i)).toBeInTheDocument();
    expect(screen.getAllByRole('heading', { name: 'Uploaded Files' })).toHaveLength(2);

    fireEvent.click(screen.getByRole('button', { name: 'Receive' }));

    expect(screen.getByRole('heading', { name: 'Receive' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Start receiving' })).toBeInTheDocument();
    expect(screen.getByText('RECEIVED FILES')).toBeInTheDocument();
  });
});
