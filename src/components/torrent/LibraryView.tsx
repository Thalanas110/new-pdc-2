import type { TorrentLibraryEntry } from '../../lib/transferModel';

type LibraryViewProps = {
  library: TorrentLibraryEntry[];
  onStartDownload: (torrentId: string) => void;
};

export function LibraryView({ library, onStartDownload }: LibraryViewProps) {
  return (
    <section className="loop-page" aria-labelledby="library-heading">
      <h1 id="library-heading">Library</h1>
      {library.length === 0 ? (
        <p>No published files yet</p>
      ) : (
        <ul>
          {library.map((entry) => (
            <li key={entry.torrentId}>
              <strong>{entry.displayName}</strong>
              <span>
                {' '}
                / {entry.seederCount} seeders / {entry.localStatus}
              </span>
              <button type="button" onClick={() => onStartDownload(entry.torrentId)}>
                Download
              </button>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
