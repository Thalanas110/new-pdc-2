import type { ChangeEvent } from 'react';

import { formatBytes } from '../../lib/transferModel';

type PublishViewProps = {
  selectedFile: File | null;
  publishing: boolean;
  publishPercent: number;
  onFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
  onPublish: () => void;
};

export function PublishView({
  selectedFile,
  publishing,
  publishPercent,
  onFileChange,
  onPublish,
}: PublishViewProps) {
  const stagedSize = selectedFile ? formatBytes(selectedFile.size) : 'No file staged';

  return (
    <section className="view-shell" aria-labelledby="publish-heading">
      <header className="view-header">
        <div>
          <p className="view-eyebrow">Publisher Node</p>
          <h1 id="publish-heading">Publish</h1>
        </div>
        <p className="view-summary">
          Turn one local file into an immutable torrent manifest with verified pieces ready for reseeding.
        </p>
      </header>

      <div className="publish-grid">
        <label className={`dropzone-card ${selectedFile ? 'loaded' : ''}`}>
          <input aria-label="Publish file" type="file" onChange={onFileChange} />
          <span className="dropzone-card__tag">Publish intake</span>
          <strong>{selectedFile ? selectedFile.name : 'Choose a file to cut into swarm pieces'}</strong>
          <p>
            {selectedFile
              ? `${stagedSize} staged. Publishing will create a new torrent entry instead of overwriting an old one.`
              : 'Select one file. Each publish creates a fresh manifest, piece map, and seeding session.'}
          </p>
        </label>

        <section className="data-card data-card--ink" aria-labelledby="publish-ops-heading">
          <div className="data-card__eyebrow">Manifest profile</div>
          <h2 id="publish-ops-heading">Publisher operations</h2>
          <dl className="metric-list">
            <div>
              <dt>Staged size</dt>
              <dd>{stagedSize}</dd>
            </div>
            <div>
              <dt>Piece strategy</dt>
              <dd>256 KB verified chunks</dd>
            </div>
            <div>
              <dt>Lifecycle</dt>
              <dd>Publish to seed to fan out</dd>
            </div>
          </dl>

          <div className="progress-cluster">
            <div className="progress-cluster__row">
              <span>Publish progress</span>
              <strong>{publishPercent}%</strong>
            </div>
            <div className="meter">
              <span style={{ width: `${publishPercent}%` }} />
            </div>
          </div>

          <button
            type="button"
            className="action-button action-button--light"
            onClick={onPublish}
            disabled={!selectedFile || publishing}
          >
            {publishing ? 'Publishing...' : 'Publish file'}
          </button>
        </section>
      </div>
    </section>
  );
}
