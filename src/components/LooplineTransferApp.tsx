import { Loader2, Plus } from 'lucide-react';
import { type ChangeEvent, type DragEvent, type ReactNode, useCallback, useEffect, useMemo, useState } from 'react';
import { fetchFiles, fetchStatus, sendFile, startReceiver } from '../lib/backendClient';
import {
  type BackendStatus,
  type FileKind,
  type TransferFileEntry,
  fileKinds,
  formatBytes,
  isAllowedPeerAddress,
} from '../lib/transferModel';

type Notice = {
  tone: 'good' | 'bad' | 'quiet';
  text: string;
};

type ViewMode = 'transfer' | 'receive';

type PanelFile = {
  id: string;
  name: string;
  detail: string;
  url?: string;
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
      ) : (
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
      <h1 id="transfer-heading">Transfer</h1>

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
          <h2 id="side-uploaded-files-heading">Uploaded Files</h2>
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
      <h1 id="receive-heading">Receive</h1>

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
          <div className="received-title">RECEIVED FILES</div>
          <div className="received-body">
            <FilePanel files={receivedFiles.map(toPanelFile)} />
          </div>
        </div>
      </section>
    </section>
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
        <span>Inbox</span>
        <input aria-label="Inbox" value={inboxCount} readOnly />
      </label>
      {action ? <div className="protocol-action">{action}</div> : null}
    </div>
  );
}

function FilePanel({ files }: { files: PanelFile[] }) {
  if (files.length === 0) {
    return <div className="blank-panel" aria-label="No files" />;
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
