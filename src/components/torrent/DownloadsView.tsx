import type { TorrentDownloadEntry } from '../../lib/transferModel';

type DownloadsViewProps = {
  downloads: TorrentDownloadEntry[];
};

export function DownloadsView({ downloads }: DownloadsViewProps) {
  return (
    <section className="loop-page" aria-labelledby="downloads-heading">
      <h1 id="downloads-heading">Downloads</h1>
      {downloads.length === 0 ? (
        <p>No active downloads</p>
      ) : (
        <ul>
          {downloads.map((entry) => (
            <li key={entry.torrentId}>
              <strong>{entry.displayName}</strong>
              <span>
                {' '}
                / {entry.status} / {entry.verifiedPieces} of {entry.pieceCount}
              </span>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
