import { Loader2, Plus, RefreshCw } from 'lucide-react';
import { type ChangeEvent, type DragEvent, type ReactNode, useCallback, useEffect, useMemo, useState } from 'react';
import {
  addSyncPeer,
  fetchFiles,
  fetchStatus,
  removeSyncPeer,
  sendFile,
  startReceiver,
  syncSharedFolder,
  uploadSharedFile,
} from '../lib/backendClient';
import {
  type BackendStatus,
  type FileKind,
  type SyncPeer,
  type TransferFileEntry,
  fileKinds,
  formatBytes,
  isAllowedPeerAddress,
} from '../lib/transferModel';

type Notice = {
  tone: 'good' | 'bad' | 'quiet';
  text: string;
};

type ViewMode = 'transfer' | 'receive' | 'shared';

type PanelFile = {
  id: string;
  name: string;
  detail: string;
  url?: string;
};

type StatusMetric = {
  text: string;
  tone?: Notice['tone'];
};

export function HomeView() {
  const [activeView, setActiveView] = useState<ViewMode>('transfer');
  const [status, setStatus] = useState<BackendStatus | null>(null);
  const [notice, setNotice] = useState<Notice>({ tone: 'quiet', text: 'Backend waiting on 127.0.0.1:8787' });
  const [peerHost, setPeerHost] = useState('127.0.0.1');
  const [peerPort, setPeerPort] = useState(8788);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [filesByKind, setFilesByKind] = useState<Record<FileKind, TransferFileEntry[]>>({
    received: [],
    sent: [],
    shared: [],
  });
  const [dragging, setDragging] = useState(false);
  const [sending, setSending] = useState(false);
  const [uploadPercent, setUploadPercent] = useState(0);
  const [selectedSharedFile, setSelectedSharedFile] = useState<File | null>(null);
  const [sharedUploading, setSharedUploading] = useState(false);
  const [sharedUploadPercent, setSharedUploadPercent] = useState(0);
  const [sharedSyncing, setSharedSyncing] = useState(false);

  const refreshStatus = useCallback(async (signal?: AbortSignal) => {
    try {
      const nextStatus = await fetchStatus(signal);
      setStatus(nextStatus);
      setNotice({
        tone: nextStatus.listenerActive ? 'good' : 'quiet',
        text: nextStatus.listenerActive ? 'Receiver is ready' : 'Receiver idle',
      });
    } catch {
      setStatus(null);
      setNotice({ tone: 'bad', text: 'C++ backend offline on 127.0.0.1:8787' });
    }
  }, []);

  const refreshFiles = useCallback(async (signal?: AbortSignal) => {
    try {
      const fileLists = await Promise.all(fileKinds.map((kind) => fetchFiles(kind, signal)));
      setFilesByKind({
        received: fileLists[0],
        sent: fileLists[1],
        shared: fileLists[2],
      });
    } catch {
      setFilesByKind({ received: [], sent: [], shared: [] });
    }
  }, []);

  useEffect(() => {
    const controller = new AbortController();
    void refreshStatus(controller.signal);
    void refreshFiles(controller.signal);
    const timer = window.setInterval(() => {
      void refreshStatus(controller.signal);
      void refreshFiles(controller.signal);
    }, 1200);

    return () => {
      controller.abort();
      window.clearInterval(timer);
    };
  }, [refreshFiles, refreshStatus]);

  const receivedFiles = filesByKind.received;
  const sharedFiles = filesByKind.shared;
  const syncPeers = status?.syncPeers ?? [];
  const uploadedFiles = useMemo<PanelFile[]>(() => {
    const stagedFile = selectedFile
      ? [
          {
            id: `staged-${selectedFile.name}`,
            name: selectedFile.name,
            detail: `${sending ? `${uploadPercent}%` : 'Ready'} / ${formatBytes(selectedFile.size)}`,
          },
        ]
      : [];

    const storedFiles = [...filesByKind.sent, ...filesByKind.shared].map((file) => ({
      id: `${file.kind}-${file.name}`,
      name: file.name,
      detail: `${file.kind} / ${formatBytes(file.size)}`,
      url: file.url,
    }));

    return [...stagedFile, ...storedFiles];
  }, [filesByKind.sent, filesByKind.shared, selectedFile, sending, uploadPercent]);

  const canSend = Boolean(selectedFile) && isAllowedPeerAddress(peerHost) && !sending;

  const handleFiles = (files: FileList | null) => {
    const file = firstSelectedFile(files);
    if (!file) {
      return;
    }
    setSelectedFile(file);
    setUploadPercent(0);
    setNotice({ tone: 'quiet', text: `${file.name} staged` });
  };

  const onDrop = (event: DragEvent<HTMLLabelElement>) => {
    event.preventDefault();
    setDragging(false);
    handleFiles(event.dataTransfer.files);
  };

  const onFileChange = (event: ChangeEvent<HTMLInputElement>) => {
    handleFiles(event.target.files);
  };

  const onSharedFileChange = (event: ChangeEvent<HTMLInputElement>) => {
    const file = firstSelectedFile(event.target.files);
    if (!file) {
      return;
    }
    setSelectedSharedFile(file);
    setSharedUploadPercent(0);
    setNotice({ tone: 'quiet', text: `${file.name} staged for shared upload` });
  };

  const onStartReceiver = async () => {
    try {
      await startReceiver(peerPort);
      await refreshStatus();
      setNotice({ tone: 'good', text: 'Receiver is ready' });
    } catch (error) {
      setNotice({ tone: 'bad', text: error instanceof Error ? error.message : 'Receiver failed' });
    }
  };

  const runSharedSync = async (quiet = false) => {
    if (sharedSyncing) {
      return null;
    }

    setSharedSyncing(true);
    if (!quiet) {
      setNotice({ tone: 'quiet', text: 'Syncing shared folder' });
    }

    try {
      const result = await syncSharedFolder();
      await refreshStatus();
      await refreshFiles();
      if (!quiet) {
        const peerLabel = result.peers === 1 ? 'peer' : 'peers';
        setNotice({
          tone: result.peers > 0 ? 'good' : 'bad',
          text:
            result.peers > 0
              ? `Shared sync checked ${result.files} files with ${result.peers} ${peerLabel}`
              : 'Add a sync peer before syncing.',
        });
      }
      return result;
    } catch (error) {
      if (!quiet) {
        setNotice({ tone: 'bad', text: error instanceof Error ? error.message : 'Shared sync failed' });
      }
      throw error;
    } finally {
      setSharedSyncing(false);
    }
  };

  const onAddSyncPeer = async () => {
    if (!isAllowedPeerAddress(peerHost)) {
      setNotice({ tone: 'bad', text: 'Use localhost or a private LAN IP.' });
      return;
    }

    try {
      await addSyncPeer({ host: peerHost, port: peerPort });
      const syncResult = await runSharedSync(true);
      await refreshStatus();
      setNotice({
        tone: 'good',
        text: syncResult
          ? `Shared folder syncing with ${peerHost}:${peerPort} / ${syncResult.files} files checked`
          : `Shared folder syncing with ${peerHost}:${peerPort}`,
      });
    } catch (error) {
      setNotice({ tone: 'bad', text: error instanceof Error ? error.message : 'Could not add sync peer' });
    }
  };

  const onRemoveSyncPeer = async (peer: SyncPeer) => {
    try {
      await removeSyncPeer(peer);
      await refreshStatus();
      setNotice({ tone: 'quiet', text: `Stopped shared sync with ${peer.host}:${peer.port}` });
    } catch (error) {
      setNotice({ tone: 'bad', text: error instanceof Error ? error.message : 'Could not remove sync peer' });
    }
  };

  const onUploadShared = async () => {
    if (!selectedSharedFile || sharedUploading) {
      return;
    }

    setSharedUploading(true);
    setSharedUploadPercent(0);
    setNotice({ tone: 'quiet', text: `Uploading ${selectedSharedFile.name} to shared` });
    try {
      const uploadedName = await uploadSharedFile({
        file: selectedSharedFile,
        onProgress: setSharedUploadPercent,
      });
      setSharedUploadPercent(100);
      setSelectedSharedFile(null);
      setNotice({ tone: 'good', text: `${uploadedName} added to shared` });
      if (syncPeers.length > 0) {
        await runSharedSync(true);
      }
      await refreshFiles();
      await refreshStatus();
    } catch (error) {
      setNotice({ tone: 'bad', text: error instanceof Error ? error.message : 'Shared upload failed' });
    } finally {
      setSharedUploading(false);
    }
  };

  const onSend = async () => {
    if (!selectedFile || !canSend) {
      return;
    }

    setSending(true);
    setUploadPercent(0);
    setNotice({ tone: 'quiet', text: `Sending ${selectedFile.name}` });
    try {
      await sendFile({
        file: selectedFile,
        peerHost,
        peerPort,
        onProgress: setUploadPercent,
      });
      setUploadPercent(100);
      setNotice({ tone: 'good', text: 'Transfer delivered' });
      await refreshStatus();
      await refreshFiles();
    } catch (error) {
      setNotice({ tone: 'bad', text: error instanceof Error ? error.message : 'Transfer failed' });
    } finally {
      setSending(false);
    }
  };

  return (
    <main className="loop-app">
      <header className="loop-nav">
        <Logo />
        <nav className="mode-tabs" aria-label="Transfer pages">
          <button
            type="button"
            className={activeView === 'transfer' ? 'active' : ''}
            aria-pressed={activeView === 'transfer'}
            onClick={() => setActiveView('transfer')}
          >
            Transfer
          </button>
          <button
            type="button"
            className={activeView === 'receive' ? 'active' : ''}
            aria-pressed={activeView === 'receive'}
            onClick={() => setActiveView('receive')}
          >
            Receive
          </button>
          <button
            type="button"
            className={activeView === 'shared' ? 'active' : ''}
            aria-pressed={activeView === 'shared'}
            onClick={() => setActiveView('shared')}
          >
            Shared
          </button>
        </nav>
      </header>

      {activeView === 'transfer' ? (
        <TransferScreen
          dragging={dragging}
          inboxCount={receivedFiles.length}
          notice={notice}
          peerHost={peerHost}
          peerPort={peerPort}
          selectedFile={selectedFile}
          sending={sending}
          uploadPercent={uploadPercent}
          uploadedFiles={uploadedFiles}
          canSend={canSend}
          onDrop={onDrop}
          onFileChange={onFileChange}
          onPeerHostChange={setPeerHost}
          onPeerPortChange={setPeerPort}
          onSend={onSend}
          onDragStateChange={setDragging}
        />
      ) : activeView === 'receive' ? (
        <ReceiveScreen
          inboxCount={receivedFiles.length}
          notice={notice}
          peerHost={peerHost}
          peerPort={peerPort}
          receivedFiles={receivedFiles}
          receiverActive={Boolean(status?.listenerActive)}
          onPeerHostChange={setPeerHost}
          onPeerPortChange={setPeerPort}
          onStartReceiver={onStartReceiver}
        />
      ) : (
        <SharedScreen
          inboxCount={sharedFiles.length}
          notice={notice}
          peerHost={peerHost}
          peerPort={peerPort}
          sharedFiles={sharedFiles}
          sharedDir={status?.sharedDir ?? 'shared'}
          syncPeers={syncPeers}
          selectedSharedFile={selectedSharedFile}
          sharedUploading={sharedUploading}
          sharedUploadPercent={sharedUploadPercent}
          sharedSyncing={sharedSyncing}
          onPeerHostChange={setPeerHost}
          onPeerPortChange={setPeerPort}
          onAddSyncPeer={onAddSyncPeer}
          onRemoveSyncPeer={onRemoveSyncPeer}
          onSyncShared={() => void runSharedSync()}
          onSharedFileChange={onSharedFileChange}
          onUploadShared={onUploadShared}
        />
      )}
    </main>
  );
}

function Logo() {
  return (
    <div className="loop-logo" aria-label="LOOP">
      <span>L</span>
      <span className="logo-flower" />
      <span className="logo-flower" />
      <span>P</span>
    </div>
  );
}

function firstSelectedFile(files: FileList | null): File | null {
  return files?.[0] ?? files?.item(0) ?? null;
}

function TransferScreen({
  dragging,
  inboxCount,
  notice,
  peerHost,
  peerPort,
  selectedFile,
  sending,
  uploadPercent,
  uploadedFiles,
  canSend,
  onDrop,
  onFileChange,
  onPeerHostChange,
  onPeerPortChange,
  onSend,
  onDragStateChange,
}: {
  dragging: boolean;
  inboxCount: number;
  notice: Notice;
  peerHost: string;
  peerPort: number;
  selectedFile: File | null;
  sending: boolean;
  uploadPercent: number;
  uploadedFiles: PanelFile[];
  canSend: boolean;
  onDrop: (event: DragEvent<HTMLLabelElement>) => void;
  onFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
  onPeerHostChange: (value: string) => void;
  onPeerPortChange: (value: number) => void;
  onSend: () => void;
  onDragStateChange: (dragging: boolean) => void;
}) {
  return (
    <section className="loop-page transfer-page" aria-labelledby="transfer-heading">
      <ScreenHeader
        id="transfer-heading"
        title="Transfer"
        metrics={[
          { text: notice.text, tone: notice.tone },
          { text: `Target ${peerHost}:${peerPort}` },
          { text: `Received ${inboxCount}` },
        ]}
      />

      <div className="transfer-layout">
        <div className="transfer-primary">
          <ProtocolControls
            inboxCount={inboxCount}
            peerHost={peerHost}
            peerPort={peerPort}
            onPeerHostChange={onPeerHostChange}
            onPeerPortChange={onPeerPortChange}
          />

          <section className="dark-frame upload-frame" aria-labelledby="uploaded-files-heading">
            <h2 id="uploaded-files-heading">Uploaded Files</h2>
            <label
              className={`upload-drop ${dragging ? 'dragging' : ''}`}
              onDragOver={(event) => {
                event.preventDefault();
                onDragStateChange(true);
              }}
              onDragLeave={() => onDragStateChange(false)}
              onDrop={onDrop}
            >
              <input type="file" onChange={onFileChange} />
              <span className="plus-tile" aria-hidden="true">
                <Plus size={42} strokeWidth={2.6} />
              </span>
              <span className="upload-prompt">Click anywhere to upload</span>
              {selectedFile ? (
                <span className="selected-file">
                  {selectedFile.name} / {formatBytes(selectedFile.size)}
                </span>
              ) : null}
            </label>

            {selectedFile ? (
              <div className="transfer-actions">
                <div className="upload-meter" aria-label="Upload progress">
                  <span style={{ width: `${uploadPercent}%` }} />
                </div>
                <button className="send-button" type="button" disabled={!canSend} onClick={onSend}>
                  {sending ? <Loader2 className="spin" size={18} /> : null}
                  <span>Send file</span>
                </button>
              </div>
            ) : null}

            {!isAllowedPeerAddress(peerHost) ? (
              <p className="action-note bad">Use localhost or a private LAN IP.</p>
            ) : null}
            {notice.tone !== 'quiet' ? <p className={`action-note ${notice.tone}`}>{notice.text}</p> : null}
          </section>
        </div>

        <aside className="dark-frame side-frame" aria-labelledby="side-uploaded-files-heading">
          <h2 id="side-uploaded-files-heading" className="frame-heading">
            <span>Uploaded Files</span>
            <strong aria-hidden="true">{uploadedFiles.length}</strong>
          </h2>
          <FilePanel files={uploadedFiles} />
        </aside>
      </div>
    </section>
  );
}

function ReceiveScreen({
  inboxCount,
  notice,
  peerHost,
  peerPort,
  receivedFiles,
  receiverActive,
  onPeerHostChange,
  onPeerPortChange,
  onStartReceiver,
}: {
  inboxCount: number;
  notice: Notice;
  peerHost: string;
  peerPort: number;
  receivedFiles: TransferFileEntry[];
  receiverActive: boolean;
  onPeerHostChange: (value: string) => void;
  onPeerPortChange: (value: number) => void;
  onStartReceiver: () => void;
}) {
  return (
    <section className="loop-page receive-page" aria-labelledby="receive-heading">
      <ScreenHeader
        id="receive-heading"
        title="Receive"
        metrics={[
          { text: receiverActive ? 'Receiver ready' : 'Receiver idle', tone: receiverActive ? 'good' : 'quiet' },
          { text: `Socket ${peerPort}` },
          { text: `Inbox ${inboxCount}` },
        ]}
      />

      <section className="receive-board" aria-label="Receive controls">
        <ProtocolControls
          variant="receive"
          inboxCount={inboxCount}
          peerHost={peerHost}
          peerPort={peerPort}
          onPeerHostChange={onPeerHostChange}
          onPeerPortChange={onPeerPortChange}
          action={
            <button className="receive-button" type="button" onClick={onStartReceiver}>
              {receiverActive ? 'Receiving' : 'Start receiving'}
            </button>
          }
        />

        {notice.tone !== 'quiet' ? <p className={`receive-note ${notice.tone}`}>{notice.text}</p> : null}

        <div className="received-frame">
          <PanelTitle label="RECEIVED FILES" count={receivedFiles.length} />
          <div className="received-body">
            <FilePanel files={receivedFiles.map(toPanelFile)} />
          </div>
        </div>
      </section>
    </section>
  );
}

function SharedScreen({
  inboxCount,
  notice,
  peerHost,
  peerPort,
  sharedFiles,
  sharedDir,
  syncPeers,
  selectedSharedFile,
  sharedUploading,
  sharedUploadPercent,
  sharedSyncing,
  onPeerHostChange,
  onPeerPortChange,
  onAddSyncPeer,
  onRemoveSyncPeer,
  onSyncShared,
  onSharedFileChange,
  onUploadShared,
}: {
  inboxCount: number;
  notice: Notice;
  peerHost: string;
  peerPort: number;
  sharedFiles: TransferFileEntry[];
  sharedDir: string;
  syncPeers: SyncPeer[];
  selectedSharedFile: File | null;
  sharedUploading: boolean;
  sharedUploadPercent: number;
  sharedSyncing: boolean;
  onPeerHostChange: (value: string) => void;
  onPeerPortChange: (value: number) => void;
  onAddSyncPeer: () => void;
  onRemoveSyncPeer: (peer: SyncPeer) => void;
  onSyncShared: () => void;
  onSharedFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
  onUploadShared: () => void;
}) {
  const canShare = isAllowedPeerAddress(peerHost);
  const canUploadShared = Boolean(selectedSharedFile) && !sharedUploading;
  const canSyncShared = syncPeers.length > 0 && !sharedSyncing;

  return (
    <section className="loop-page shared-page" aria-labelledby="shared-heading">
      <ScreenHeader
        id="shared-heading"
        title="Shared"
        metrics={[
          { text: notice.text, tone: notice.tone },
          { text: `Shared ${sharedFiles.length}` },
          { text: `Peers ${syncPeers.length}` },
        ]}
      />

      <section className="shared-board" aria-label="Shared folder controls">
        <ProtocolControls
          variant="receive"
          inboxCount={inboxCount}
          peerHost={peerHost}
          peerPort={peerPort}
          onPeerHostChange={onPeerHostChange}
          onPeerPortChange={onPeerPortChange}
          action={
            <div className="shared-action-group">
              <button className="shared-sync-button" type="button" disabled={!canSyncShared} onClick={onSyncShared}>
                <RefreshCw className={sharedSyncing ? 'spin' : undefined} size={16} />
                <span>Sync now</span>
              </button>
              <button className="receive-button" type="button" disabled={!canShare} onClick={onAddSyncPeer}>
                Start sharing
              </button>
            </div>
          }
        />

        {!canShare ? <p className="receive-note bad">Use localhost or a private LAN IP.</p> : null}
        {notice.tone !== 'quiet' ? <p className={`receive-note ${notice.tone}`}>{notice.text}</p> : null}

        <div className="shared-path-strip">
          <span>Shared folder</span>
          <strong>{sharedDir}</strong>
        </div>

        <div className="shared-upload-rail">
          <label className="shared-upload-target">
            <input aria-label="Shared file" type="file" onChange={onSharedFileChange} />
            <span className="mini-plus" aria-hidden="true">
              <Plus size={22} strokeWidth={2.8} />
            </span>
            <span>
              <strong>{selectedSharedFile ? selectedSharedFile.name : 'Click to stage shared upload'}</strong>
              <small>{selectedSharedFile ? formatBytes(selectedSharedFile.size) : 'Directly add a file to this shared folder'}</small>
            </span>
          </label>
          <div className="shared-upload-meter" aria-label="Shared upload progress">
            <span style={{ width: `${sharedUploadPercent}%` }} />
          </div>
          <button className="shared-upload-button" type="button" disabled={!canUploadShared} onClick={onUploadShared}>
            {sharedUploading ? <Loader2 className="spin" size={18} /> : null}
            <span>Upload to shared</span>
          </button>
        </div>

        <div className="shared-content-grid">
          <div className="shared-frame">
            <PanelTitle label="SHARED FILES" count={sharedFiles.length} />
            <div className="shared-body">
              <FilePanel files={sharedFiles.map(toPanelFile)} />
            </div>
          </div>

          <aside className="shared-frame sync-frame" aria-labelledby="sync-peers-heading">
            <PanelTitle id="sync-peers-heading" label="SYNC PEERS" count={syncPeers.length} />
            <div className="sync-body">
              {syncPeers.length > 0 ? (
                <div className="sync-peer-list">
                  {syncPeers.map((peer) => (
                    <button
                      className="sync-peer-row"
                      key={`${peer.host}:${peer.port}`}
                      type="button"
                      onClick={() => onRemoveSyncPeer(peer)}
                    >
                      <span>{peer.host}:{peer.port}</span>
                      <strong>Remove</strong>
                    </button>
                  ))}
                </div>
              ) : (
                <div className="sync-empty">No sync peers</div>
              )}
            </div>
          </aside>
        </div>
      </section>
    </section>
  );
}

function ScreenHeader({ id, title, metrics }: { id: string; title: string; metrics: StatusMetric[] }) {
  return (
    <div className="screen-title-row">
      <h1 id={id}>{title}</h1>
      <div className="status-rail" aria-label={`${title} status`}>
        {metrics.map((metric) => (
          <span className={`status-pill ${metric.tone ?? 'quiet'}`} key={metric.text}>
            {metric.text}
          </span>
        ))}
      </div>
    </div>
  );
}

function PanelTitle({ id, label, count }: { id?: string; label: string; count: number }) {
  return (
    <div className="received-title" id={id}>
      <span>{label}</span>
      <strong aria-label={`${label} count`}>{count}</strong>
    </div>
  );
}

function ProtocolControls({
  action,
  inboxCount,
  peerHost,
  peerPort,
  variant = 'transfer',
  onPeerHostChange,
  onPeerPortChange,
}: {
  action?: ReactNode;
  inboxCount: number;
  peerHost: string;
  peerPort: number;
  variant?: 'transfer' | 'receive';
  onPeerHostChange: (value: string) => void;
  onPeerPortChange: (value: number) => void;
}) {
  return (
    <div className={`protocol-strip ${variant}`}>
      <label className="protocol-field">
        <span>HTTP</span>
        <input aria-label="HTTP" value={peerHost} onChange={(event) => onPeerHostChange(event.target.value)} />
      </label>
      <label className="protocol-field">
        <span>Socket</span>
        <input
          aria-label="Socket"
          type="number"
          min={1024}
          max={65535}
          value={peerPort}
          onChange={(event) => onPeerPortChange(Number(event.target.value))}
        />
      </label>
      <label className="protocol-field">
        <span>Inbox count</span>
        <output aria-label="Inbox count">{inboxCount}</output>
      </label>
      {action ? <div className="protocol-action">{action}</div> : null}
    </div>
  );
}

function FilePanel({ files }: { files: PanelFile[] }) {
  if (files.length === 0) {
    return (
      <div className="blank-panel" aria-label="No files">
        <span>No files</span>
      </div>
    );
  }

  return (
    <div className="file-list">
      {files.map((file) =>
        file.url ? (
          <a className="file-row" key={file.id} href={file.url} target="_blank" rel="noreferrer">
            <strong>{file.name}</strong>
            <span>{file.detail}</span>
          </a>
        ) : (
          <div className="file-row" key={file.id}>
            <strong>{file.name}</strong>
            <span>{file.detail}</span>
          </div>
        ),
      )}
    </div>
  );
}

function toPanelFile(file: TransferFileEntry): PanelFile {
  return {
    id: `${file.kind}-${file.name}`,
    name: file.name,
    detail: formatBytes(file.size),
    url: file.url,
  };
}
