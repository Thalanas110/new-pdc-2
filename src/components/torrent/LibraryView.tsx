import type { TorrentLibraryEntry } from '../../lib/transferModel';
import { formatBytes } from '../../lib/transferModel';

type LibraryViewProps = {
  library: TorrentLibraryEntry[];
  onStartDownload: (torrentId: string) => void;
};

export function LibraryView({ library, onStartDownload }: LibraryViewProps) {
  return (
    <section className="view-shell" aria-labelledby="library-heading">
      <header className="view-header">
        <div>
          <p className="view-eyebrow">Distributed Catalog</p>
          <h1 id="library-heading">Library</h1>
        </div>
        <p className="view-summary">
          Every row is one immutable torrent entry advertised through the swarm. Seeders increase as downloads finish.
        </p>
      </header>

      {library.length === 0 ? (
        <div className="empty-panel empty-panel--large">
          <strong>No published files yet.</strong>
          <p>Publish from one laptop first or bootstrap into a seeder to sync the distributed catalog.</p>
        </div>
      ) : (
        <ul className="torrent-grid">
          {library.map((entry) => (
            <li key={entry.torrentId} className="torrent-card">
              <div className="torrent-card__head">
                <div>
                  <p className="torrent-card__label">Torrent entry</p>
                  <strong>{entry.displayName}</strong>
                </div>
                <span className={`status-chip status-chip--${entry.localStatus}`}>{entry.localStatus}</span>
              </div>
              <dl className="torrent-card__stats">
                <div>
                  <dt>Size</dt>
                  <dd>{formatBytes(entry.fileSize)}</dd>
                </div>
                <div>
                  <dt>Pieces</dt>
                  <dd>{entry.pieceCount}</dd>
                </div>
                <div>
                  <dt>Seeders</dt>
                  <dd>{entry.seederCount}</dd>
                </div>
              </dl>
              <button type="button" className="action-button action-button--dark" onClick={() => onStartDownload(entry.torrentId)}>
                Download
              </button>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
