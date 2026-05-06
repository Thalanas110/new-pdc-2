import { createRootRoute } from '@tanstack/react-router';
import {
  Activity,
  AlertTriangle,
  CheckCircle2,
  Download,
  Eye,
  FileUp,
  FileText,
  Film,
  FolderOpen,
  HardDrive,
  Image as ImageIcon,
  Loader2,
  Music,
  Play,
  RadioTower,
  RefreshCw,
  Server,
  ShieldCheck,
  UploadCloud,
  X,
} from 'lucide-react';
import { type ChangeEvent, type DragEvent, useCallback, useEffect, useMemo, useState } from 'react';
import markUrl from '../assets/loopline-mark.svg';
import { fetchFiles, fetchStatus, sendFile, startReceiver } from '../lib/backendClient';
import {
  type BackendStatus,
  type FileKind,
  type TransferRecord,
  type TransferFileEntry,
  formatBytes,
  getFilePreviewKind,
  getTransferPercent,
  isAllowedPeerAddress,
} from '../lib/transferModel';

type Notice = {
  tone: 'good' | 'bad' | 'quiet';
  text: string;
};

export const Route = createRootRoute({
  component: HomeView,
});

export function HomeView() {
  const [status, setStatus] = useState<BackendStatus | null>(null);
  const [notice, setNotice] = useState<Notice>({ tone: 'quiet', text: 'Backend waiting on 127.0.0.1:8787' });
  const [peerHost, setPeerHost] = useState('127.0.0.1');
  const [peerPort, setPeerPort] = useState(8788);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [fileKind, setFileKind] = useState<FileKind>('received');
  const [filesByKind, setFilesByKind] = useState<Record<FileKind, TransferFileEntry[]>>({
    received: [],
    sent: [],
  });
  const [selectedVaultFile, setSelectedVaultFile] = useState<TransferFileEntry | null>(null);
  const [dragging, setDragging] = useState(false);
  const [sending, setSending] = useState(false);
  const [uploadPercent, setUploadPercent] = useState(0);

  const refreshStatus = useCallback(async (signal?: AbortSignal) => {
    try {
      const nextStatus = await fetchStatus(signal);
      setStatus(nextStatus);
      const receiverMode = nextStatus.allowRemotePeers ? 'LAN' : 'loopback';
      setNotice({
        tone: nextStatus.listenerActive ? 'good' : 'quiet',
        text: nextStatus.listenerActive ? `Receiver armed on ${receiverMode}` : 'Receiver idle',
      });
    } catch {
      setStatus(null);
      setNotice({ tone: 'bad', text: 'C++ backend offline on 127.0.0.1:8787' });
    }
  }, []);

  const refreshFiles = useCallback(async (signal?: AbortSignal) => {
    try {
      const [received, sent] = await Promise.all([fetchFiles('received', signal), fetchFiles('sent', signal)]);
      setFilesByKind({ received, sent });
    } catch {
      setFilesByKind({ received: [], sent: [] });
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

  useEffect(() => {
    const activeFiles = filesByKind[fileKind];
    if (activeFiles.length === 0) {
      setSelectedVaultFile(null);
      return;
    }
    if (
      !selectedVaultFile ||
      selectedVaultFile.kind !== fileKind ||
      !activeFiles.some((entry) => entry.name === selectedVaultFile.name)
    ) {
      setSelectedVaultFile(activeFiles[0]);
    }
  }, [fileKind, filesByKind, selectedVaultFile]);

  const transfers = status?.transfers ?? [];
  const latestTransfer = transfers[0];
  const receivedFiles = useMemo(
    () => transfers.filter((transfer) => transfer.direction === 'incoming' && transfer.status === 'complete'),
    [transfers],
  );
  const canSend = Boolean(selectedFile) && isAllowedPeerAddress(peerHost) && !sending;

  const handleFiles = (files: FileList | null) => {
    const file = files?.item(0);
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

  const onStartReceiver = async () => {
    try {
      await startReceiver(peerPort);
      await refreshStatus();
      setNotice({ tone: 'good', text: 'Receiver is ready' });
    } catch (error) {
      setNotice({ tone: 'bad', text: error instanceof Error ? error.message : 'Receiver failed' });
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
    <main className="app-shell">
      <section className="command-surface" aria-label="Loopline transfer console">
        <header className="topbar">
          <div className="brand-lockup">
            <img className="brand-mark" src={markUrl} alt="" />
            <div>
              <p className="eyebrow">LAN P2P</p>
              <h1>Loopline</h1>
            </div>
          </div>

          <div className={`status-pill ${notice.tone}`} role="status">
            {notice.tone === 'bad' ? <AlertTriangle size={17} /> : <ShieldCheck size={17} />}
            <span>{notice.text}</span>
          </div>
        </header>

        <div className="workspace-grid">
          <section className="node-panel" aria-labelledby="node-heading">
            <div className="section-heading">
              <RadioTower size={18} />
              <h2 id="node-heading">Receiver</h2>
            </div>

            <div className="node-readout">
              <Metric label="HTTP" value={status ? `:${status.httpPort}` : ':8787'} icon={<Server size={18} />} />
              <Metric
                label="Socket"
                value={status ? `:${status.transferPort}` : `:${peerPort}`}
                icon={<Activity size={18} />}
              />
              <Metric
                label="Inbox"
                value={status ? `${receivedFiles.length}` : '0'}
                icon={<Download size={18} />}
              />
            </div>

            <div className="receiver-state">
              <span className={status?.listenerActive ? 'signal-dot live' : 'signal-dot'} />
              <span>
                {status?.listenerActive
                  ? `${status.allowRemotePeers ? 'LAN' : 'Loopback'} listener active`
                  : 'No active listener'}
              </span>
            </div>

            <button className="primary-action" type="button" onClick={onStartReceiver}>
              <Play size={18} />
              <span>Start receiver</span>
            </button>

            <div className="path-strip">
              <HardDrive size={16} />
              <span>{status?.receiveDir ?? 'backend\\received'}</span>
            </div>
          </section>

          <section className="send-panel" aria-labelledby="send-heading">
            <div className="section-heading">
              <UploadCloud size={18} />
              <h2 id="send-heading">Send</h2>
            </div>

            <div className="peer-grid">
              <label>
                <span>Peer host</span>
                <input value={peerHost} onChange={(event) => setPeerHost(event.target.value)} />
              </label>
              <label>
                <span>Peer port</span>
                <input
                  type="number"
                  min={1024}
                  max={65535}
                  value={peerPort}
                  onChange={(event) => setPeerPort(Number(event.target.value))}
                />
              </label>
            </div>

            <label
              className={`drop-target ${dragging ? 'dragging' : ''}`}
              onDragOver={(event) => {
                event.preventDefault();
                setDragging(true);
              }}
              onDragLeave={() => setDragging(false)}
              onDrop={onDrop}
            >
              <input type="file" onChange={onFileChange} />
              <FileUp size={28} />
              <span>{selectedFile ? selectedFile.name : 'Select file'}</span>
              <strong>{selectedFile ? formatBytes(selectedFile.size) : 'No file staged'}</strong>
            </label>

            <div className="send-row">
              <div className="progress-shell" aria-label="Upload progress">
                <span style={{ width: `${uploadPercent}%` }} />
              </div>
              <button className="send-action" type="button" disabled={!canSend} onClick={onSend}>
                {sending ? <Loader2 className="spin" size={18} /> : <UploadCloud size={18} />}
                <span>Send file</span>
              </button>
            </div>

            {!isAllowedPeerAddress(peerHost) ? (
              <div className="inline-alert">
                <X size={16} />
                <span>Use localhost or a private LAN IP</span>
              </div>
            ) : null}
          </section>

          <section className="ledger-panel" aria-labelledby="ledger-heading">
            <div className="section-heading">
              <CheckCircle2 size={18} />
              <h2 id="ledger-heading">Transfers</h2>
            </div>

            <TransferFocus transfer={latestTransfer} localPercent={sending ? uploadPercent : undefined} />

            <div className="transfer-list">
              {transfers.length > 0 ? (
                transfers.map((transfer) => <TransferRow key={transfer.id} transfer={transfer} />)
              ) : (
                <div className="empty-ledger">
                  <span>No transfers yet</span>
                </div>
              )}
            </div>
          </section>

          <FileVault
            activeKind={fileKind}
            filesByKind={filesByKind}
            selectedFile={selectedVaultFile}
            onKindChange={setFileKind}
            onSelectFile={setSelectedVaultFile}
            onRefresh={() => void refreshFiles()}
          />
        </div>
      </section>
    </main>
  );
}

function FileVault({
  activeKind,
  filesByKind,
  selectedFile,
  onKindChange,
  onSelectFile,
  onRefresh,
}: {
  activeKind: FileKind;
  filesByKind: Record<FileKind, TransferFileEntry[]>;
  selectedFile: TransferFileEntry | null;
  onKindChange: (kind: FileKind) => void;
  onSelectFile: (file: TransferFileEntry) => void;
  onRefresh: () => void;
}) {
  const activeFiles = filesByKind[activeKind];

  return (
    <section className="file-vault-panel" aria-labelledby="file-vault-heading">
      <div className="vault-toolbar">
        <div className="section-heading vault-title">
          <FolderOpen size={18} />
          <h2 id="file-vault-heading">File vault</h2>
        </div>

        <div className="vault-tabs" aria-label="File direction">
          <button type="button" aria-pressed={activeKind === 'received'} onClick={() => onKindChange('received')}>
            <Download size={16} />
            <span>Received</span>
            <strong>{filesByKind.received.length}</strong>
          </button>
          <button type="button" aria-pressed={activeKind === 'sent'} onClick={() => onKindChange('sent')}>
            <UploadCloud size={16} />
            <span>Sent</span>
            <strong>{filesByKind.sent.length}</strong>
          </button>
          <button className="icon-action" type="button" aria-label="Refresh files" onClick={onRefresh}>
            <RefreshCw size={16} />
          </button>
        </div>
      </div>

      <div className="vault-grid">
        <div className="vault-list" aria-label={`${activeKind} files`}>
          {activeFiles.length > 0 ? (
            activeFiles.map((file) => (
              <button
                key={`${file.kind}-${file.name}`}
                type="button"
                className={`vault-file ${selectedFile?.kind === file.kind && selectedFile.name === file.name ? 'active' : ''}`}
                onClick={() => onSelectFile(file)}
              >
                <FileGlyph file={file} />
                <span>
                  <strong>{file.name}</strong>
                  <small>{formatBytes(file.size)}</small>
                </span>
                <Eye size={15} />
              </button>
            ))
          ) : (
            <div className="empty-vault">
              <FolderOpen size={22} />
              <span>No {activeKind} files yet</span>
            </div>
          )}
        </div>

        <FilePreview file={selectedFile} />
      </div>
    </section>
  );
}

function FileGlyph({ file }: { file: TransferFileEntry }) {
  const kind = getFilePreviewKind(file.name, file.contentType);
  if (kind === 'image') return <ImageIcon size={18} />;
  if (kind === 'video') return <Film size={18} />;
  if (kind === 'audio') return <Music size={18} />;
  return <FileText size={18} />;
}

function FilePreview({ file }: { file: TransferFileEntry | null }) {
  if (!file) {
    return (
      <div className="preview-stage empty-preview">
        <FolderOpen size={34} />
        <span>Select a file to preview it here</span>
      </div>
    );
  }

  const previewKind = getFilePreviewKind(file.name, file.contentType);

  return (
    <div className="preview-stage">
      <div className="preview-header">
        <div>
          <span className="mono-label">{file.kind}</span>
          <strong>{file.name}</strong>
        </div>
        <span>{formatBytes(file.size)}</span>
      </div>

      <div className="preview-body">
        {previewKind === 'image' ? <img src={file.url} alt={file.name} /> : null}
        {previewKind === 'video' ? <video src={file.url} controls /> : null}
        {previewKind === 'audio' ? <audio src={file.url} controls /> : null}
        {previewKind === 'pdf' || previewKind === 'text' ? <iframe title={file.name} src={file.url} /> : null}
        {previewKind === 'unsupported' ? (
          <div className="unsupported-preview">
            <FileText size={36} />
            <strong>Preview unavailable</strong>
            <span>This file is stored locally, but the browser cannot render this format inline.</span>
          </div>
        ) : null}
      </div>
    </div>
  );
}

function Metric({ label, value, icon }: { label: string; value: string; icon: React.ReactNode }) {
  return (
    <div className="metric">
      {icon}
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}

function TransferFocus({ transfer, localPercent }: { transfer?: TransferRecord; localPercent?: number }) {
  const percent = transfer
    ? getTransferPercent(transfer.bytesTransferred, transfer.size)
    : typeof localPercent === 'number'
      ? localPercent
      : 0;

  return (
    <div className="transfer-focus">
      <div>
        <span className="mono-label">Active line</span>
        <strong>{transfer?.fileName ?? 'Standby'}</strong>
      </div>
      <div className="ring" style={{ '--value': `${percent}%` } as React.CSSProperties}>
        <span>{percent}%</span>
      </div>
    </div>
  );
}

function TransferRow({ transfer }: { transfer: TransferRecord }) {
  const percent = getTransferPercent(transfer.bytesTransferred, transfer.size);
  return (
    <article className="transfer-row">
      <div className={`direction ${transfer.direction}`}>
        {transfer.direction === 'incoming' ? <Download size={15} /> : <UploadCloud size={15} />}
      </div>
      <div className="transfer-copy">
        <strong>{transfer.fileName}</strong>
        <span>{transfer.message || transfer.peer}</span>
      </div>
      <div className="mini-progress" aria-label={`${transfer.fileName} progress`}>
        <span style={{ width: `${percent}%` }} />
      </div>
      <span className={`state-tag ${transfer.status}`}>{transfer.status}</span>
    </article>
  );
}
