import { type ChangeEvent, useEffect, useState } from 'react';

import {
  bootstrapPeer,
  fetchLibrary,
  fetchStatus,
  publishFile,
  startDownload as requestDownload,
} from '../lib/backendClient';
import {
  type BackendStatus,
  type TorrentDownloadEntry,
  type TorrentLibraryEntry,
  isAllowedPeerAddress,
} from '../lib/transferModel';
import { DownloadsView } from './torrent/DownloadsView';
import { LibraryView } from './torrent/LibraryView';
import { PublishView } from './torrent/PublishView';
import { SwarmView } from './torrent/SwarmView';

type ViewMode = 'publish' | 'swarm' | 'library' | 'downloads';

function emptyStatus(): BackendStatus {
  return {
    nodeId: '',
    host: '127.0.0.1',
    httpPort: 8787,
    transferPort: 8788,
    peers: [],
    library: [],
    downloads: [],
    receiveDir: '',
    transfers: [],
    listenerActive: false,
    syncPeers: [],
  };
}

export function HomeView() {
  const [activeView, setActiveView] = useState<ViewMode>('publish');
  const [status, setStatus] = useState<BackendStatus>(emptyStatus);
  const [library, setLibrary] = useState<TorrentLibraryEntry[]>([]);
  const [downloads, setDownloads] = useState<TorrentDownloadEntry[]>([]);
  const [notice, setNotice] = useState('Swarm idle');
  const [peerHost, setPeerHost] = useState('127.0.0.1');
  const [peerPort, setPeerPort] = useState(8788);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [publishing, setPublishing] = useState(false);
  const [publishPercent, setPublishPercent] = useState(0);

  useEffect(() => {
    let cancelled = false;

    const refresh = async () => {
      try {
        const [nextStatus, nextLibrary] = await Promise.all([fetchStatus(), fetchLibrary()]);
        if (cancelled) {
          return;
        }

        setStatus(nextStatus);
        setPeerPort((currentPort) => (currentPort === 8788 ? nextStatus.transferPort : currentPort));
        setLibrary(nextLibrary);
        setDownloads(nextStatus.downloads);
      } catch {
        if (!cancelled) {
          setNotice('Backend offline');
        }
      }
    };

    void refresh();
    const timer = window.setInterval(() => {
      void refresh();
    }, 1500);

    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, []);

  const onFileChange = (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0] ?? null;
    setSelectedFile(file);
    setPublishPercent(0);
    setNotice(file ? `${file.name} staged for publish` : 'No file selected');
  };

  const onPublish = async () => {
    if (!selectedFile) {
      return;
    }

    setPublishing(true);
    setPublishPercent(0);
    setNotice(`Publishing ${selectedFile.name}`);
    try {
      await publishFile({
        file: selectedFile,
        onProgress: setPublishPercent,
      });
      setNotice(`Published ${selectedFile.name}`);
      setLibrary(await fetchLibrary());
    } catch (error) {
      setNotice(error instanceof Error ? error.message : 'Publish failed');
    } finally {
      setPublishing(false);
    }
  };

  const onBootstrap = async () => {
    if (!isAllowedPeerAddress(peerHost)) {
      setNotice('Use localhost or a private LAN peer');
      return;
    }

    try {
      await bootstrapPeer({ host: peerHost, port: peerPort });
      setStatus(await fetchStatus());
      setNotice(`Connected to ${peerHost}:${peerPort}`);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : 'Bootstrap failed');
    }
  };

  const onStartDownload = async (torrentId: string) => {
    try {
      await requestDownload(torrentId);
      const nextStatus = await fetchStatus();
      setStatus(nextStatus);
      setDownloads(nextStatus.downloads);
      setActiveView('downloads');
      setNotice(`Started ${torrentId}`);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : 'Download failed');
    }
  };

  let currentView = (
    <PublishView
      selectedFile={selectedFile}
      publishing={publishing}
      publishPercent={publishPercent}
      onFileChange={onFileChange}
      onPublish={onPublish}
    />
  );

  if (activeView === 'swarm') {
    currentView = (
      <SwarmView
        peerHost={peerHost}
        peerPort={peerPort}
        peers={status.peers}
        onPeerHostChange={setPeerHost}
        onPeerPortChange={setPeerPort}
        onBootstrap={onBootstrap}
      />
    );
  } else if (activeView === 'library') {
    currentView = <LibraryView library={library} onStartDownload={onStartDownload} />;
  } else if (activeView === 'downloads') {
    currentView = <DownloadsView downloads={downloads} />;
  }

  return (
    <main className="loop-shell">
      <header className="loop-header">
        <p className="loop-kicker">Loopline Torrent Swarm</p>
        <p aria-label="Swarm status">{notice}</p>
      </header>

      <nav className="mode-tabs" aria-label="Torrent pages">
        <button type="button" aria-pressed={activeView === 'publish'} onClick={() => setActiveView('publish')}>
          Publish
        </button>
        <button type="button" aria-pressed={activeView === 'swarm'} onClick={() => setActiveView('swarm')}>
          Swarm
        </button>
        <button type="button" aria-pressed={activeView === 'library'} onClick={() => setActiveView('library')}>
          Library
        </button>
        <button type="button" aria-pressed={activeView === 'downloads'} onClick={() => setActiveView('downloads')}>
          Downloads
        </button>
      </nav>

      {currentView}
    </main>
  );
}
