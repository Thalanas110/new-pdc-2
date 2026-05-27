import { useEffect, useState } from 'react';

import type { FileKind, TransferFileEntry } from '../../lib/transferModel';
import { fileKinds, formatBytes, getFilePreviewKind } from '../../lib/transferModel';

type FilesViewProps = {
  kind: FileKind;
  files: TransferFileEntry[];
  onKindChange: (kind: FileKind) => void;
};

const kindLabels: Record<FileKind, string> = {
  received: 'Received',
  sent: 'Sent',
  shared: 'Shared',
};

export function FilesView({ kind, files, onKindChange }: FilesViewProps) {
  const [activeName, setActiveName] = useState<string | null>(null);

  useEffect(() => {
    if (files.length === 0) {
      setActiveName(null);
      return;
    }

    if (!activeName || !files.some((file) => file.name === activeName)) {
      setActiveName(files[0].name);
    }
  }, [activeName, files]);

  const selected = activeName ? files.find((file) => file.name === activeName) ?? null : null;
  const previewKind = selected ? getFilePreviewKind(selected.name, selected.contentType) : 'unsupported';

  const renderPreview = (file: TransferFileEntry) => {
    switch (previewKind) {
      case 'image':
        return <img className="file-preview__image" src={file.url} alt={file.name} />;
      case 'video':
        return <video className="file-preview__media" src={file.url} controls />;
      case 'audio':
        return <audio className="file-preview__media" src={file.url} controls />;
      case 'pdf':
        return <iframe className="file-preview__frame" src={file.url} title={file.name} />;
      case 'text':
        return <iframe className="file-preview__frame" src={file.url} title={file.name} />;
      default:
        return (
          <div className="file-preview__placeholder">
            <strong>Preview not available.</strong>
            <p>Use the download action to open this file on your device.</p>
          </div>
        );
    }
  };

  return (
    <section className="view-shell" aria-labelledby="files-heading">
      <header className="view-header">
        <div>
          <p className="view-eyebrow">Local Vault</p>
          <h1 id="files-heading">Files</h1>
        </div>
        <p className="view-summary">
          Browse files on this node, preview common formats, and download a copy to the device&apos;s downloads folder.
        </p>
      </header>

      <div className="file-kind-tabs" role="tablist" aria-label="File locations">
        {fileKinds.map((entry) => (
          <button
            key={entry}
            type="button"
            role="tab"
            aria-selected={kind === entry}
            onClick={() => onKindChange(entry)}
          >
            {kindLabels[entry]}
          </button>
        ))}
      </div>

      <div className="file-vault">
        <div className="file-vault__list">
          {files.length === 0 ? (
            <div className="empty-panel">
              <strong>No files found.</strong>
              <p>Publish or download a file first, then come back to preview or download it.</p>
            </div>
          ) : (
            <ul className="file-list">
              {files.map((file) => {
                const isActive = file.name === activeName;

                return (
                  <li key={`${file.kind}:${file.name}`} className={`file-card${isActive ? ' active' : ''}`}>
                    <button type="button" className="file-card__select" onClick={() => setActiveName(file.name)}>
                      <div>
                        <p className="torrent-card__label">{kindLabels[file.kind]}</p>
                        <strong>{file.name}</strong>
                      </div>
                      <span className="status-chip status-chip--complete">ready</span>
                    </button>
                    <dl className="torrent-card__stats">
                      <div>
                        <dt>Size</dt>
                        <dd>{formatBytes(file.size)}</dd>
                      </div>
                      <div>
                        <dt>Type</dt>
                        <dd>{file.contentType || 'unknown'}</dd>
                      </div>
                    </dl>
                    <div className="file-card__actions">
                      <a className="action-button action-button--dark" href={file.url} target="_blank" rel="noreferrer">
                        Open
                      </a>
                      <a className="action-button action-button--light" href={file.downloadUrl} download={file.name}>
                        Download
                      </a>
                    </div>
                  </li>
                );
              })}
            </ul>
          )}
        </div>

        <aside className="file-vault__preview data-card" aria-live="polite">
          {selected ? (
            <>
              <div className="data-card__eyebrow">Preview</div>
              <h2>{selected.name}</h2>
              <p className="file-preview__meta">
                {formatBytes(selected.size)} • {selected.contentType || 'unknown'}
              </p>
              <div className="file-preview">{renderPreview(selected)}</div>
              <div className="file-preview__actions">
                <a className="action-button action-button--dark" href={selected.url} target="_blank" rel="noreferrer">
                  Open
                </a>
                <a className="action-button action-button--light" href={selected.downloadUrl} download={selected.name}>
                  Download
                </a>
              </div>
            </>
          ) : (
            <>
              <div className="data-card__eyebrow">Preview</div>
              <h2>Select a file</h2>
              <p className="file-preview__meta">Pick a file to preview it here.</p>
            </>
          )}
        </aside>
      </div>
    </section>
  );
}
