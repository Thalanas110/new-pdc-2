import { describe, expect, it } from 'vitest';
import {
  buildPeerUrl,
  formatBytes,
  getFilePreviewKind,
  getTransferPercent,
  isAllowedPeerAddress,
  isLocalhostAddress,
} from './transferModel';

describe('transfer model', () => {
  it('formats transfer sizes with useful units', () => {
    expect(formatBytes(0)).toBe('0 B');
    expect(formatBytes(1024)).toBe('1 KB');
    expect(formatBytes(1536)).toBe('1.5 KB');
    expect(formatBytes(5 * 1024 * 1024)).toBe('5 MB');
  });

  it('keeps progress inside the visible 0-100 range', () => {
    expect(getTransferPercent(25, 100)).toBe(25);
    expect(getTransferPercent(500, 100)).toBe(100);
    expect(getTransferPercent(-10, 100)).toBe(0);
    expect(getTransferPercent(50, 0)).toBe(0);
  });

  it('only treats loopback addresses as local peers', () => {
    expect(isLocalhostAddress('127.0.0.1')).toBe(true);
    expect(isLocalhostAddress('localhost')).toBe(true);
    expect(isLocalhostAddress('0.0.0.0')).toBe(false);
    expect(isLocalhostAddress('192.168.1.7')).toBe(false);
  });

  it('allows localhost and private LAN peers for docker p2p mode', () => {
    expect(isAllowedPeerAddress('127.0.0.1')).toBe(true);
    expect(isAllowedPeerAddress('192.168.1.42')).toBe(true);
    expect(isAllowedPeerAddress('10.10.0.15')).toBe(true);
    expect(isAllowedPeerAddress('172.20.4.9')).toBe(true);
    expect(isAllowedPeerAddress('8.8.8.8')).toBe(false);
    expect(isAllowedPeerAddress('0.0.0.0')).toBe(false);
  });

  it('accepts common phone hotspot address ranges', () => {
    expect(isAllowedPeerAddress('172.20.10.2')).toBe(true);
    expect(isAllowedPeerAddress('192.168.43.25')).toBe(true);
    expect(isAllowedPeerAddress('192.168.137.18')).toBe(true);
  });

  it('builds a stable receiver URL for localhost peers', () => {
    expect(buildPeerUrl('127.0.0.1', 8787)).toBe('http://127.0.0.1:8787');
    expect(buildPeerUrl('localhost', 9000)).toBe('http://localhost:9000');
  });

  it('chooses inline preview modes from file metadata', () => {
    expect(getFilePreviewKind('photo.png', 'image/png')).toBe('image');
    expect(getFilePreviewKind('paper.pdf', 'application/pdf')).toBe('pdf');
    expect(getFilePreviewKind('notes.txt', 'text/plain')).toBe('text');
    expect(getFilePreviewKind('clip.mp4', 'video/mp4')).toBe('video');
    expect(getFilePreviewKind('voice.mp3', 'audio/mpeg')).toBe('audio');
    expect(getFilePreviewKind('archive.zip', 'application/zip')).toBe('unsupported');
  });
});
