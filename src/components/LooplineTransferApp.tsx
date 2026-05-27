import { type ChangeEvent, useEffect, useState } from 'react';

import {
  bootstrapPeer,
  fetchFiles,
  fetchLibrary,
  fetchStatus,
  publishFile,
  startDownload as requestDownload,
} from '../lib/backendClient';
import {
  type BackendStatus,
  type FileKind,
  type TransferFileEntry,
  type TorrentDownloadEntry,
  type TorrentLibraryEntry,
  formatBytes,
  isAllowedPeerAddress,
} from '../lib/transferModel';
import { DownloadsView } from './torrent/DownloadsView';
import { FilesView } from './torrent/FilesView';
import { GuidePanel } from './torrent/GuidePanel';
import { LibraryView } from './torrent/LibraryView';
import { PublishView } from './torrent/PublishView';
import { SwarmView } from './torrent/SwarmView';

type ViewMode = 'publish' | 'swarm' | 'library' | 'downloads' | 'files';

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
  const [fileKind, setFileKind] = useState<FileKind>('received');
  const [files, setFiles] = useState<TransferFileEntry[]>([]);

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

  useEffect(() => {
    if (activeView !== 'files') {
      return;
    }

    let cancelled = false;
    const controller = new AbortController();

    const refresh = async () => {
      try {
        const nextFiles = await fetchFiles(fileKind, controller.signal);
        if (!cancelled) {
          setFiles(nextFiles);
        }
      } catch {
        if (!cancelled) {
          setFiles([]);
        }
      }
    };

    void refresh();
    const timer = window.setInterval(() => {
      void refresh();
    }, 2000);

    return () => {
      cancelled = true;
      controller.abort();
      window.clearInterval(timer);
    };
  }, [activeView, fileKind]);

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

  const liveSeeders = library.filter((entry) => entry.localStatus === 'seeding' || entry.localStatus === 'complete').length;
  const totalPayload = library.reduce((sum, entry) => sum + entry.fileSize, 0);
  const downloadPressure = downloads.filter((entry) => entry.status === 'downloading' || entry.status === 'discovering').length;
  const guideTone = status.listenerActive ? 'good' : 'bad';

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
  } else if (activeView === 'files') {
    currentView = <FilesView kind={fileKind} files={files} onKindChange={setFileKind} />;
  }

  return (
    <main className="loop-shell">
      <div className="loop-gridlines" aria-hidden="true" />
      <div className="loop-noise" aria-hidden="true" />
      <header className="loop-header">
        <div className="loop-header__copy">
          <p className="loop-kicker">Loopline // Swarm Console</p>
          <h1 className="loop-title">Hotspot-ready torrent distribution for your class demo.</h1>
          <p className="loop-lede">
            Immutable file publishing, verified chunk exchange, bootstrap fallback, and automatic reseeding after
            completion.
          </p>
        </div>

        <div className="loop-header__telemetry">
          <div className={`signal-banner ${guideTone}`}>
            <span className="signal-banner__label">Swarm status</span>
            <strong aria-label="Swarm status">{notice}</strong>
            <span className="signal-banner__meta">
              {status.listenerActive ? 'Transfer listener armed' : 'Listener offline'} / port {status.transferPort}
            </span>
          </div>

          <div className="telemetry-grid">
            <article className="telemetry-card">
              <span>Known peers</span>
              <strong>{status.peers.length}</strong>
              <small>Bootstrap once, then let the swarm spread peer intel.</small>
            </article>
            <article className="telemetry-card">
              <span>Published files</span>
              <strong>{library.length}</strong>
              <small>{formatBytes(totalPayload)} currently exposed to the library.</small>
            </article>
            <article className="telemetry-card">
              <span>Local seeders</span>
              <strong>{liveSeeders}</strong>
              <small>Completed downloads turn into seeders automatically.</small>
            </article>
            <article className="telemetry-card">
              <span>Active pulls</span>
              <strong>{downloadPressure}</strong>
              <small>Parallel piece requests update in the downloads rail.</small>
            </article>
          </div>
        </div>
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

      <section className="control-deck">
        <div className="control-stage">{currentView}</div>

        <aside className="control-rail">
          <GuidePanel transferPort={status.transferPort} />

          <section className="rail-card rail-card--dark" aria-labelledby="ops-heading">
            <div className="rail-card__eyebrow">Operator Readout</div>
            <h2 id="ops-heading">What the class should watch for.</h2>
            <ul className="ops-list">
              <li>Every publish creates a new immutable torrent entry. Editing a file means republishing it.</li>
              <li>The `Library` shows what the swarm knows. The `Downloads` view shows verified piece progress.</li>
              <li>
                If your phone hotspot hides peers, use the `Swarm` tab and type one seeder&apos;s host plus port{' '}
                {status.transferPort}.
              </li>
              <li>After one downloader finishes, start another download and show that the seeder count increases.</li>
            </ul>
          </section>
        </aside>
      </section>
    </main>
  );
}
