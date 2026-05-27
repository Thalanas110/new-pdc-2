import type { TorrentDownloadEntry } from '../../lib/transferModel';
import { formatBytes, getTransferPercent } from '../../lib/transferModel';

type DownloadsViewProps = {
  downloads: TorrentDownloadEntry[];
};

export function DownloadsView({ downloads }: DownloadsViewProps) {
  return (
    <section className="view-shell" aria-labelledby="downloads-heading">
      <header className="view-header">
        <div>
          <p className="view-eyebrow">Session Monitor</p>
          <h1 id="downloads-heading">Downloads</h1>
        </div>
        <p className="view-summary">
          Track verified piece accumulation, active seeding peers, and the handoff from leecher to seeder.
        </p>
      </header>

      {downloads.length === 0 ? (
        <div className="empty-panel empty-panel--large">
          <strong>No active downloads.</strong>
          <p>Start one from the library and this panel will show piece verification and peer participation live.</p>
        </div>
      ) : (
        <ul className="download-stack">
          {downloads.map((entry) => {
            const progress = getTransferPercent(entry.verifiedPieces, entry.pieceCount);
            const isComplete = entry.status === 'seeding' || entry.status === 'complete';
            const previewUrl = `/api/library/open?torrentId=${encodeURIComponent(entry.torrentId)}`;
            const downloadUrl = `/api/library/download?torrentId=${encodeURIComponent(entry.torrentId)}`;

            return (
              <li key={entry.torrentId} className="download-card">
                <div className="download-card__head">
                  <div>
                    <p className="torrent-card__label">Download session</p>
                    <strong>{entry.displayName}</strong>
                  </div>
                  <span className={`status-chip status-chip--${entry.status}`}>{entry.status}</span>
                </div>

                <div className="progress-cluster">
                  <div className="progress-cluster__row">
                    <span>
                      {entry.verifiedPieces} of {entry.pieceCount} verified pieces
                    </span>
                    <strong>{progress}%</strong>
                  </div>
                  <div className="meter">
                    <span style={{ width: `${progress}%` }} />
                  </div>
                </div>

                <dl className="torrent-card__stats">
                  <div>
                    <dt>Payload</dt>
                    <dd>{formatBytes(entry.fileSize)}</dd>
                  </div>
                  <div>
                    <dt>Active peers</dt>
                    <dd>{entry.activePeers.length}</dd>
                  </div>
                  <div>
                    <dt>Role</dt>
                    <dd>{entry.status === 'seeding' ? 'Seeder' : 'Leecher'}</dd>
                  </div>
                </dl>

                {entry.activePeers.length > 0 ? (
                  <ul className="peer-pill-list">
                    {entry.activePeers.map((peer) => (
                      <li key={peer}>{peer}</li>
                    ))}
                  </ul>
                ) : (
                  <p className="field-note">Waiting for peer assignments or fresh bitfield responses.</p>
                )}

                {isComplete ? (
                  <div className="download-card__actions">
                    <a className="action-button action-button--dark" href={previewUrl} target="_blank" rel="noreferrer">
                      Preview
                    </a>
                    <a className="action-button action-button--light" href={downloadUrl} download={entry.displayName}>
                      Save to device
                    </a>
                  </div>
                ) : (
                  <p className="field-note">Preview unlocks after download verification finishes.</p>
                )}
              </li>
            );
          })}
        </ul>
      )}
    </section>
  );
}
