import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { HomeView } from './LooplineTransferApp';

describe('root transfer home', () => {
  it('renders the reference-style transfer and receive pages', () => {
    render(<HomeView />);

    expect(screen.getByRole('heading', { name: 'Transfer' })).toBeInTheDocument();
    expect(screen.getByLabelText('Transfer status')).toHaveTextContent('Backend waiting on 127.0.0.1:8787');
    expect(screen.getByLabelText('Transfer status')).toHaveTextContent('Received 0');
    expect(screen.getAllByText('No files')).not.toHaveLength(0);
    expect(screen.getByRole('button', { name: 'Transfer' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Receive' })).toHaveAttribute('aria-pressed', 'false');
    expect(screen.getByRole('button', { name: 'Shared' })).toHaveAttribute('aria-pressed', 'false');
    expect(screen.getByLabelText('HTTP')).toHaveValue('127.0.0.1');
    expect(screen.getByLabelText('Socket')).toHaveValue(8788);
    expect(screen.getByLabelText('Inbox count')).toHaveValue(0);
    expect(screen.getByText(/Click anywhere to upload/i)).toBeInTheDocument();
    expect(screen.getAllByRole('heading', { name: 'Uploaded Files' })).toHaveLength(2);

    fireEvent.click(screen.getByRole('button', { name: 'Receive' }));

    expect(screen.getByRole('heading', { name: 'Receive' })).toBeInTheDocument();
    expect(screen.getByLabelText('Receive status')).toHaveTextContent('Inbox 0');
    expect(screen.getByRole('button', { name: 'Start receiving' })).toBeInTheDocument();
    expect(screen.getByText('RECEIVED FILES')).toBeInTheDocument();
  });

  it('renders the shared workspace with folder sync controls', () => {
    render(<HomeView />);

    fireEvent.click(screen.getByRole('button', { name: 'Shared' }));

    expect(screen.getByRole('heading', { name: 'Shared' })).toBeInTheDocument();
    expect(screen.getByLabelText('Shared status')).toHaveTextContent('Shared 0');
    expect(screen.getByLabelText('Shared status')).toHaveTextContent('Peers 0');
    expect(screen.getByRole('button', { name: 'Start sharing' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Sync now' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Upload to shared' })).toBeDisabled();
    expect(screen.getByText('Click to stage shared upload')).toBeInTheDocument();
    expect(screen.getByText('SHARED FILES')).toBeInTheDocument();
    expect(screen.getByText('SYNC PEERS')).toBeInTheDocument();
    expect(screen.getByText('No sync peers')).toBeInTheDocument();
    expect(screen.getByLabelText('Inbox count')).toHaveValue(0);

    fireEvent.change(screen.getByLabelText('Inbox count'), {
      target: { value: '7' },
    });

    expect(screen.getByLabelText('Inbox count')).toHaveValue(7);

    fireEvent.change(screen.getByLabelText('Shared file'), {
      target: {
        files: [new File(['shared'], 'shared-note.txt', { type: 'text/plain' })],
      },
    });

    expect(screen.getByRole('button', { name: 'Upload to shared' })).toBeEnabled();
    expect(screen.getAllByText(/shared-note.txt/)).not.toHaveLength(0);
  });

  it('allows unlimited inbox counts across transfer, receive, and shared', () => {
    render(<HomeView />);

    fireEvent.change(screen.getByLabelText('Inbox count'), {
      target: { value: '2500' },
    });

    expect(screen.getByLabelText('Inbox count')).toHaveValue(2500);
    expect(screen.getByLabelText('Transfer status')).toHaveTextContent('Received 2500');

    fireEvent.click(screen.getByRole('button', { name: 'Receive' }));
    fireEvent.change(screen.getByLabelText('Inbox count'), {
      target: { value: '5000' },
    });

    expect(screen.getByLabelText('Inbox count')).toHaveValue(5000);
    expect(screen.getByLabelText('Receive status')).toHaveTextContent('Inbox 5000');

    fireEvent.click(screen.getByRole('button', { name: 'Shared' }));
    fireEvent.change(screen.getByLabelText('Inbox count'), {
      target: { value: '10000' },
    });

    expect(screen.getByLabelText('Inbox count')).toHaveValue(10000);
  });
});
