import { createRootRoute } from '@tanstack/react-router';
import {
  Activity,
  AlertTriangle,
  CheckCircle2,
  Download,
  FileUp,
  HardDrive,
  Loader2,
  Play,
  RadioTower,
  Server,
  ShieldCheck,
  UploadCloud,
  X,
} from 'lucide-react';
import { type ChangeEvent, type DragEvent, useCallback, useEffect, useMemo, useState } from 'react';
import markUrl from '../assets/loopline-mark.svg';
import { fetchStatus, sendFile, startReceiver } from '../lib/backendClient';
import {
  type BackendStatus,
  type TransferRecord,
  formatBytes,
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

  useEffect(() => {
    const controller = new AbortController();
    void refreshStatus(controller.signal);
    const timer = window.setInterval(() => {
      void refreshStatus(controller.signal);
    }, 1200);

    return () => {
      controller.abort();
      window.clearInterval(timer);
    };
  }, [refreshStatus]);

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
        </div>
      </section>
    </main>
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
